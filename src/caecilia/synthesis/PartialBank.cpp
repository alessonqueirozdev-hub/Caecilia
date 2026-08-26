// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/synthesis/PartialBank.h"

#include "caecilia/dsp/Kernels.h"

#include "caecilia/dsp/DspMath.h"

#include <cmath>

namespace caecilia::synth
{

namespace
{
    constexpr float  kTwoPi        = 6.28318530717958647692f;
    constexpr double kMinSampleRate = 1.0;
    constexpr float  kLn2Over1200  = 0.000577622650f; ///< ln(2)/1200: cents -> ratio (linear approx).
    constexpr float  kInvLog10Of2  = 3.321928095f;    ///< 1/log10(2), for dB <-> exp2 conversion.

    /// Highest pitch a mixture rank is allowed to reach before it breaks back an
    /// octave. Around 6 kHz is where organ builders in practice stop letting the
    /// crown climb — above it the ranks stop reinforcing the chorus and just hiss.
    constexpr double kMixtureCeilingHz = 6000.0;

    /// Convert decibels to a linear gain via exp2, which is materially cheaper
    /// than pow(10, x/20) and is called once per partial per note.
    [[nodiscard]] inline float dbToLinear(float db) noexcept
    {
        return std::exp2(db * (kInvLog10Of2 * 0.05f)); // 10^(db/20) == 2^(db/20 / log10(2))
    }

    /// Cheap ratio for a small cents offset: 2^(c/1200) ~= 1 + c*ln2/1200.
    /// Accurate to <1e-4 for |c| < ~30 cents, which covers all drift/glide here,
    /// and avoids a per-partial pow() on the audio thread.
    [[nodiscard]] inline float centsToRatioApprox(float cents) noexcept
    {
        return 1.0f + cents * kLn2Over1200;
    }

    /// Smoothstep (raised-cosine-like) 0->1 easing; kills the hard linear-ramp
    /// "blip" that made the attack sound synthetic.
    [[nodiscard]] inline float smoothstep01(float x) noexcept
    {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;
        return x * x * (3.0f - 2.0f * x);
    }

    /// One step of a cheap xorshift PRNG in [-1, 1). RT-safe, deterministic.
    [[nodiscard]] inline float nextBipolar(std::uint32_t& s) noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        // Map the top bits into [-1, 1).
        return static_cast<float>(static_cast<std::int32_t>(s)) * (1.0f / 2147483648.0f);
    }

    /// Reciprocal of a full turn in radians, for converting an analysed phase
    /// into the turns @ref dsp::fastSineTurns takes.
    constexpr float kInvTwoPi = 1.0f / kTwoPi;

    /// Denominator of the per-partial bloom ramp: the bloom reaches its longest
    /// value 5.64 octaves above 120 Hz, i.e. at the top of the compass.
    constexpr float kBloomOctaveSpan = 5.64f;

    /// Decibels of spectral tilt per unit of the wind's brightness coefficient.
    ///
    /// Six, because that is the unit the whole model is already written in: a
    /// spectral recipe's roll-off of 1.0 means -6 dB per octave of harmonic, so a
    /// brightness coefficient of 1.0 meaning "one more unit of roll-off" makes the
    /// two scales the same scale.
    ///
    /// What it comes to, against the demo organ's measured sag of a few percent
    /// under a chord: a Reed (coefficient 1.20) losing ten percent of its wind
    /// drops its sixteenth harmonic 2.9 dB while its fundamental does not move at
    /// all. That is what a plenum losing its wind sounds like -- duller, not merely
    /// flatter and quieter, which is all it did before.
    constexpr float kWindTiltDbPerOctave = 6.0f;

    /// Linear interpolation between a bass anchor and a treble anchor, keyed on a
    /// normalised note height in [0, 1].
    [[nodiscard]] inline float lerp(float lo, float hi, float t) noexcept
    {
        return lo + (hi - lo) * t;
    }
} // namespace

void PartialBank::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > kMinSampleRate ? sampleRate : 48000.0;

    // The ONLY allocation in this class: reserve the maximum partial storage up
    // front so seedFrom()/renderAdd() never touch the heap.
    partials_.assign(maxPartials_, Partial{});
    partialCount_ = 0;

    chunkL_.assign(kChunkFrames, 0.0f);
    chunkR_.assign(kChunkFrames, 0.0f);
    chunkEnv_.assign(kChunkFrames, 0.0f);

    setEnvelopeTimes(attackSeconds_, releaseSeconds_);
    stage_          = Stage::Idle;
    envGain_        = 0.0f;
    noteTimeSeconds_ = 0.0;
}

void PartialBank::seedFrom(const SpectralModel& model, float /*phaseAlignSeconds*/) noexcept
{
    // Copy only up to the reserved capacity; excess partials are dropped rather
    // than allocating. This keeps the call RT-safe.
    const std::size_t capacity = partials_.size();
    const std::size_t count    = model.partials.size() < capacity ? model.partials.size() : capacity;

    // Re-voicing a bank that is currently SOUNDING is the normal case when the
    // organist draws or retires a stop mid-chord, and it must not be audible as a
    // discontinuity. So while the bank speaks, the running oscillator state and
    // the per-partial gains are LEFT ALONE: the new spectrum becomes the target
    // that the existing per-sample gain ramp glides to over the next block, and
    // the phasors keep turning. The result is what a real instrument does — the
    // held notes change colour — instead of the note being cut dead.
    const bool sounding = (stage_ != Stage::Idle);

    // How this stop breaks, derived here rather than in trigger() because it is a
    // property of the SPECTRUM: this runs once per re-voicing and trigger() needs
    // the answer once per note.
    //
    // Two things have to come out of one pass over the ranks: which rule applies,
    // and how far it may go. A stop with two or more distinct ranks, all of them on
    // the organ series, is a COMPOUND and walks the series a row at a time. Anything
    // else -- one rank, or a pitch the series has no entry for, like a Tierce's 5 --
    // breaks by octaves, which is what keeps a tierce a tierce and a nazard a
    // nazard.
    //
    // This reads a bank as ONE STOP, which is what a bank is since one voice per
    // (rank, note): the plugin seeds each voice from a single rank's spectrum. The
    // pre-ARCH-001 composite path -- several stops merged into one spectrum, still
    // used by the bench tool -- would present a Fourniture and a lone Nazard as one
    // compound and break them together. That is wrong, and it is wrong only where
    // nothing musical is being judged.
    breakMode_      = BreakMode::None;
    breakTopRatio_  = 0.0f;
    breakTopSeries_ = -1;
    breakMaxShift_  = 0;
    {
        constexpr std::size_t kMaxRanks = 12;
        float       ranks[kMaxRanks]{};
        std::size_t rankCount = 0;
        bool        allOnSeries = true;
        float       lowest      = 0.0f;

        for (std::size_t i = 0; i < count; ++i)
        {
            const float rr = model.partials[i].rankRatioToF0;
            if (!(rr > 0.0f))
                continue;

            bool seen = false;
            for (std::size_t k = 0; k < rankCount; ++k)
                seen = seen || (ranks[k] > rr * 0.995f && ranks[k] < rr * 1.005f);
            if (seen)
                continue;

            if (rankCount < kMaxRanks)
                ranks[rankCount++] = rr;
            if (rr > breakTopRatio_)
                breakTopRatio_ = rr;
            if (lowest <= 0.0f || rr < lowest)
                lowest = rr;
            if (mixtureSeriesIndex(rr) < 0)
                allOnSeries = false;
        }

        if (rankCount >= 2 && allOnSeries)
        {
            breakMode_      = BreakMode::Series;
            breakTopSeries_ = mixtureSeriesIndex(breakTopRatio_);
            // Stop when the LOWEST rank reaches the unison: below it a mixture is
            // no longer crowning anything.
            breakMaxShift_  = mixtureSeriesIndex(lowest);
        }
        else if (rankCount >= 1)
        {
            breakMode_     = BreakMode::Octave;
            // Same floor, expressed in octaves: how many times the lowest rank can
            // halve while staying at or above unison.
            breakMaxShift_ = lowest > 1.0f
                           ? static_cast<int>(std::floor(std::log2(lowest)))
                           : 0;
            if (breakMaxShift_ <= 0)
                breakMode_ = BreakMode::None; // a unison rank has nowhere to break to
        }
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        const PartialTrack& t = model.partials[i];
        Partial& p          = partials_[i];
        p.ratioToF0         = t.ratioToF0;
        p.amplitude         = dbToLinear(t.ampDb);
        p.windSensitivity   = t.windSensitivity;
        p.onsetSeconds      = t.onsetSeconds;
        p.rankRatio         = t.rankRatioToF0;
        p.rankSeries        = static_cast<std::int8_t>(
                                  t.rankRatioToF0 > 0.0f
                                      ? mixtureSeriesIndex(t.rankRatioToF0) : -1);
        p.soundingRatio     = t.ratioToF0;
        // log2 of the ratio, so trigger() can reach a partial's absolute octave
        // by ADDING to the note's own log-pitch instead of taking a log2 per
        // partial per note-on. Guarded because a malformed model could carry a
        // non-positive ratio, and log2(0) is -inf.
        p.logRatio          = std::log2(t.ratioToF0 > 1.0e-6f ? t.ratioToF0 : 1.0e-6f);
        p.logSounding       = p.logRatio;
        p.blockGain         = 0.0f;
        p.gainInc           = 0.0f;

        if (!sounding || i >= partialCount_)
        {
            // Either the whole bank is silent, or this is a partial the previous
            // registration did not have -- a newly drawn rank, which starts silent
            // and ramps in rather than clicking on.
            //
            // Seed the quadrature oscillator at the analysed start phase. From here
            // on the phase is carried by the (oscX, oscY) phasor, never an angle.
            const float turns = t.phase * kInvTwoPi;
            p.curGain = 0.0f;
            p.oscX    = dsp::fastSineTurns(turns + 0.25f); // cos(phase)
            p.oscY    = dsp::fastSineTurns(turns);         // sin(phase)
            p.cosInc  = 1.0f;
            p.sinInc  = 0.0f;
        }

        // Seed a decorrelated PRNG per partial so every partial drifts on its own
        // trajectory (correlated drift would just be vibrato, not a living chorus).
        // The identity comes from the SPECTRUM, not from the array index: keying it
        // on the index made a registration sound different depending on the order
        // its stops were drawn in. Fall back to the index only for hand-built
        // models (tests, tools) that carry no seed.
        p.seed = t.seed != 0u ? t.seed
                              : (0x9E3779B9u ^ (static_cast<std::uint32_t>(i) * 2654435761u));
        p.rng = p.seed;
        if (p.rng == 0u) p.rng = 0x2545F491u;
        p.driftCents = 0.0f;
        p.bloomSeconds = 0.010f; // filled per note in trigger() from the real pitch.

        // Nearest integer harmonic (>=1) drives the per-note treble tilt. Its log2
        // is precomputed HERE, off the note-on path: trigger() used to call log2()
        // per partial, which on a 291-partial Tutti chord put thousands of
        // transcendentals inside a single audio callback.
        //
        // Counted from the RANK's fundamental, not from the 8' reference the
        // spectrum is written against. A 2' Doublette's fundamental arrives as ratio
        // 4, and counting it as the fourth harmonic voiced it down 12 dB at the top
        // of the compass -- so the Doublette slid 11.8 dB from MIDI 48 to 96 where
        // the Montre 8' slid 0.6, and the crown of the plenum went dull exactly
        // where it should have been brightest.
        const float base = t.rankBaseRatio > 1.0e-6f ? t.rankBaseRatio : 1.0f;
        const int h = static_cast<int>(p.ratioToF0 / base + 0.5f);
        p.harmonicIndex  = h < 1 ? 1 : h;
        p.logHarmonic    = std::log2(static_cast<float>(p.harmonicIndex));

        // The wind's brightness axis moves a partial in proportion to how far up
        // the rank's own harmonic series it sits -- a fundamental does not dull,
        // its sixteenth harmonic dulls four octaves' worth -- scaled by whatever
        // extra tracking the spectrum asked for.
        p.windBrightExp  = p.logHarmonic * (1.0f + t.brightnessTrack);
        p.noteLevelScale = 1.0f;
        p.formantGain    = 1.0f;
    }

    // Partials the new registration no longer has must be silenced, not left
    // running at their old gain.
    for (std::size_t i = count; i < partialCount_; ++i)
    {
        partials_[i].blockGain = 0.0f;
        partials_[i].amplitude = 0.0f;
        partials_[i].curGain   = 0.0f;
    }

    partialCount_    = count;
    if (!sounding)
        fundamentalHz_ = model.fundamentalHz;   // a live note keeps its own pitch
    steadyFormants_  = model.steadyFormants; // fixed-Hz formant (reeds); flues have peakCount==0
}

void PartialBank::setStereoSpread(float spread) noexcept
{
    stereoSpread_ = spread < 0.0f ? 0.0f : (spread > 1.0f ? 1.0f : spread);
}

void PartialBank::trigger(core::PipeId pipe, core::Velocity velocity, double frequencyHz) noexcept
{
    fundamentalHz_   = frequencyHz;
    noteTimeSeconds_ = 0.0;

    // Per-VOICE decorrelation salt. The seed gives every partial a scattered start
    // phase and its own drift PRNG, but those are keyed on the partial INDEX only —
    // so two voices sounding the same pitch (a unison coupler, or the same key on
    // two divisions) start phase-coherent and drift in lock-step, summing at +6 dB
    // instead of +3 dB. That coherent-sum hotspot is what forced the old bus duck.
    // Salting the phase and PRNG per (rank, note) makes the voices independent, so N
    // voices sum incoherently (~sqrt(N)) — the Aeolus/GrandOrgue headroom mechanism.
    const std::uint32_t voiceSalt =
        (static_cast<std::uint32_t>(pipe.rankId)     * 2654435761u)
      ^ (static_cast<std::uint32_t>(pipe.midiNote)   * 40503u)
      ^ (static_cast<std::uint32_t>(pipe.divisionId) * 2246822519u)
      ^ 0x85EBCA6Bu;
    const auto hash32 = [](std::uint32_t x) noexcept {
        x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x;
    };

    // A pipe organ is VELOCITY-FLAT: a key is either down or up, and the pipe always
    // speaks at its one voiced loudness. The old 0.4..1.0 velocity curve gave ~8 dB of
    // level swing from keystroke force, so — since no two struck keys share a MIDI
    // velocity — every note of a held chord came out at a different volume (the user's
    // "notes not at the same volume"). Fix it at unity; the pipe's level lives in the
    // voiced spectrum, not the keystroke.
    (void) velocity;
    velocityGain_ = 1.0f;

    // Retrigger (i.e. this slot was STOLEN from a sounding note) is handled
    // differently from a fresh note-on. Resetting the envelope to zero and
    // re-randomising every oscillator phase put a hard discontinuity into a
    // signal that was at full level a sample earlier — an audible click on every
    // steal. Because the spectrum is unchanged, keeping the envelope and the
    // phasors turns that cliff into a pitch change under a one-block gain ramp.
    const bool retrigger = (stage_ != Stage::Idle);

    const double f0 = fundamentalHz_ > 1.0 ? fundamentalHz_ : 120.0;

    // --- per-note quantities, computed ONCE (they do not vary by partial) -------
    // These four lines used to sit INSIDE the per-partial loop, so a 291-partial
    // Tutti paid for them 291 times per note — hundreds of microseconds of
    // transcendentals inside the audio callback, right when a chord arrives and the
    // render cost is already at its peak.
    const float logF0         = static_cast<float>(std::log2(f0));
    const float noteHeightRaw = (logF0 - 6.0311f) * 0.2f;   // log2(65.4) = 6.0311; 0 at C2, 1 five octaves up
    const float noteHeight    = noteHeightRaw < 0.0f ? 0.0f : (noteHeightRaw > 1.0f ? 1.0f : noteHeightRaw);
    const float tiltPerLog    = -trebleTiltDb_ * noteHeight;                     // dB per log2(harmonic)

    // Where this note sits above the bloom's 120 Hz reference. A partial's own
    // octave is this PLUS its precomputed log ratio, minus any break-back folds --
    // an add, where trigger() used to take a log2 per partial. On a Tutti chord
    // that was thousands of transcendentals inside a single audio callback.
    const float logF0Over120  = logF0 - 6.9069f;            // log2(120) = 6.9069

    // The wind's ATTACK axis, the fourth coefficient and the other one nothing
    // read. A pipe on slack wind speaks more slowly: its jet takes longer to set
    // the air column going. Read once at note-on rather than per block, because
    // speech is settled in the first few milliseconds and re-timing it under a
    // sustained note would be a wobble, not a speech.
    //
    // It has to reach the ENVELOPE and not only the per-partial blooms. Measured
    // on an A3: the blooms are 16 ms of a 33 ms speech and the envelope attack is
    // the rest, so stretching the blooms alone moved the speech 2.1% against a
    // coefficient asking for 12%.
    float speechStretch = 1.0f;
    if (wind_ != nullptr)
    {
        const float dev = wind_->pressureDeviation(chest_, 0);
        // Sag (negative deviation) lengthens the attack; surplus shortens it.
        // Clamped hard: this multiplies a time, and a runaway reservoir reading
        // must not be able to stall a pipe or make it click.
        speechStretch = 1.0f - windCurve_.attack(dev);
        speechStretch = speechStretch < 0.5f ? 0.5f
                      : (speechStretch > 2.0f ? 2.0f : speechStretch);
    }

    // Speech timing from the family profile, interpolated by pitch: a big bass pipe
    // fills slowly and collapses slowly, a small treble pipe speaks at once. The
    // RELEASE is left alone -- the coefficient is named for the attack, and a pipe
    // whose wind is failing does not take longer to stop.
    setEnvelopeTimes(speechStretch * lerp(speech_.attackAtC2Sec,  speech_.attackAtC7Sec,  noteHeight),
                     lerp(speech_.releaseAtC2Sec, speech_.releaseAtC7Sec, noteHeight));

    // Chest placement: consecutive semitones alternate sides of the case, exactly as
    // a real C-side / C#-side layout puts them, with a gentle extra spread up the
    // compass so the treble opens out. Equal-power so the centre energy is constant.
    // The C-side / C#-side alternation is a WITHIN-rank effect and stays modest;
    // the dominant separation is between ranks, which sit at different places in
    // the case. Making the rank term dominant is what actually decorrelates two
    // unison stops — with the note term dominant, every rank of a given key
    // clustered on the same side and they went on cancelling each other.
    const float side     = (pipe.midiNote & 1u) ? 1.0f : -1.0f;
    const float notePan  = side * stereoSpread_ * 0.35f * (0.6f + 0.4f * noteHeight);
    dsp::equalPowerPan(notePan, panL_, panR_);

    const float bloomSpan = (maxBloomSeconds_ - 0.008f) * speechStretch;

    // One break for the whole stop, worked out once. Every rank moves together by
    // the same number of series steps, which is what keeps them distinct and keeps
    // the mixture's interval shape intact across the break.
    const int breakShift = breakShiftForNote(pipe.midiNote);

    // Formant peak gains are constant for the whole note; converting them inside
    // the partial loop meant peakCount * partialCount exponentials per note-on.
    // Peak gains AND the reciprocal of each half-bandwidth: the latter was a
    // divide per peak per partial, and a peak's bandwidth does not vary by partial.
    float formantLin[kMaxFormants]{};
    float formantInvHalfBw[kMaxFormants]{};
    for (std::size_t k = 0; k < steadyFormants_.peakCount && k < kMaxFormants; ++k)
    {
        const FormantPeak& pk = steadyFormants_.peaks[k];
        formantLin[k]       = dbToLinear(pk.gainDb);
        formantInvHalfBw[k] = pk.bandwidthHz > 0.0f ? 2.0f / pk.bandwidthHz : 0.0f;
    }

    for (std::size_t i = 0; i < partialCount_; ++i)
    {
        Partial& p = partials_[i];
        // Scatter the start phase AND re-seed the drift PRNG per voice (not just per
        // partial index), so different voices are mutually decorrelated — never
        // re-align to coherence (coherent phase is the "electronic organ" click, and
        // coherent voices are the tutti hotspot).
        const std::uint32_t h = hash32(voiceSalt ^ (p.seed * 2246822519u));
        p.rng   = h | 1u;
        if (!retrigger)
        {
            // The start phase only has to be SCATTERED, so it is read off the unit
            // circle rather than computed: cos and sin of a random angle cost 33 ns
            // to arrive at a number whose sole requirement is that it be arbitrary.
            const dsp::Phasor start = dsp::randomPhasor(h >> 8);
            p.oscX = start.cos;
            p.oscY = start.sin;
            p.curGain = 0.0f;   // ramp up from silence (click-free onset)
        }
        p.driftCents = 0.0f;
        p.gainInc = 0.0f;

        // Mixture break-back. The rank this partial belongs to moves `breakShift`
        // steps down the organ series, and the partial moves with it -- including
        // when the partial is a HARMONIC of that rank rather than its fundamental,
        // because it is the same pipe speaking.
        //
        // This used to fold each partial on its own, halving it until it fell under
        // the ceiling, with only `ratio > 1.5` to stop it. Measured on the demo
        // organ's Fourniture IV, that left two distinct pitches out of four above
        // A5 -- two pairs of ranks landing together, so half the stop vanished --
        // and at the top of the compass a stop nominally at 2' was sounding a 4'
        // and a 5 1/3'. A quint below the unison, in a mixture.
        p.soundingRatio = p.ratioToF0;
        p.logSounding   = p.logRatio;
        if (breakShift > 0 && p.rankRatio > 0.0f)
        {
            // The scale is the RANK's move, applied to whatever this partial is --
            // the rank's own pitch or one of its harmonics. That is what keeps a
            // broken rank a harmonic series instead of a heap of partials that each
            // folded on a different schedule.
            const float scale =
                brokenRankRatio(p.rankRatio, p.rankSeries, breakShift) / p.rankRatio;
            p.soundingRatio *= scale;
            p.logSounding   += std::log2(scale);
        }

        const double freq = f0 * static_cast<double>(p.soundingRatio);

        // Per-partial attack bloom, derived from the partial's ABSOLUTE pitch so a
        // bass note blooms fast and treble partials speak progressively later.
        //
        // logSounding, not logRatio: a broken-back rank speaks at the pitch it
        // actually sounds. Using the unbroken ratio gives every rank of every
        // mixture the bloom of a pitch it is not playing, silently, across the
        // whole treble -- exactly where mixtures break.
        const float oct  = (logF0Over120 + p.logSounding) * (1.0f / kBloomOctaveSpan);
        const float frac = oct < 0.0f ? 0.0f : (oct > 1.0f ? 1.0f : oct);
        p.bloomSeconds   = 0.008f * speechStretch + bloomSpan * frac;

        // Per-note treble tilt (Aeolus _h_lev): the higher the NOTE, the more its
        // upper harmonics are voiced down — real pipes shed brightness up the
        // compass, so a static composite (bright everywhere) sounds synthetic.
        // logHarmonic was precomputed at seed time.
        //
        // This exp2 stays. It was measured at 8.3 ns against a 178 ns note-on, and
        // a hand-rolled fast exp2 recovered 1.9 ns of that. Tabulating it per
        // distinct harmonic looks better on a 189-partial composite and is a
        // pessimisation under one-voice-per-rank, where the distinct-harmonic count
        // equals the partial count.
        p.noteLevelScale = dbToLinear(tiltPerLog * p.logHarmonic);

        // Fixed-Hz formant boost (reeds): the boosted band sits at an absolute
        // frequency, so which harmonic it lifts changes note to note — the constant
        // brassy color of a Trompette. 0 dB baseline + peaks above (never silences
        // the inter-formant body).
        float fg = 1.0f;
        for (std::size_t k = 0; k < steadyFormants_.peakCount && k < kMaxFormants; ++k)
        {
            if (formantInvHalfBw[k] == 0.0f) continue;
            const float d = (static_cast<float>(freq) - steadyFormants_.peaks[k].centerHz)
                          * formantInvHalfBw[k];
            fg += (formantLin[k] - 1.0f) / (1.0f + d * d);
        }
        p.formantGain = fg < 0.05f ? 0.05f : (fg > 3.0f ? 3.0f : fg);

        // Offset this partial's rank from the note's own chest position. Ranks are
        // physically apart in the case, so two unison stops are two sources, not
        // one — which is exactly what stops them cancelling.
        // Ranks separate by up to +/-0.55 and the note term adds up to +/-0.35, so
        // the two never sum past hard-pan — if they did, every rank would clamp to
        // the same edge and the placement would collapse back to a point.
        const float rankOffset =
            (static_cast<float>((p.seed >> 8) & 0xFFFFu) / 65535.0f - 0.5f)
            * stereoSpread_ * 1.1f;
        float pan = notePan + rankOffset;
        pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
        dsp::equalPowerPan(pan, p.panL, p.panR);
    }

    // Arm the wind-attack chiff: a short filtered-noise breath, pitched by the
    // note (a one-pole low-pass a few multiples above the fundamental) with a fast
    // attack and a short exponential decay — the "speech" of a pipe starting.
    if (chiffAmount_ > 0.0f)
    {
        const double sr = sampleRate_ > kMinSampleRate ? sampleRate_ : 48000.0;
        const double fc = std::min(0.45 * sr, std::max(1500.0, f0 * 4.0));
        chiffLpCoef_  = static_cast<float>(std::min(1.0, 6.2831853 * fc / sr));
        chiffAtkSamp_ = static_cast<int>(0.003 * sr);
        if (chiffAtkSamp_ < 1) chiffAtkSamp_ = 1;
        const double decayTau = 0.045 * sr;                 // ~45 ms decay constant
        chiffDecayMul_ = static_cast<float>(std::exp(-1.0 / decayTau));
        chiffLenSamp_  = chiffAtkSamp_ + static_cast<int>(decayTau * 4.0);
        chiffSamp_ = 0;
        chiffEnv_  = 0.0f;
        chiffLp_   = 0.0f;
    }
    else
    {
        chiffLenSamp_ = 0;
    }

    stage_ = Stage::Attack;
    if (!retrigger)
        envGain_ = 0.0f; // a stolen voice keeps its level; only the pitch moves
}

float PartialBank::brokenRankRatio(float rankRatio, int seriesIndex,
                                   int shift) const noexcept
{
    if (shift <= 0 || !(rankRatio > 0.0f))
        return rankRatio;

    if (breakMode_ == BreakMode::Series)
    {
        const int to = seriesIndex - shift;
        if (to < 0 || seriesIndex < 0)
            return rankRatio;
        return kMixtureSeries[static_cast<std::size_t>(to)];
    }

    return rankRatio * std::exp2(static_cast<float>(-shift));
}

int PartialBank::breakShiftForNote(core::MidiNote note) const noexcept
{
    if (breakMode_ == BreakMode::None || breakMaxShift_ <= 0)
        return 0;

    // Breaks land at a C or an F sharp, and each one holds for the six semitones
    // it opens -- so the test is that span's TOP note, not the note being played.
    // Deciding it per note would put the break wherever the ceiling happened to be
    // crossed, and an organist would hear the crown wobble rather than step.
    //
    // Six semitones rather than twelve because of the arithmetic, not fashion: a
    // step down the organ series is a fourth or a fifth, and an octave is neither.
    // Breaking once an octave means every break has to be a DOUBLE step to keep the
    // crown under the ceiling -- 15-19-22-26 straight to 8-12-15-19, skipping the
    // row in between -- which is a bigger jump than any builder would put in an
    // instrument. A span of a tritone is close enough to one series step that each
    // break moves the composition exactly one row, which is what a break scheme is.
    const int spanTop = (static_cast<int>(note) / 6) * 6 + 5;

    // Equal-tempered on purpose. See the header: a break is a fact about the
    // compass, and a temperament that moved it would be absurd.
    const double f = 440.0 * std::exp2(static_cast<double>(spanTop - 69) / 12.0);

    int shift = 0;
    while (shift < breakMaxShift_
           && f * static_cast<double>(
                  brokenRankRatio(breakTopRatio_, breakTopSeries_, shift))
              > kMixtureCeilingHz)
        ++shift;

    return shift;
}

void PartialBank::release() noexcept
{
    if (stage_ != Stage::Idle)
        stage_ = Stage::Release;
}

void PartialBank::silence() noexcept
{
    stage_    = Stage::Idle;
    envGain_  = 0.0f;
    exprInc_  = 0.0f;
    chiffEnv_ = 0.0f;
    chiffLp_  = 0.0f;
    chiffLenSamp_ = 0;
    noteTimeSeconds_ = 0.0;
    for (std::size_t i = 0; i < partialCount_; ++i)
    {
        partials_[i].curGain   = 0.0f;
        partials_[i].blockGain = 0.0f;
        partials_[i].gainInc   = 0.0f;
    }
}

void PartialBank::setWindCoupling(const core::IWindSupply* wind,
                                  core::WindchestId chest,
                                  wind::WindResponseCurve curve) noexcept
{
    wind_      = wind;
    chest_     = chest;
    windCurve_ = curve;
}

void PartialBank::setEnvelopeTimes(float attackSeconds, float releaseSeconds) noexcept
{
    attackSeconds_  = attackSeconds  > 0.0f ? attackSeconds  : 0.0005f;
    releaseSeconds_ = releaseSeconds > 0.0f ? releaseSeconds : 0.0005f;

    const double sr = sampleRate_ > kMinSampleRate ? sampleRate_ : 48000.0;
    attackStep_  = static_cast<float>(1.0 / (static_cast<double>(attackSeconds_)  * sr));
    releaseStep_ = static_cast<float>(1.0 / (static_cast<double>(releaseSeconds_) * sr));
}

void PartialBank::setLiveliness(float instabilityCents,
                                float attackGlideCents,
                                float maxBloomSeconds,
                                float hfRolloffHz,
                                float trebleTiltDb) noexcept
{
    instabilityCents_ = instabilityCents < 0.0f ? 0.0f : instabilityCents;
    attackGlideCents_ = attackGlideCents;
    maxBloomSeconds_  = maxBloomSeconds < 0.008f ? 0.008f : maxBloomSeconds;
    hfRolloffHz_      = hfRolloffHz < 500.0f ? 500.0f : hfRolloffHz;
    trebleTiltDb_     = trebleTiltDb < 0.0f ? 0.0f : trebleTiltDb;
}

bool PartialBank::isActive() const noexcept
{
    return stage_ != Stage::Idle;
}

void PartialBank::recomputeBlockCoefficients(std::size_t frames) noexcept
{
    const double sr      = sampleRate_ > kMinSampleRate ? sampleRate_ : 48000.0;
    const double nyquist = 0.5 * sr;

    // Per-block wind coupling: read the pressure deviation once and translate it
    // into a global pitch/level shift through the tonal-family response curve.
    // TODO(phase2): sample the wind tap per control-rate sub-block for
    // sample-accurate FM/AM instead of one read per block.
    float deviation = 0.0f;
    if (wind_ != nullptr)
        deviation = wind_->pressureDeviation(chest_, 0);

    const double pitchRatio =
        std::exp2(static_cast<double>(windCurve_.pitchCents(deviation)) / 1200.0);
    const float levelLin = dbToLinear(windCurve_.levelDb(deviation));

    // The brightness axis: extra spectral tilt, in dB per octave of harmonic.
    //
    // Guarded rather than always applied, because a reservoir sitting at its
    // nominal pressure has nothing to say and this costs an exp2 per partial per
    // block. How much: measured with `caecilia-bench --perrank --wind -0.10`, a
    // 32-note tutti of 832 voices runs at 0.41 of a core either way -- the
    // difference is under the noise floor of a desktop, because one exp2 sits
    // beside the cos and sin this loop already takes per partial per block, and all
    // three are dwarfed by the per-sample render. The guard stays anyway: it costs
    // one compare, and at rest it is right every block.
    const float windTiltDb  = kWindTiltDbPerOctave * windCurve_.brightness(deviation);
    const bool  windTilting = windTiltDb > 0.001f || windTiltDb < -0.001f;

    // Attack pitch glide (bank-level): a labial pipe starts a touch flat and
    // "pulls" up to pitch as it speaks. Applied to every partial equally so the
    // whole tone glides in — a strong cue that this is a wind instrument, not a
    // switched-on oscillator.
    float glideRatio = 1.0f;
    if (noteTimeSeconds_ < static_cast<double>(glideSeconds_) && attackGlideCents_ != 0.0f)
    {
        const float frac  = static_cast<float>(noteTimeSeconds_) / glideSeconds_;
        const float cents = attackGlideCents_ * (1.0f - smoothstep01(frac));
        glideRatio = centsToRatioApprox(cents);
    }

    // The random-walk detune is updated once per block (control rate). Its
    // coefficient is derived from the block's DURATION, not fixed per block: a
    // fixed coefficient made the whole "living pipe" character a function of the
    // host's buffer size — a ~13 ms drift time constant at 64 samples became
    // ~213 ms at 1024, so the same project sounded different in different DAWs and
    // an offline render never matched real time.
    constexpr float kDriftTauSeconds = 0.15f;
    const float driftCoef = 1.0f - std::exp(-static_cast<float>(frames)
                                            / (kDriftTauSeconds * static_cast<float>(sr)));

    for (std::size_t i = 0; i < partialCount_; ++i)
    {
        Partial& p = partials_[i];

        // Advance this partial's independent slow pitch drift.
        p.driftCents += driftCoef * (nextBipolar(p.rng) * instabilityCents_ - p.driftCents);
        const float driftRatio = centsToRatioApprox(p.driftCents);

        // Per-partial wind sensitivity scales how much this partial tracks the
        // global deviation (brightness development lives here in a later phase).
        const double partialPitch = 1.0 + (pitchRatio - 1.0) * static_cast<double>(p.windSensitivity + 1.0f);
        const double freq = fundamentalHz_ * static_cast<double>(p.soundingRatio)
                          * partialPitch * static_cast<double>(glideRatio)
                          * static_cast<double>(driftRatio);

        // Anti-aliasing: drop partials above Nyquist and fade the top with a
        // raised-cosine (smoothstep) rather than a linear ramp, so a partial
        // sliding across the fade edge under drift/glide never steps its level.
        const float aaGain = smoothstep01(static_cast<float>((nyquist - freq) / (nyquist * 0.4)));

        // A partial at or above Nyquist contributes nothing, and letting its
        // oscillator run at that rate is both wasted work and a numerical hazard
        // (the old angle accumulator could only wrap one period per sample, so an
        // above-sample-rate partial drifted without bound and lost precision).
        // Freeze it instead: zero rotation, zero gain.
        if (freq >= nyquist || aaGain <= 0.0f)
        {
            p.cosInc    = 1.0f;
            p.sinInc    = 0.0f;
            p.blockGain = 0.0f;
            continue;
        }

        // Gentle per-note top-octave tilt (-6 dB/oct above the corner) that tames
        // the metallic upper partials of high notes without dulling foundations.
        float hfGain = 1.0f;
        if (freq > static_cast<double>(hfRolloffHz_))
            hfGain = hfRolloffHz_ / static_cast<float>(freq);

        // Per-partial attack bloom: the partial stays silent until its onset time,
        // then eases in over its (pitch-derived) bloom window with a smooth curve.
        // The onset delay is CAPPED very short: a long, frequency-progressive
        // stagger (some recipes delay the top harmonics by tens of ms) made the
        // brightness sweep up during the attack on harmonically-rich stops — an
        // artificial "wah". Capping it keeps all partials speaking within a tight
        // window, so the onset is soft but never sweeps.
        constexpr float kMaxOnset = 0.004f;
        const float effOnset = p.onsetSeconds < kMaxOnset ? p.onsetSeconds : kMaxOnset;
        const double dt = noteTimeSeconds_ - static_cast<double>(effOnset);
        const float  bloomGain = smoothstep01(static_cast<float>(dt) / p.bloomSeconds);

        // Rotation for this block. Two transcendentals per partial per BLOCK
        // replace one per partial per SAMPLE — with a 512-sample buffer that is a
        // ~250x reduction in transcendental work, and it is what makes a full
        // registration affordable at all.
        // The cast is explicit and kTwoPi stays a float: promoting the CONSTANT to a
        // double 2*pi would be more accurate and would move every partial's
        // rotation, which the fingerprint would report as a regression it is not.
        const float w = static_cast<float>(static_cast<double>(kTwoPi) * freq / sr);
        p.cosInc = std::cos(w);
        p.sinInc = std::sin(w);

        // The recursive rotation is only conditionally stable in float: |z| drifts
        // slowly away from 1. One Newton step for the inverse square root, once per
        // block, pins it back — cheap, and it never has to be exact.
        const float mag = p.oscX * p.oscX + p.oscY * p.oscY;
        const float renorm = 1.5f - 0.5f * mag; // ~1/sqrt(mag) for mag near 1
        p.oscX *= renorm;
        p.oscY *= renorm;

        p.blockGain = p.amplitude * aaGain * hfGain * bloomGain * levelLin
                    * p.noteLevelScale * p.formantGain;

        if (windTilting)
            p.blockGain *= dbToLinear(windTiltDb * p.windBrightExp);
    }
}

void PartialBank::renderAdd(core::AudioBlock& block) noexcept
{
    if (stage_ == Stage::Idle || block.isEmpty())
        return;

    const std::size_t frames   = block.numFrames();
    const std::size_t channels = block.numChannels();
    const double sr = sampleRate_ > kMinSampleRate ? sampleRate_ : 48000.0;

    recomputeBlockCoefficients(frames);

    // Per-partial gain ramp: interpolate each partial's gain from its previous
    // value to this block's target ACROSS the block, so the per-block coefficient
    // update (bloom, wind, drift) never steps at a block boundary. That step was a
    // subtle click at every note attack; ramping removes it (no zipper).
    const float invFrames = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;
    for (std::size_t i = 0; i < partialCount_; ++i)
        partials_[i].gainInc = (partials_[i].blockGain - partials_[i].curGain) * invFrames;

    float* const dstL = block.channel(0);
    float* const dstR = channels > 1 ? block.channel(1) : nullptr;

    // Partial-OUTER, frame-inner, in chunks. The old shape was the other way round
    // -- for each frame, walk every partial -- and it could not vectorise, because
    // summing the partials of one frame is a horizontal reduction. This way each
    // partial writes a run of frames with no reduction at all.
    for (std::size_t pos = 0; pos < frames; )
    {
        const std::size_t room = frames - pos;
        const std::size_t want = room < kChunkFrames ? room : kChunkFrames;

        // 1. The whole-bank envelope for this chunk, which is also what decides how
        //    long the chunk is: a release can reach Idle mid-block, and the frame it
        //    reaches zero on is still written (silently) exactly as before.
        std::size_t count = 0;
        while (count < want)
        {
            switch (stage_)
            {
                case Stage::Attack:
                    envGain_ += attackStep_;
                    if (envGain_ >= 1.0f) { envGain_ = 1.0f; stage_ = Stage::Sustain; }
                    break;
                case Stage::Release:
                    envGain_ -= releaseStep_;
                    if (envGain_ <= 0.0f) { envGain_ = 0.0f; stage_ = Stage::Idle; }
                    break;
                case Stage::Sustain:
                case Stage::Idle:
                default:
                    break;
            }

            // The swell shoe is ramped per SAMPLE, so dragging it under a held chord
            // is smooth rather than a staircase at block boundaries.
            exprGain_ += exprInc_;
            chunkEnv_[count] = smoothstep01(envGain_) * masterGain_ * velocityGain_
                             * exprGain_;
            ++count;

            // Purely an optimisation, and worth saying so: without it the frames
            // after the release ends still render, at an envelope of exactly zero,
            // and the output is identical. What it saves is rendering silence.
            if (stage_ == Stage::Idle)
                break;
        }

        if (count == 0)
            break;

        // 2. Every partial's whole run, four frames at a time. A silent partial is
        //    still rendered: its curGain is zero, and branching to skip it would cost
        //    more than the multiply-adds it saves.
        std::fill_n(chunkL_.data(), count, 0.0f);
        std::fill_n(chunkR_.data(), count, 0.0f);

        for (std::size_t i = 0; i < partialCount_; ++i)
        {
            Partial& p = partials_[i];

            dsp::kernels::PhasorState osc{ p.oscX, p.oscY };
            dsp::kernels::partialAccumulate(chunkL_.data(), chunkR_.data(), count,
                                            osc, p.cosInc, p.sinInc,
                                            p.curGain, p.gainInc, p.panL, p.panR);
            p.oscX = osc.x;
            p.oscY = osc.y;
        }

        // 3. Envelope, chiff and output. The chiff is a filtered noise burst and is
        //    per-sample by nature, so it stays here rather than in the kernel.
        for (std::size_t k = 0; k < count; ++k)
        {
            const float env = chunkEnv_[k];
            float toneL = chunkL_[k] * env;
            float toneR = chunkR_[k] * env;

            // After the tone envelope, so it speaks at full level right at onset
            // instead of being swallowed by the fade-in. It belongs to the note, not
            // to any one rank, so it sits at the note's own chest position.
            if (chiffLenSamp_ > 0 && chiffSamp_ < chiffLenSamp_)
            {
                const float noise = nextBipolar(chiffRng_);
                chiffLp_ += chiffLpCoef_ * (noise - chiffLp_);
                if (chiffSamp_ < chiffAtkSamp_)
                    chiffEnv_ = static_cast<float>(chiffSamp_) / static_cast<float>(chiffAtkSamp_);
                else
                    chiffEnv_ *= chiffDecayMul_;
                const float chiff = chiffLp_ * chiffEnv_ * chiffAmount_ * velocityGain_
                                  * masterGain_;
                toneL += chiff * panL_;
                toneR += chiff * panR_;
                ++chiffSamp_;
            }

            // A mono bus gets the sum of both sides, so a mono host never loses the
            // energy that the stereo placement moved off-centre.
            const std::size_t n = pos + k;
            if (dstR != nullptr)
            {
                if (dstL != nullptr) dstL[n] += toneL;
                dstR[n] += toneR;
            }
            else if (dstL != nullptr)
            {
                dstL[n] += (toneL + toneR) * 0.70710678f;
            }
        }

        pos += count;

        if (stage_ == Stage::Idle)
            break;
    }

    // Channels beyond stereo are the engine's business, not the voice's: the bus
    // already holds every other voice's contribution by this point, so copying
    // from it here would fold the whole mix into the extra channels.
    noteTimeSeconds_ += static_cast<double>(frames) / sr;
}

} // namespace caecilia::synth

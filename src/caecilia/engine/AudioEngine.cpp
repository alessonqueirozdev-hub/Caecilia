// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/engine/AudioEngine.h"

#include "caecilia/wind/OrganWind.h"

#include <algorithm>
#include <cmath>

namespace caecilia::core::engine
{

namespace
{
/// Gain a fully shut swell box leaves: -13 dB.
///
/// The FLAT half of a shutter. The spectral half is applyShutters, and the two
/// together are what makes a closed box read as a lid rather than as a fader.
constexpr float kShutGainConst = 0.2239f;

/// Corner of the shutter low-pass with the box wide open -- high enough to be
/// transparent, rather than bypassed, so a shoe leaving the top of its travel
/// glides instead of switching.
constexpr float kShutterOpenHz = 18000.0f;

/// Corner with the box shut. About fourteen decibels of extra treble loss at
/// 4 kHz, on top of the thirteen the flat gain already takes -- which is the
/// shape a real box has: a closed swell does not get quieter so much as dull.
constexpr float kShutterClosedHz = 800.0f;

/// Octaves between the two, precomputed so the per-block sweep is one exp2.
constexpr float kShutterOctaveSpan = 4.4918531f; // log2(18000 / 800)
} // namespace

// ---------------------------------------------------------------------------
// Off-thread setup.
// ---------------------------------------------------------------------------

void AudioEngine::prepare(SampleRate  sampleRate,
                          std::size_t maxBlockFrames,
                          std::size_t numChannels,
                          std::size_t numWindchests)
{
    sampleRate_     = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockFrames_ = maxBlockFrames;
    numChannels_    = numChannels > 0 ? numChannels : 2;
    numWindchests_  = std::clamp<std::size_t>(numWindchests, 1, kMaxWindchests);

    // A generous default so a full pool of nominal-cost voices renders unless the
    // host explicitly tightens the budget via setBlockBudget().
    if (blockBudgetUnits_ <= 0.0f)
        blockBudgetUnits_ = static_cast<float>(kMaxVoices);

    scheduler_.prepare(kMaxVoices, maxBlockFrames_);

    // Allocate the per-windchest accumulation buses as one flat block, plus the
    // stable channel-pointer table the AudioBlock views alias each callback.
    const std::size_t frames   = maxBlockFrames_;
    const std::size_t chanTot  = numWindchests_ * numChannels_;
    busSamples_.assign(chanTot * frames, 0.0f);
    busChannelPtrs_.assign(chanTot, nullptr);
    for (std::size_t c = 0; c < numWindchests_; ++c)
        for (std::size_t ch = 0; ch < numChannels_; ++ch)
        {
            const std::size_t flat = c * numChannels_ + ch;
            busChannelPtrs_[flat]  = busSamples_.data() + flat * frames;
        }
    busBlocks_.assign(numWindchests_, AudioBlock{});

    framePos_      = 0;
    blockFrames_   = 0;
    sustainDown_.fill(false);

    // Every shoe wide open. A division nobody has sent CC 11 for must sound, and
    // re-preparing must not leave a box shut that the player last opened.
    expressionTarget_.fill(1.0f);
    expressionCurrent_.fill(1.0f);
    expressionRamp_.fill(RenderContext::ExpressionRamp{});

    // One pole per chest per channel, and they start open: a chest nobody declared
    // enclosed is never touched, and one that is starts transparent rather than
    // muffling the first block.
    shutters_.assign(numWindchests_ * numChannels_, dsp::OnePole{});
    for (dsp::OnePole& f : shutters_)
    {
        f.prepare(sampleRate_);
        f.setLowpass(kShutterOpenHz);
        f.reset();
    }
    pendingMeters_ = MeterSnapshot{};
    meterSumSq_    = 0.0;
    meterSamples_  = 0;
    meters_.write(MeterSnapshot{});
}

void AudioEngine::reset() noexcept
{
    // Voices first: the buses and meters below describe what they produced.
    (void) pool_.silenceAll();
    heldKeyCount_ = 0;
    sustainDown_.fill(false);

    for (std::size_t c = 0; c < numWindchests_; ++c)
        for (std::size_t ch = 0; ch < numChannels_; ++ch)
            if (float* p = busChannelPtrs_[c * numChannels_ + ch])
                std::fill(p, p + maxBlockFrames_, 0.0f);

    // The swell shutters hold state, and zeroing the buses they filter is not the
    // same as clearing them: a one-pole with energy in it keeps outputting a decay
    // from a silent input. Short, but "immediately" is the contract a host reset
    // makes, and "A host reset silences the instrument immediately" caught this the
    // moment the shutters existed.
    for (dsp::OnePole& f : shutters_)
        f.reset();

    // Commands queued for a block that will never be rendered would otherwise
    // arrive after the reset and un-silence the instrument.
    while (commandRing_.drop()) {}

    pendingMeters_ = MeterSnapshot{};
    meterSumSq_    = 0.0;
    meterSamples_  = 0;
    meters_.write(MeterSnapshot{});
}

void AudioEngine::bindVoices(IVoice* const* voices, std::size_t count) noexcept
{
    pool_.bind(voices, count);
}

// ---------------------------------------------------------------------------
// Audio thread: the whole callback body.
// ---------------------------------------------------------------------------

void AudioEngine::processBlock(AudioBlock& output) noexcept
{
    const std::size_t total = output.numFrames();

    if (maxBlockFrames_ == 0 || total == 0)
    {
        // Not prepared. Emit silence rather than pass the host buffer through.
        for (std::size_t ch = 0; ch < output.numChannels(); ++ch)
            if (float* dst = output.channel(ch))
                std::fill(dst, dst + total, 0.0f);
        return;
    }

    blockFrames_ = total;

    // The engaged ranks, if the message thread has published a new set. Read once
    // per block rather than per note: a registration cannot change within a block,
    // and a note-on that fanned out across a half-updated table would sound a
    // registration that never existed.
    if (engagedRanks_.hasFresh())
    {
        const EngagedRankTable next = engagedRanks_.read();
        reconcileHeldKeys(next);
        ranks_ = next;
    }

    // Block-scoped, deliberately outside the slice loop:
    //
    //   * the CPU budget is an allowance for one callback, so resetting it per
    //     slice would hand a sixteen-event block sixteen full budgets and make
    //     the governor meaningless;
    //   * the wind step advances a reservoir ODE by one block.
    //
    // Both used to sit inside renderSlice(), which was harmless while a block was
    // only ever sliced by an oversized buffer and is not once events cut it.
    if (blockBudgetUnits_ > 0.0f)
        budget_.reset(blockBudgetUnits_);
    stepWind(total);
    beginMeters();

    // Render up to the next event, apply it, repeat.
    //
    // The block is also still cut at maxBlockFrames_, because a block larger than
    // the one prepare() was promised is a host contract violation that happens
    // anyway -- offline bounces and validators both do it -- and the old code
    // clamped the render and left the REST of the host's buffer untouched, so
    // whatever was in it survived into the master chain as a burst of noise.
    for (std::size_t done = 0; done < total;)
    {
        const std::size_t next = drainCommandsThrough(done, total);
        std::size_t       n    = next - done;          // >= 1 by construction
        if (n > maxBlockFrames_)
            n = maxBlockFrames_;

        AudioBlock slice = output.subBlock(done, n);
        renderSlice(slice);
        done += n;
    }

    publishMeters();
}

void AudioEngine::renderSlice(AudioBlock& output) noexcept
{
    const std::size_t numFrames = output.numFrames();

    zeroChestBuses(numFrames);
    stepExpression(numFrames);

    RenderContext ctx = makeContext(numFrames);

    pool_.refresh();
    scheduler_.renderActive(pool_.view(), ctx);

    // The spectral half of the swell box, on the bus rather than in the voices.
    applyShutters(numFrames);

    mixBusesToOutput(output, numFrames);
    applyMasterChain(output);

    framePos_ += static_cast<std::uint64_t>(numFrames);

    accumulateMeters(output, numFrames);
}

// ---------------------------------------------------------------------------
// processBlock stages.
// ---------------------------------------------------------------------------

std::size_t AudioEngine::drainCommandsThrough(std::size_t throughFrame,
                                              std::size_t totalFrames) noexcept
{
    while (const EngineCommand* cmd = commandRing_.peek())
    {
        // An offset past the end of the block lands on its LAST frame rather than
        // being deferred. Deferring would strand the command until the next
        // callback, and a note-off that arrives a block late is a stuck note.
        const std::size_t at = cmd->sampleOffset < totalFrames
                             ? static_cast<std::size_t>(cmd->sampleOffset)
                             : totalFrames - 1;

        if (at > throughFrame)
            return at; // belongs to a later slice; leave it queued

        // An offset EARLIER than where the renderer already is gets applied now,
        // never rewound. That asymmetry is deliberate: producers are not required
        // to be monotonic -- the console keyboard and the host-parameter bridge
        // both stamp 0 no matter when in the block they were enqueued -- and
        // reordering them against the host's timestamped MIDI would need a sort on
        // the audio thread and a correctness argument nobody has.
        applyCommand(*cmd);
        (void) commandRing_.drop();
    }

    // Nothing queued: render to the end of the block. Both exits return strictly
    // more than throughFrame (the caller only asks while throughFrame < total),
    // which is what guarantees the render loop always advances.
    return totalFrames;
}

void AudioEngine::applyCommand(const EngineCommand& cmd) noexcept
{
    switch (cmd.type)
    {
        case EngineCommandType::NoteOn:  handleNoteOn(cmd.payload.noteOn);   break;
        case EngineCommandType::NoteOff: handleNoteOff(cmd.payload.noteOff); break;
        case EngineCommandType::Panic:   handlePanic();                      break;
        case EngineCommandType::SetSustain: handleSustain(cmd.payload.sustain); break;
        case EngineCommandType::SetExpression:
            handleExpression(cmd.payload.expression);
            break;

        case EngineCommandType::SetReverbParams:
            if (masterReverb_ != nullptr)
                masterReverb_->setParams(cmd.payload.reverb.params);
            break;

        case EngineCommandType::ApplyStateDelta:
            // TODO(phase0.7): apply the registration StateDelta as a
            // click-free, wind-modeled stop crossfade.
            break;
        case EngineCommandType::SetTremulant:
            // Addressed by chest: the supply owns the chest-to-tremulant mapping,
            // and it is not the identity -- this organ has three chests and one
            // tremulant, on the Récit.
            if (wind_ != nullptr)
            {
                // Shape before enable, so a tremulant engaged in the same command
                // starts at the depth it was asked for rather than gliding up from
                // whatever the last one left behind.
                wind_->setChestTremulantShape(cmd.payload.tremulant.chest,
                                              cmd.payload.tremulant.rateHz,
                                              cmd.payload.tremulant.depthNorm);
                wind_->setChestTremulantEnabled(cmd.payload.tremulant.chest,
                                                cmd.payload.tremulant.enabled);
            }
            break;

        case EngineCommandType::SetWindParams:
            // TODO(phase0.2): retune the chest's nominal pressure and conductances.
            // Unlike the tremulant this needs WindModel to accept a live parameter
            // change, which configure() does not do today.
            break;
        case EngineCommandType::SetTemperament:
            // Deliberately nothing, and not a gap. Realising a temperament is 128
            // exp2 calls into a table a note-on may be reading, so it cannot happen
            // here whatever this case did. The processor watches the parameters,
            // rebuilds on the message thread, and publishes a snapshot the audio
            // thread adopts at a block boundary -- see tuning::LiveTuning.
            break;
        case EngineCommandType::StopEngage:
        case EngineCommandType::StopDisengage:
            // Informational; the audible change arrives via ApplyStateDelta.
            break;
        case EngineCommandType::None:
        default:
            break;
    }
}

void AudioEngine::stepWind(std::size_t numFrames) noexcept
{
    if (wind_ == nullptr)
        return;

    // The block's load, registered before the integration so the pressure the
    // voices are about to read is the pressure this chord actually produces.
    //
    // Summed over the LIVE slots, each carrying what its own pipe draws. A running
    // total kept at note-on and note-off would be cheaper and would drift: a voice
    // leaves in more ways than it arrives -- released, stolen, silenced by a panic,
    // retired along with its rank -- and every one of those paths would have to
    // remember to give the wind back.
    //
    // Registered on chest 0 alone, which sounds wrong and mostly is not: every
    // chest is fed by ONE reservoir, so the sag that matters -- the one a heavy
    // pedal chord puts into the Récit -- comes from the shared bellows and reaches
    // every chest regardless of which one booked the demand. What chest 0
    // additionally takes is its own trunk drop, and with the default conductances
    // (feed 4, trunk 50) that is about seven percent of the total.
    const VoicePoolView live = pool_.view();
    float demand = 0.0f;
    for (std::size_t i = 0; i < live.activeCount; ++i)
    {
        const std::size_t slot = live.activeIndices[i];
        if (slot < slotFlow_.size())
            demand += slotFlow_[slot];
    }

    wind_->registerDemand(WindchestId{ 0 }, demand);

    wind_->step(numFrames);
}

void AudioEngine::zeroChestBuses(std::size_t numFrames) noexcept
{
    for (std::size_t c = 0; c < numWindchests_; ++c)
        for (std::size_t ch = 0; ch < numChannels_; ++ch)
        {
            float* p = busChannelPtrs_[c * numChannels_ + ch];
            std::fill(p, p + numFrames, 0.0f);
        }
}

RenderContext AudioEngine::makeContext(std::size_t numFrames) noexcept
{
    // Rebuild the bus views for this block's frame count (no allocation: the
    // AudioBlock objects live in a pre-sized vector and just re-point).
    for (std::size_t c = 0; c < numWindchests_; ++c)
        busBlocks_[c] = AudioBlock(&busChannelPtrs_[c * numChannels_], numChannels_, numFrames);

    RenderContext ctx;
    ctx.sampleRate       = sampleRate_;
    ctx.numFrames        = numFrames;
    ctx.framePos         = framePos_;
    ctx.oversampleFactor = 1;
    ctx.nyquist          = sampleRate_ * 0.5;
    ctx.wind             = wind_;
    ctx.tuning           = tuning_;
    ctx.chestBuses       = busBlocks_.data();
    ctx.numChestBuses    = numWindchests_;

    ctx.expression = expressionRamp_;

    // This slice's share of the block, so the budget is spent once per block no
    // matter how many events cut it. See RenderContext::costScale.
    ctx.costScale = blockFrames_ > 0
                  ? static_cast<float>(numFrames) / static_cast<float>(blockFrames_)
                  : 1.0f;

    // The budget itself is reset once per BLOCK, in processBlock().
    ctx.budget = blockBudgetUnits_ > 0.0f ? &budget_ : nullptr;

    return ctx;
}

void AudioEngine::mixBusesToOutput(AudioBlock& output, std::size_t numFrames) noexcept
{
    const std::size_t outCh = output.numChannels();

    // Overwrite the host buffer with silence, then sum every chest bus in.
    for (std::size_t ch = 0; ch < outCh; ++ch)
        if (float* dst = output.channel(ch))
            std::fill(dst, dst + numFrames, 0.0f);

    const std::size_t ch = std::min(outCh, numChannels_);
    for (std::size_t c = 0; c < numWindchests_; ++c)
    {
        AudioBlock& bus = busBlocks_[c];
        for (std::size_t k = 0; k < ch; ++k)
        {
            const float* src = bus.channel(k);
            float*       dst = output.channel(k);
            if (src == nullptr || dst == nullptr)
                continue;
            for (std::size_t f = 0; f < numFrames; ++f)
                dst[f] += src[f];
        }
    }
}

void AudioEngine::applyMasterChain(AudioBlock& output) noexcept
{
    // TODO(phase0.3): insert per-pipe spatialization / early reflections before
    // the tail; for now the master chain is the reverb send only.
    if (masterReverb_ != nullptr)
        masterReverb_->process(output);
}

void AudioEngine::beginMeters() noexcept
{
    pendingMeters_ = MeterSnapshot{};
    meterSumSq_    = 0.0;
    meterSamples_  = 0;
}

void AudioEngine::accumulateMeters(const AudioBlock& output, std::size_t numFrames) noexcept
{
    // Folded in per slice, divided once per block. Computing an RMS per slice and
    // publishing each time meant the console's VU showed whatever the LAST slice
    // held -- on a block cut at a note-off, a 32-frame tail.
    float peak = pendingMeters_.master.peak;
    for (std::size_t ch = 0; ch < output.numChannels(); ++ch)
    {
        const float* p = output.channel(ch);
        if (p == nullptr)
            continue;
        for (std::size_t f = 0; f < numFrames; ++f)
        {
            const float s = p[f];
            const float a = s < 0.0f ? -s : s;
            if (a > peak)
                peak = a;
            meterSumSq_ += static_cast<double>(s) * static_cast<double>(s);
            ++meterSamples_;
        }
    }
    pendingMeters_.master.peak = peak;

    // The high-water mark across the block: a note that started in slice two is
    // part of what this block played, even if the count fell again by slice five.
    const auto active = static_cast<std::uint32_t>(pool_.activeCount());
    if (active > pendingMeters_.activeVoices)
        pendingMeters_.activeVoices = active;
}

void AudioEngine::publishMeters() noexcept
{
    pendingMeters_.framePos = framePos_;
    pendingMeters_.master.rms =
        meterSamples_ > 0
            ? static_cast<float>(std::sqrt(meterSumSq_ / static_cast<double>(meterSamples_)))
            : 0.0f;

    if (wind_ != nullptr)
    {
        const WindchestId chest0{0};
        pendingMeters_.windPressurePa = wind_->pressureAt(chest0, 0);
        pendingMeters_.windSagNorm    = wind_->pressureDeviation(chest0, 0);
    }
    // TODO(phase0.2): populate per-division meters and tremulant phase.

    meters_.write(pendingMeters_);
}

// ---------------------------------------------------------------------------
// Command handlers.
// ---------------------------------------------------------------------------

bool AudioEngine::startRankVoice(const EngagedRank& rank, MidiNote note,
                                 Velocity velocity) noexcept
{
    // The rank id lives in the PipeId, which is what makes one key several
    // distinguishable voices -- and what lets a note-off find all of them.
    const PipeId pipe{ static_cast<std::uint16_t>(rank.stop.value), note,
                       static_cast<std::uint8_t>(rank.division.value) };

    const VoiceHandle handle = pool_.acquire(pipe, framePos_);
    if (!handle.isValid())
        return false;

    IVoice* v = pool_.resolve(handle);
    if (v == nullptr)
        return false;

    // What this pipe draws from its chest. Volumetric flow scales with the pipe,
    // and a pipe scales with the wavelength it sounds -- so a 16' rank's pipe draws
    // twice an 8''s, and a bass pipe of either draws more again. Referenced to
    // middle C on an 8' rank, which is where windFlow == 1 and the note term == 1.
    //
    // Counting voices instead, as this did, made ten notes on a 2' Doublette load
    // the reservoir exactly as hard as ten on a 32' Bombarde.
    if (handle.index < slotFlow_.size())
    {
        const float octavesBelowMiddleC =
            (60.0f - static_cast<float>(note)) * (1.0f / 12.0f);
        slotFlow_[handle.index] =
            rank.windFlow * std::exp2(octavesBelowMiddleC) * wind::kFlowPerVoice;
    }

    v->adoptRank(rank.voicing);
    v->noteOn(pipe, velocity);
    return true;
}

void AudioEngine::rememberHeldKey(MidiNote note, DivisionId division,
                                  Velocity velocity) noexcept
{
    for (std::size_t i = 0; i < heldKeyCount_; ++i)
        if (heldKeys_[i].note == note && heldKeys_[i].division.value == division.value)
            return; // already down; a repeat is not a second key
    if (heldKeyCount_ < kMaxHeldKeys)
        heldKeys_[heldKeyCount_++] = HeldKey{ note, division, velocity };
}

void AudioEngine::forgetHeldKey(MidiNote note, DivisionId division) noexcept
{
    for (std::size_t i = 0; i < heldKeyCount_; ++i)
        if (heldKeys_[i].note == note && heldKeys_[i].division.value == division.value)
        {
            heldKeys_[i] = heldKeys_[--heldKeyCount_];
            return;
        }
}

void AudioEngine::reconcileHeldKeys(const EngagedRankTable& next) noexcept
{
    if (heldKeyCount_ == 0)
        return;

    const auto engagedIn = [](const EngagedRankTable& t, StopId stop)
    {
        for (std::size_t i = 0; i < t.count; ++i)
            if (t.ranks[i].stop.value == stop.value)
                return true;
        return false;
    };

    // Retired ranks: a real note-off on every held key, so the pipe stops the way
    // a pipe stops -- its own release, its own decay -- rather than vanishing.
    for (std::size_t i = 0; i < ranks_.count; ++i)
    {
        const EngagedRank& gone = ranks_.ranks[i];
        if (engagedIn(next, gone.stop))
            continue;
        for (std::size_t k = 0; k < heldKeyCount_; ++k)
            if (heldKeys_[k].division.value == gone.division.value)
                pool_.noteOff(PipeId{ static_cast<std::uint16_t>(gone.stop.value),
                                      heldKeys_[k].note,
                                      static_cast<std::uint8_t>(gone.division.value) });
    }

    // Newly drawn ranks: a real note-on from silence, with that rank's own speech.
    // A rank that was ALREADY sounding is not touched at all -- not re-seeded, not
    // ramped, nothing -- so it stays bit-identical across the change.
    for (std::size_t i = 0; i < next.count; ++i)
    {
        const EngagedRank& added = next.ranks[i];
        bool wasEngaged = false;
        for (std::size_t j = 0; j < ranks_.count; ++j)
            wasEngaged = wasEngaged || ranks_.ranks[j].stop.value == added.stop.value;
        if (wasEngaged)
            continue;

        for (std::size_t k = 0; k < heldKeyCount_; ++k)
        {
            if (heldKeys_[k].division.value != added.division.value)
                continue;
            if (pool_.freeCount() == 0
                && pool_.stealQuietestKey(scheduler_.stealPolicy()) == 0)
                break;
            (void) startRankVoice(added, heldKeys_[k].note, heldKeys_[k].velocity);
        }
    }
}

void AudioEngine::handleNoteOn(const EngineCommand::NoteOnPayload& p) noexcept
{
    // One key is one voice per engaged rank. A single Principal is one voice; a
    // Tutti is twenty-six, all starting together, each with its own speech timing,
    // its own place in the case and its own tuning -- which is what an organ is
    // and what a single composite voice cannot be.
    const std::size_t needed = ranks_.count;
    if (needed == 0)
        return; // nothing drawn: correctly silent

    // ADMISSION. Reserve the whole group or take none of it. Starting a key that
    // runs out of voices halfway leaves a chord with a hole in its registration --
    // a Tutti missing its mixtures, say -- which sounds like a broken instrument
    // rather than like a busy one. A key that does not speak at all sounds like a
    // key that was not pressed.
    //
    // The steal is a whole KEY for the same reason: taking one rank off a sounding
    // chord is more audible than ending one note early.
    while (pool_.freeCount() < needed)
        if (pool_.stealQuietestKey(scheduler_.stealPolicy()) == 0)
            return; // nothing left to take; the block is genuinely full

    rememberHeldKey(p.pipe.midiNote, p.division, p.velocity);

    for (std::size_t i = 0; i < needed; ++i)
    {
        const EngagedRank& rank = ranks_.ranks[i];
        // Only the ranks of the division the key belongs to. A drawstop on the
        // Récit does not sound when a Grand-Orgue key goes down -- that is what a
        // coupler is for, and couplers arrive as their own key events.
        if (rank.division.value != p.division.value)
            continue;
        (void) startRankVoice(rank, p.pipe.midiNote, p.velocity);
    }
}

void AudioEngine::handleNoteOff(const EngineCommand::NoteOffPayload& p) noexcept
{
    // By KEY, not by pipe. One key is now several voices that differ only in their
    // rank id, so matching a whole PipeId would release exactly one of them and
    // leave the rest of the registration sounding with the key up.
    //
    // It also means a key release stays one command on the ring however many stops
    // are drawn, instead of one per rank.
    forgetHeldKey(p.pipe.midiNote, p.division);

    const std::size_t div = p.pipe.divisionId;
    if (div < kMaxDivisions && sustainDown_[div])
        pool_.holdKeyForSustain(p.pipe.midiNote, p.division);
    else
        pool_.noteOffKey(p.pipe.midiNote, p.division);
}

void AudioEngine::handleSustain(const EngineCommand::SustainPayload& p) noexcept
{
    const std::size_t div = p.division.value;
    if (div >= kMaxDivisions)
        return;

    sustainDown_[div] = p.down;
    if (! p.down)
        pool_.releaseSustained(p.division);
}

void AudioEngine::handlePanic() noexcept
{
    pool_.allNotesOff();
    heldKeyCount_ = 0;
    sustainDown_.fill(false); // a panic must not leave the pedal latched down

    // The swell shoes are deliberately NOT touched. A panic is "stop the notes",
    // not "throw the boxes open": flinging a shut box open would make the next
    // thing the organist plays arrive at full volume, which is the opposite of
    // what they set the pedal for.
}

void AudioEngine::handleExpression(const EngineCommand::ExpressionPayload& p) noexcept
{
    const std::size_t div = p.division.value;
    if (div >= kExpressionDivisions)
        return;

    // Shut is not silent. A real swell box at its tightest still passes a good
    // deal of sound -- around -13 dB, which is where this ratio comes from -- and
    // a box that went to zero would read as a mute rather than as an enclosure.
    // Shared with applyShutters, which inverts it to recover the shoe position.
    constexpr float kShutGain = kShutGainConst;
    const float position = p.position < 0.0f ? 0.0f : (p.position > 1.0f ? 1.0f : p.position);
    expressionTarget_[div] = kShutGain + (1.0f - kShutGain) * position;
}

void AudioEngine::setEnclosedChests(std::span<const ChestEnclosure> enclosed) noexcept
{
    shoeForChest_.fill(-1);
    for (const ChestEnclosure& e : enclosed)
        if (e.chest.value < shoeForChest_.size()
            && e.division.value < kExpressionDivisions)
            shoeForChest_[e.chest.value] = static_cast<std::int16_t>(e.division.value);
}

void AudioEngine::applyShutters(std::size_t numFrames) noexcept
{
    if (numFrames == 0 || shutters_.empty())
        return;

    for (std::size_t c = 0; c < numWindchests_; ++c)
    {
        const std::int16_t shoe = shoeForChest_[c];
        if (shoe < 0)
            continue; // an open chest is never filtered and costs nothing

        // Back out the shoe's POSITION from the gain the ramp already carries.
        // Inverting rather than storing it twice: the gain is what glides, so the
        // position derived from it glides in exactly the same way, and there is no
        // second piece of state to keep in step.
        const float gain     = expressionCurrent_[static_cast<std::size_t>(shoe)];
        const float position = std::clamp(
            (gain - kShutGainConst) / (1.0f - kShutGainConst), 0.0f, 1.0f);

        // Geometric in frequency, because pitch is. Halfway down the shoe should
        // sound halfway shut, and a linear sweep of hertz would spend almost the
        // whole travel up where nothing is audible anyway.
        const float cutoff = kShutterClosedHz
                           * std::exp2(position * kShutterOctaveSpan);

        for (std::size_t ch = 0; ch < numChannels_; ++ch)
        {
            float* p = busChannelPtrs_[c * numChannels_ + ch];
            if (p == nullptr)
                continue;

            dsp::OnePole& f = shutters_[c * numChannels_ + ch];
            f.setLowpass(cutoff);
            for (std::size_t n = 0; n < numFrames; ++n)
                p[n] = f.process(p[n]);
        }
    }
}

void AudioEngine::stepExpression(std::size_t numFrames) noexcept
{
    if (numFrames == 0)
        return;

    // One-pole glide toward the target, expressed as a straight ramp ACROSS the
    // block. Publishing a level instead would step at every block boundary, which
    // on a shoe drag is a staircase; publishing a ramp costs the same and is
    // smooth to the sample.
    const double sr    = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
    const float  alpha = static_cast<float>(
        1.0 - std::exp(-static_cast<double>(numFrames) / (kExpressionGlideSeconds * sr)));
    const float invFrames = 1.0f / static_cast<float>(numFrames);

    for (std::size_t d = 0; d < kExpressionDivisions; ++d)
    {
        const float start = expressionCurrent_[d];
        const float end   = start + alpha * (expressionTarget_[d] - start);
        expressionCurrent_[d]   = end;
        expressionRamp_[d].start = start;
        expressionRamp_[d].inc   = (end - start) * invFrames;
    }
}

} // namespace caecilia::core::engine

/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/PluginProcessor.h"

#include "caecilia/plugin/PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace caecilia::plugin
{

namespace
{
/// Total simultaneous voices. Each key grabs one composite voice, so this is the
/// polyphony ceiling across every manual and the pedal at once.
constexpr std::size_t kPolyphony = 96;
} // namespace

synth::SpectralModel CaeciliaAudioProcessor::compositeSpectrum() const
{
    // Collect the drawn StopIds and delegate to the shared pure-core builder, so
    // the plugin and the headless render/verification harness voice identically.
    std::vector<core::StopId> engagedIds;
    engagedIds.reserve(engaged_.size());
    for (const model::Stop& stop : organ_.stops())
        if (stop.id().value < engaged_.size() && engaged_[stop.id().value])
            engagedIds.push_back(stop.id());

    return model::buildRegistrationCompositeSpectrum(organ_, engagedIds);
}

void CaeciliaAudioProcessor::buildInstrument(double sampleRate, std::size_t maxBlockFrames)
{
    const synth::SpectralModel composite = compositeSpectrum();

    synth::VoiceContext ctx;
    ctx.family  = core::TonalFamily::Principal;
    ctx.footage = core::footage::kEight; // composite is already referenced to 8'

    // Size every voice's partial bank to hold the WHOLE composite: the default
    // bank caps at 64 partials and silently drops the rest, which would mute the
    // last-ordered ranks of any full registration. Must precede prepare(), which
    // is what reserves the storage.
    const std::size_t maxPartials = std::max<std::size_t>(composite.partials.size(), 16);

    voices_.clear();
    voicePtrs_.clear();
    voices_.reserve(kPolyphony);
    voicePtrs_.reserve(kPolyphony);
    for (std::size_t i = 0; i < kPolyphony; ++i)
    {
        auto v = std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(maxPartials);
        v->prepare(sampleRate, maxBlockFrames);
        v->setContext(ctx);
        v->seedFrom(composite);
        voicePtrs_.push_back(v.get());
        voices_.push_back(std::move(v));
    }
}

CaeciliaAudioProcessor::CaeciliaAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output",
                                                        juce::AudioChannelSet::stereo(),
                                                        true))
    , parameters_(*this)
{
    // Compile the instrument once (off-thread). The console lays itself out from
    // it and the registration is a subset of its stops.
    organ_ = model::buildCaeciliaDemoOrgan();
    engaged_.assign(organ_.stops().size(), false);
    chooseDefaultRegistration();
}

// ---------------------------------------------------------------------------
// Registration defaults and control.
// ---------------------------------------------------------------------------

void CaeciliaAudioProcessor::chooseDefaultRegistration()
{
    // Primary manual = the division carrying the most stops (the Grand-Orgue on
    // the demo instrument); MIDI notes and key lights are routed here.
    const auto& stops = organ_.stops();
    std::array<int, ui::KeyStateSnapshot::kMaxDivisions> perDivision{};
    for (const model::Stop& s : stops)
        if (s.division().value < perDivision.size())
            ++perDivision[s.division().value];

    std::size_t best = 0;
    for (std::size_t d = 1; d < perDivision.size(); ++d)
        if (perDivision[d] > perDivision[best])
            best = d;
    playDivision_ = core::DivisionId{ static_cast<std::uint16_t>(best) };

    // Draw a classic opening plenum on the primary manual: the whole principal
    // chorus, its mixtures, and an 8' flute foundation for body.
    for (const model::Stop& s : stops)
    {
        if (s.division() != playDivision_)
            continue;
        const bool principalChorus = s.family() == core::TonalFamily::Principal
                                  || s.family() == core::TonalFamily::Mixture;
        const bool fluteFoundation = s.family() == core::TonalFamily::Flute
                                  && s.footage() == core::footage::kEight;
        if (principalChorus || fluteFoundation)
            engaged_[s.id().value] = true;
    }

    // Safety net: if the heuristic drew nothing, engage the first 8' stop found so
    // the instrument is never silent out of the box.
    if (std::none_of(engaged_.begin(), engaged_.end(), [](bool b) { return b; }))
        for (const model::Stop& s : stops)
            if (s.footage() == core::footage::kEight)
            {
                engaged_[s.id().value] = true;
                break;
            }
}

bool CaeciliaAudioProcessor::isStopEngaged(core::StopId stop) const noexcept
{
    return stop.value < engaged_.size() && engaged_[stop.value];
}

void CaeciliaAudioProcessor::toggleStop(core::StopId stop)
{
    if (stop.value >= engaged_.size())
        return;
    engaged_[stop.value] = ! engaged_[stop.value];

    if (getSampleRate() <= 0.0)
        return; // not prepared yet; the new state applies at the next prepareToPlay

    swapVoicesFromComposite(compositeSpectrum());
}

void CaeciliaAudioProcessor::setUiRegistration(const std::vector<model::RegistrationRank>& ranks)
{
    if (getSampleRate() <= 0.0)
        return; // not prepared yet
    swapVoicesFromComposite(model::buildCompositeFromRegistration(ranks));
}

void CaeciliaAudioProcessor::setUiReverb(int spaceIndex, float mix)
{
    const auto preset = static_cast<dsp::ReverbPreset>(juce::jlimit(0, 4, spaceIndex));
    core::ReverbParams params = dsp::FdnReverb::presetParams(preset);
    params.mix = juce::jlimit(0.0f, 1.0f, mix);
    const juce::ScopedLock sl(getCallbackLock());
    reverb_.setParams(params); // RT-safe snapshot swap
}

void CaeciliaAudioProcessor::swapVoicesFromComposite(const synth::SpectralModel& composite)
{
    const double sr = getSampleRate();
    if (sr <= 0.0)
        return;

    // Build the new voice bank off the audio thread, then swap it in under the
    // processing lock so the audio thread never renders a half-rebuilt pool.
    const auto frames = static_cast<std::size_t>(juce::jmax(1, getBlockSize()));
    const std::size_t maxPartials = std::max<std::size_t>(composite.partials.size(), 16);

    synth::VoiceContext ctx;
    ctx.family  = core::TonalFamily::Principal;
    ctx.footage = core::footage::kEight;

    std::vector<std::unique_ptr<synth::AdditiveVoice>> newVoices;
    std::vector<core::IVoice*>                          newPtrs;
    newVoices.reserve(kPolyphony);
    newPtrs.reserve(kPolyphony);
    for (std::size_t i = 0; i < kPolyphony; ++i)
    {
        auto v = std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(maxPartials);
        v->prepare(sr, frames);
        v->setContext(ctx);
        v->seedFrom(composite);
        newPtrs.push_back(v.get());
        newVoices.push_back(std::move(v));
    }

    {
        const juce::ScopedLock sl(getCallbackLock());
        engine_.bindVoices(newPtrs.data(), newPtrs.size());
        voices_.swap(newVoices);
        voicePtrs_.swap(newPtrs);
    }
    // newVoices now holds the previous bank; it is freed here, after the lock is
    // released and the engine has been rebound away from it. No use-after-free.
}

void CaeciliaAudioProcessor::uiNote(core::DivisionId division, core::MidiNote note, bool down)
{
    // Lock-free hand-off to the audio thread; drop if the (large) ring is full.
    // The clicked division travels with the event so the right keyboard sounds
    // and lights (not always the primary manual).
    (void) uiNotes_.push(UiNoteEvent{ division, note, down });
}

// ---------------------------------------------------------------------------
// Lifecycle (off-thread). prepareToPlay is the ONLY place allocation happens.
// ---------------------------------------------------------------------------

void CaeciliaAudioProcessor::prepareToPlay(double sampleRate, int maxBlockSamples)
{
    const auto frames   = static_cast<std::size_t>(juce::jmax(0, maxBlockSamples));
    const auto channels = static_cast<std::size_t>(juce::jmax(1, getTotalNumOutputChannels()));

    // The single allocation point: size every RT buffer from the host contract.
    // TODO(phase0.1): windchest count comes from the loaded OrganSpec.
    engine_.prepare(sampleRate, frames, channels, /*numWindchests*/ 1);

    // Bind the single-producer command path and the metering read path. Route
    // MIDI note-ons to the primary manual so they light the right keyboard.
    commandBridge_.connect(engine_.commandQueue());
    commandBridge_.setDefaultDivision(playDivision_);
    commandBridge_.resetChangeTracking();
    meterBridge_.connect(engine_);

    // Equal-tempered A=440 tuning table (historical temperaments swap in here via
    // the temperament parameter in a later phase). Bound read-only into the engine.
    tuning_.setReferenceA4Hz(440.0);
    engine_.setTuning(&tuning_);

    // Build the drawn registration's voices and bind them into the engine.
    buildInstrument(sampleRate, frames);
    engine_.bindVoices(voicePtrs_.data(), voicePtrs_.size());

    // Master reverb: a large-hall preset by default; the parameter bridge then
    // syncs it to the APVTS reverb controls on the first processed block.
    reverb_.prepare(sampleRate, frames, channels);
    reverb_.setPreset(dsp::ReverbPreset::Hall);
    engine_.setMasterReverb(&reverb_);

    // On-screen keyboard -> audio thread: clear any stale queued notes and
    // pre-reserve the scratch MIDI buffer so merging UI notes never allocates on
    // the audio thread (ample for the 512-slot event ring).
    { UiNoteEvent drain; while (uiNotes_.pop(drain)) {} }
    uiScratch_.ensureSize(8192);
    hostScratch_.ensureSize(8192); // host MIDI minus swallowed page-turn keys
    keys_ = {};

    masterGain_.reset(sampleRate, 0.02); // 20 ms output-trim ramp
    masterGain_.setCurrentAndTargetValue(1.0f);

    updateLatency();
}

void CaeciliaAudioProcessor::releaseResources()
{
    // Engine buffers persist until the next prepare(); nothing to free here. The
    // no-allocation contract means there is no per-run scratch to release.
}

bool CaeciliaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Instrument: no input bus; mono or stereo output only.
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// ---------------------------------------------------------------------------
// Audio callback (real-time thread).
// ---------------------------------------------------------------------------

void CaeciliaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals; // per-thread FTZ/DAZ for the whole callback

    const auto numChannels = static_cast<std::size_t>(getTotalNumOutputChannels());
    const auto numFrames   = static_cast<std::size_t>(buffer.getNumSamples());

    // --- Sequencer page-turn: swallow the configured nav keys and turn them into
    // Previous/Next intents (delivered to the console by the editor's timer). The
    // remaining host MIDI is copied into a pre-reserved scratch buffer so the nav
    // keys never sound a pipe and never light the manual.
    const bool navEnabled = seqNavEnabled_.load(std::memory_order_relaxed);
    const int  navPrev     = seqPrevNote_.load(std::memory_order_relaxed);
    const int  navNext     = seqNextNote_.load(std::memory_order_relaxed);
    int        navLearn    = seqLearn_.load(std::memory_order_relaxed);
    hostScratch_.clear();
    for (const juce::MidiMessageMetadata meta : midi)
    {
        const juce::MidiMessage m = meta.getMessage();
        if (m.isNoteOn() || m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            // MIDI-learn: the first note-on while armed becomes the binding and is
            // swallowed (it neither sounds nor steps the sequencer).
            if (navLearn != 0 && m.isNoteOn())
            {
                seqLearnedNote_.store(navLearn * 256 + note, std::memory_order_relaxed);
                seqLearn_.store(0, std::memory_order_relaxed);
                navLearn = 0;
                continue;
            }
            if (navEnabled && (note == navPrev || note == navNext))
            {
                if (m.isNoteOn())
                    (void) seqNav_.push(note == navPrev ? static_cast<std::int8_t>(-1)
                                                        : static_cast<std::int8_t>(1));
                continue; // swallow both the note-on and its note-off
            }
        }
        hostScratch_.addEvent(m, meta.samplePosition);
    }

    // Lit-key feed for the console ("keys light while playing"): a byte write per
    // event, RT-safe. Host MIDI is attributed to the primary manual (a single
    // keyboard plays the Great).
    for (const juce::MidiMessageMetadata meta : hostScratch_)
    {
        const juce::MidiMessage m = meta.getMessage();
        if (m.isNoteOn())
            keys_.set(playDivision_.value, static_cast<core::MidiNote>(m.getNoteNumber()),
                      ui::KeySource::PlayedDirect);
        else if (m.isNoteOff())
            keys_.set(playDivision_.value, static_cast<core::MidiNote>(m.getNoteNumber()),
                      ui::KeySource::Off);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            keys_ = {};
    }

    // On-screen keyboard notes: drained into a PRE-RESERVED scratch buffer (never
    // grow the host MidiBuffer on the audio thread — that can heap-allocate), each
    // lit on ITS OWN division so the clicked keyboard responds. clear() keeps the
    // reserved storage, so addEvent stays allocation-free.
    uiScratch_.clear();
    {
        UiNoteEvent ev;
        while (uiNotes_.pop(ev))
        {
            const int ch = juce::jlimit(1, 16, static_cast<int>(ev.division.value) + 1);
            const juce::MidiMessage m = ev.down
                ? juce::MidiMessage::noteOn(ch, static_cast<int>(ev.note), static_cast<juce::uint8>(100))
                : juce::MidiMessage::noteOff(ch, static_cast<int>(ev.note));
            uiScratch_.addEvent(m, 0);
            keys_.set(ev.division.value, ev.note,
                      ev.down ? ui::KeySource::PlayedDirect : ui::KeySource::Off);
        }
    }

    // Console panic (stop demo / release keys): silence every sounding voice and
    // clear the lit-key display so nothing sticks.
    if (uiPanic_.exchange(false, std::memory_order_relaxed))
    {
        commandBridge_.pushPanic();
        keys_ = {};
    }

    // Single-producer encode of host intent onto the engine command ring, drained
    // by engine_.processBlock() below. Parameters first (cheap when unchanged),
    // then the host and on-screen note streams (both from THIS thread, so the ring
    // still has exactly one producer).
    commandBridge_.pushChangedParameters(parameters_);
    commandBridge_.pushMidi(hostScratch_); // host MIDI minus the swallowed nav keys
    commandBridge_.pushMidi(uiScratch_);
    midi.clear(); // this instrument produces no MIDI output

    // Wrap the host buffer as the JUCE-free AudioBlock — the only audio type that
    // crosses the engine seam — and render. The engine overwrites the buffer.
    core::AudioBlock block(buffer.getArrayOfWritePointers(), numChannels, numFrames);
    engine_.processBlock(block);

    // Publish one consistent frame (levels + lit keys) for the console to poll.
    stateMirror_.publish(engine_.latestMeters(), keys_);

    // Host-facing output trim (see header): a smoothed gain, not synthesis DSP.
    const float gainDb = [this]
    {
        if (std::atomic<float>* p = parameters_.rawParameter(ParameterLayout::kMasterGainDb))
            return p->load(std::memory_order_relaxed);
        return 0.0f;
    }();
    masterGain_.setTargetValue(juce::Decibels::decibelsToGain(gainDb, -60.0f));
    masterGain_.applyGain(buffer, buffer.getNumSamples());

    // Console master trim (Settings panel).
    buffer.applyGain(uiMaster_.load(std::memory_order_relaxed));

    // Safety soft-clip: tanh is ~transparent below ~0.4 but saturates gracefully
    // and hard-bounds the output to +/-1, so the instrument can NEVER blow up the
    // speakers even under heavy polyphony. Belt-and-braces with the energy-
    // normalised registration level (see model::normalizeComposite).
    const int nSamp = buffer.getNumSamples();
    const int nCh   = buffer.getNumChannels();
    float peaks[2] = { 0.0f, 0.0f };
    for (int ch = 0; ch < nCh; ++ch)
    {
        float* d = buffer.getWritePointer(ch);
        float  pk = 0.0f;
        for (int i = 0; i < nSamp; ++i)
        {
            const float v = std::tanh(d[i]);
            d[i] = v;
            const float a = v < 0.0f ? -v : v;
            if (a > pk) pk = a;
        }
        if (ch < 2) peaks[ch] = pk;
    }
    // Publish the REAL output peak so the console VU reflects what is heard
    // (host/physical MIDI included). Mirror to both meters when mono.
    outPeakL_.store(peaks[0], std::memory_order_relaxed);
    outPeakR_.store(nCh > 1 ? peaks[1] : peaks[0], std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Editor.
// ---------------------------------------------------------------------------

juce::AudioProcessorEditor* CaeciliaAudioProcessor::createEditor()
{
    return new CaeciliaEditor(*this);
}

// ---------------------------------------------------------------------------
// Capabilities.
// ---------------------------------------------------------------------------

double CaeciliaAudioProcessor::getTailLengthSeconds() const
{
    // Report reverb decay + pre-delay so the host flushes a long enough tail.
    auto read = [this](const char* id, float fallback)
    {
        if (std::atomic<float>* p = parameters_.rawParameter(id))
            return p->load(std::memory_order_relaxed);
        return fallback;
    };
    const float decay    = read(ParameterLayout::kReverbDecaySec, 2.5f);
    const float preDelay = read(ParameterLayout::kReverbPreDelayMs, 12.0f) * 0.001f;
    return static_cast<double>(decay + preDelay);
}

// ---------------------------------------------------------------------------
// Persistence.
// ---------------------------------------------------------------------------

void CaeciliaAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    parameters_.writeState(destData);
}

void CaeciliaAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (parameters_.readState(data, sizeInBytes))
    {
        // Force the next block to re-send the restored parameter state to the engine.
        commandBridge_.resetChangeTracking();
        updateLatency();
    }
}

// ---------------------------------------------------------------------------
// Internals.
// ---------------------------------------------------------------------------

void CaeciliaAudioProcessor::updateLatency() noexcept
{
    // Plugin delay compensation = engine DSP group delay (oversampling + reverb
    // pre-delay). // TODO(phase0.3): query the bound master reverb / oversampler
    // for its real latency; the pure engine reports it through IReverb.
    setLatencySamples(0);
}

} // namespace caecilia::plugin

// ---------------------------------------------------------------------------
// JUCE plugin entry point: the host instantiates the processor through this.
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new caecilia::plugin::CaeciliaAudioProcessor();
}

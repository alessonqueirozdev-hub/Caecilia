// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/plugin/PluginProcessor.h"

#include "caecilia/registration/FactoryGenerals.h"
#include "caecilia/wind/OrganWind.h"
#include "caecilia/registration/StopSet.h"

#include "caecilia/midi/ChannelToDivisionMap.h"
#include "caecilia/model/Division.h"
#include "caecilia/model/OrganLoader.h"
#include "caecilia/plugin/PluginEditor.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstring>
#include <span>
#include <utility>

namespace caecilia::plugin
{

namespace
{
/// Total simultaneous voices. Each key grabs one composite voice, so this is the
/// polyphony ceiling across every manual and the pedal at once.
/// Voice slots. One voice per (rank, note) needs far more of them: a ten-note
/// chord on a twenty-six-stop Tutti is 260 voices, where the composite design
/// needed ten.
///
/// This costs LESS memory, not more. A composite voice reserved 1024 partials
/// because it held every drawn rank at once; a rank holds ninety-six. 512 x 96 is
/// about half of 96 x 1024.
constexpr std::size_t kPolyphony = 512;

} // namespace

void CaeciliaAudioProcessor::buildInstrument(double sampleRate, std::size_t maxBlockFrames)
{
    synth::VoiceContext ctx;
    // The sounding tuning, which every voice consults at note-on. Without this
    // binding the voices fell back to a hard-coded equal-tempered A=440 inside
    // VoiceContext; with the binding but no way to REPLACE the table, the host's
    // Temperament and Tuning A4 parameters were automatable, saved in the document
    // and still silent. LiveTuning closes the second half: the rebuild happens off
    // the audio thread and arrives as a snapshot the audio thread adopts at a block
    // boundary.
    ctx.tuning  = &liveTuning_;

    // The wind supply, so a voice's partials actually respond to pressure. The
    // voice re-points the coupling at its own chest when it adopts a rank; this is
    // what puts a supply there to point at in the first place.
    ctx.wind    = &wind_;

    ctx.family  = core::TonalFamily::Principal;
    ctx.footage = core::footage::kEight; // composite is already referenced to 8'

    // Reserve for the LARGEST registration, not for the current one. Sizing to the
    // current composite meant every stop change had to rebuild all 96 voices --
    // roughly 2 MB of allocate-and-free, on the message thread, synchronously with
    // the console click -- and, far worse, rebinding the pool afterwards reset it
    // and killed every note that was sounding.
    voices_.clear();
    voicePtrs_.clear();
    voices_.reserve(kPolyphony);
    voicePtrs_.reserve(kPolyphony);

    // Sized for the largest thing a voice can be asked to become, which is one
    // rank. It was 1024 while a voice might have had to become an entire
    // registration -- a figure multiplied by 512 voices for memory, and TOUCHED per
    // block, so a cache figure too.
    //
    // Nothing is seeded here. Every trigger path adopts a rank first and seeding is
    // part of adoption, so a voice always carries its rank's spectrum before it
    // sounds and never reads a starting point.
    for (std::size_t i = 0; i < kPolyphony; ++i)
    {
        auto v = std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
        v->prepare(sampleRate, maxBlockFrames);
        v->setContext(ctx);
        voicePtrs_.push_back(v.get());
        voices_.push_back(std::move(v));
    }

    // One voicing per stop, built once. They do not depend on what is drawn, so
    // drawing and retiring stops never rebuilds any of this -- which is what makes
    // a drawstop click cost a table publication rather than a re-voicing of the
    // entire pool.
    rankVoicings_.clear();
    rankVoicings_.resize(registration::StopSet::kMaskCapacity);
    for (const model::Stop& s : organ_.stops())
        if (s.id().value < rankVoicings_.size())
            rankVoicings_[s.id().value] = model::buildRankVoicing(organ_, s.id());

    publishEngagedRanks();
}

void CaeciliaAudioProcessor::publishEngagedRanks()
{
    core::engine::EngagedRankTable table;

    for (std::size_t i = 0; i < rankVoicings_.size()
                            && table.count < table.ranks.size(); ++i)
    {
        if ((registration_ & (std::uint64_t{1} << i)) == 0)
            continue;
        const synth::RankVoicing& v = rankVoicings_[i];
        if (v.spectrum.partials.empty())
            continue; // a bit set for a stop this instrument does not have
        // What one pipe of this rank draws at middle C, relative to an 8'. A 16'
        // rank's pipes are twice the length of an 8''s and empty the reservoir
        // accordingly; the note's own half of the figure is applied by the engine.
        table.ranks[table.count++] =
            core::engine::EngagedRank{ &v, v.stop, v.division,
                                       wind::rankWindFlow(v.footage), v.chest };
    }

    // The drawn couplers travel WITH the ranks: a key's expansion needs both, and
    // publishing them separately would let the two disagree for a block -- a chord
    // sounding through a coupler whose source ranks had just been retired.
    for (std::size_t i = 0; i < organ_.couplers().size()
                            && table.couplerCount < table.couplers.size(); ++i)
    {
        if ((couplers_ & (std::uint32_t{1} << i)) == 0)
            continue;
        const model::Coupler& c = organ_.couplers()[i];
        table.couplers[table.couplerCount++] = core::engine::EngagedCoupler{
            c.from(), c.to(),
            static_cast<std::int16_t>(c.octaveShiftSemitones()) };
    }

    table.epoch = ++registrationEpoch_;
    engine_.setEngagedRanks(table);
}

CaeciliaAudioProcessor::CaeciliaAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output",
                                                        juce::AudioChannelSet::stereo(),
                                                        true))
    // The organ is compiled in the member-initialiser list, before the parameter
    // mirror, because the mirror's layout is built FROM it: the stop parameters
    // take their names from its stops and their DEFAULTS from its opening plenum.
    // Choosing the plenum afterwards -- which is what the old constructor body did
    // -- meant the host's idea of "default" was silence, and its "reset to
    // default" gave back an instrument with nothing drawn.
    , organ_(model::buildCaeciliaDemoOrgan())
    , playDivision_(model::primaryManual(organ_))
    , parameters_(*this, organ_, model::defaultOpeningRegistration(organ_, playDivision_))
{
    // The parameters now hold the opening plenum as their own value, so the
    // registration is simply read back out of them rather than chosen again here.
    registration_ = parameters_.stopBits();

    // The published parameter set has to be exactly what core/ParameterIds.h pins,
    // because ParameterIdHashTest guards THAT list and a host keys its automation on
    // it. The IDs cannot drift -- ParameterLayout aliases core rather than restating
    // them -- so the only way the two can disagree is create() adding or missing one,
    // which is what this catches.
    jassert(getParameters().size() == static_cast<int>(ParameterLayout::kParameterCount));

    couplers_ = parameters_.couplerBits();

    // The factory pistons, resolved against THIS organ. Built once: a general is a
    // stored registration, and rebuilding one behind the organist's back is the
    // opposite of what a combination memory is for.
    buildDefaultGenerals();

    // The root of the history is what the organ opens with. RegistrationHistory
    // starts empty, and the opening registration is applied as a Restore -- which
    // is deliberately not a recorded move -- so without this the first undo of the
    // session would take an organist to a silent console they had never seen.
    history_.reset(currentRegistrationState());
}

// ---------------------------------------------------------------------------
// Registration defaults and control.
// ---------------------------------------------------------------------------

bool CaeciliaAudioProcessor::isStopEngaged(core::StopId stop) const noexcept
{
    return stop.value < registration::StopSet::kMaskCapacity
        && (registration_ & (std::uint64_t{1} << stop.value)) != 0;
}

void CaeciliaAudioProcessor::toggleStop(core::StopId stop)
{
    if (stop.value >= registration::StopSet::kMaskCapacity)
        return;
    applyRegistration(registration_ ^ (std::uint64_t{1} << stop.value),
                      RegistrationOrigin::Console);
}

void CaeciliaAudioProcessor::setDrawnStops(std::uint64_t bits)
{
    applyRegistration(bits, RegistrationOrigin::Console);
}

void CaeciliaAudioProcessor::republishTuning(int choice, float a4Hz)
{
    liveTuning_.publish(tuning::makeSnapshot(
        ParameterLayout::temperamentFromChoice(choice),
        static_cast<double>(a4Hz)));
}

void CaeciliaAudioProcessor::toggleCoupler(std::size_t index)
{
    if (index >= ParameterLayout::kMaxCouplerParameters)
        return;
    applyCouplers(couplers_ ^ (std::uint32_t{ 1 } << index),
                  RegistrationOrigin::Console);
}

void CaeciliaAudioProcessor::applyCouplers(std::uint32_t next, RegistrationOrigin origin)
{
    // Idempotence first, exactly as applyRegistration: a console click writes the
    // parameters, the audio thread's per-block diff sees them move and calls back
    // with the same set, and that second pass stops here.
    if (next == couplers_)
        return;

    couplers_ = next;

    if (origin != RegistrationOrigin::HostParameters)
        parameters_.writeCouplerBits(couplers_);

    // Same rule as the stops, and the same history: coupling the Récit to the
    // Grand-Orgue is a registration change like any other, and an undo that took
    // back the stops but left the couplers would give back a sound the organist
    // never had.
    if (origin == RegistrationOrigin::Console)
        recordRegistration(registration::RegistrationCommand::Kind::ToggleCoupler);

    if (getSampleRate() <= 0.0)
        return; // not prepared yet; prepareToPlay publishes from couplers_

    // Drawing a coupler under a held chord is the same gesture as drawing a stop,
    // and the engine reconciles it the same way -- the keys already down start
    // sounding the borrowed ranks, and the ranks that were already sounding are not
    // touched.
    publishEngagedRanks();
}

void CaeciliaAudioProcessor::setUiTremulant(bool on)
{
    // Through the PARAMETER, exactly as the master EQ's enable goes. The console
    // pushing straight to the engine would leave the host's own Tremulant control
    // showing something the instrument was not doing.
    if (auto* p = parameters_.apvts().getParameter(ParameterLayout::kTremulantOn))
        p->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

// ---------------------------------------------------------------------------
// MIDI learn.
//
// Binding a physical control to a drawstop or a piston. An organist with a
// stop-tab console or a rank of toe studs has one instrument in front of them and
// another on the screen, and this is what makes them the same instrument.
//
// The split of work across the two threads is the whole design. The audio thread
// has two decisions -- swallow this event, and mention it -- and both need only
// one bit ("is anything bound to this control"), so that is all it is given. The
// exact match, the selector, the registration change and the table edit all happen
// on the message thread, which already owns the map and is allowed to allocate.
// ---------------------------------------------------------------------------

namespace
{
/// Translate a juce::MidiMessage into the core-native event at the module seam.
///
/// This is the seam the midi module's README describes and nothing crossed: every
/// MIDI decision in the plugin was made on juce types. It crosses here now for the
/// learn table, which is core code and JUCE-free, and the note path still does not
/// -- so this is a start on one implementation rather than two, not the end of it.
[[nodiscard]] caecilia::midi::MidiEvent toMidiEvent(const juce::MidiMessage& m) noexcept
{
    namespace midi = caecilia::midi;

    midi::MidiEvent ev;
    ev.channel = static_cast<midi::MidiChannel>(juce::jlimit(1, 16, m.getChannel()) - 1);

    if (m.isNoteOn())
    {
        ev.type  = midi::MidiMessageType::NoteOn;
        ev.data1 = static_cast<std::uint8_t>(m.getNoteNumber());
        ev.data2 = m.getVelocity();
    }
    else if (m.isNoteOff())
    {
        ev.type  = midi::MidiMessageType::NoteOff;
        ev.data1 = static_cast<std::uint8_t>(m.getNoteNumber());
    }
    else if (m.isController())
    {
        ev.type  = midi::MidiMessageType::ControlChange;
        ev.data1 = static_cast<std::uint8_t>(m.getControllerNumber());
        ev.data2 = static_cast<std::uint8_t>(m.getControllerValue());
    }
    else if (m.isProgramChange())
    {
        ev.type  = midi::MidiMessageType::ProgramChange;
        ev.data1 = static_cast<std::uint8_t>(m.getProgramChangeNumber());
    }
    return ev; // anything else stays MidiMessageType::Other
}

} // namespace

void CaeciliaAudioProcessor::armMidiLearn(const midi::RegistrationCommandTemplate& target)
{
    midiLearn_.arm(target);
    midiLearnArmed_.store(true, std::memory_order_relaxed);
}

void CaeciliaAudioProcessor::armMidiLearnStop(core::StopId stop)
{
    // `id:<n>` rather than a name: every other surface in this instrument names a
    // stop by its id, and a name substring is ambiguous by construction -- a real
    // organ has the same Trompette 8 on two divisions.
    armMidiLearn(midi::RegistrationCommandTemplate::toggle(
        "id:" + std::to_string(static_cast<int>(stop.value))));
}

void CaeciliaAudioProcessor::armMidiLearnGeneral(std::size_t index)
{
    if (index >= kNumGenerals)
        return;
    armMidiLearn(midi::RegistrationCommandTemplate::recallGeneral(
        static_cast<std::uint16_t>(index)));
}

void CaeciliaAudioProcessor::cancelMidiLearn()
{
    midiLearn_.cancel();
    midiLearnArmed_.store(false, std::memory_order_relaxed);
}

void CaeciliaAudioProcessor::clearMidiBindings()
{
    midiMap_.clearBindings();
    publishBoundControls();
}

void CaeciliaAudioProcessor::clearMidiBindingFor(const midi::RegistrationCommandTemplate& target)
{
    // Backwards, because removing compacts the table and a forward walk would skip
    // the entry that slid into the hole.
    for (std::size_t i = midiMap_.bindingCount(); i-- > 0;)
        if (midiMap_.bindingAt(i).command == target)
            midiMap_.removeBindingAt(i);
    publishBoundControls();
}

void CaeciliaAudioProcessor::clearMidiBindingForStop(core::StopId stop)
{
    clearMidiBindingFor(midi::RegistrationCommandTemplate::toggle(
        "id:" + std::to_string(static_cast<int>(stop.value))));
}

void CaeciliaAudioProcessor::clearMidiBindingForGeneral(std::size_t index)
{
    if (index < kNumGenerals)
        clearMidiBindingFor(midi::RegistrationCommandTemplate::recallGeneral(
            static_cast<std::uint16_t>(index)));
}

void CaeciliaAudioProcessor::publishBoundControls()
{
    // Rebuilt from scratch rather than patched, because a binding edit is rare and
    // a bitset of 512 bytes is cheaper to fill than to reason about incrementally.
    //
    // The published copy carries the audio thread's pending note-offs with it,
    // which is why adopt() does not clear them: a key that is DOWN when the
    // organist rebinds something still owes a note-off.
    midi::LearnedControls next = boundControls_.read();
    next.adopt(midiMap_);
    boundControls_.write(next);
}

void CaeciliaAudioProcessor::applyMidiBinding(const midi::MidiLearnBinding& binding)
{
    const midi::RegistrationCommandTemplate& c = binding.command;
    switch (c.verb)
    {
        case midi::RegistrationVerb::Toggle:
        case midi::RegistrationVerb::Engage:
        case midi::RegistrationVerb::Disengage:
        {
            const std::uint64_t mask =
                registration::resolveSelectorMask(organ_, c.selector.view());
            if (mask == 0)
                return; // an expression that matches nothing draws nothing

            const std::uint64_t next =
                c.verb == midi::RegistrationVerb::Toggle    ? (registration_ ^ mask)
              : c.verb == midi::RegistrationVerb::Engage    ? (registration_ | mask)
                                                            : (registration_ & ~mask);
            applyRegistration(next, RegistrationOrigin::Console);
            break;
        }

        case midi::RegistrationVerb::RecallGeneral:
            recallGeneral(static_cast<std::size_t>(c.index));
            break;

        case midi::RegistrationVerb::ClearAll:
            applyRegistration(0, RegistrationOrigin::Console);
            break;

        default:
            // The verb set is wider than what can be bound today -- sequencer
            // steps, divisionals, the crescendo. Nothing arms them, so nothing
            // reaches here; when something does, it lands as a case rather than as
            // a silent no-op.
            break;
    }
}

void CaeciliaAudioProcessor::handleMidiActions()
{
    std::uint32_t packed = 0;
    while (midiActions_.pop(packed))
    {
        const midi::MidiEvent ev = midi::MidiEvent::unpack(packed);

        if (midiLearn_.isArmed())
        {
            if (midiLearn_.observe(ev))
            {
                // One control, one action: re-binding a control that already had
                // one replaces it (installBinding matches on the source), and the
                // action's own previous binding goes too, so a stop cannot end up
                // answering to two tabs the organist has forgotten about.
                const midi::MidiLearnBinding captured = midiLearn_.takeCaptured();
                clearMidiBindingFor(captured.command);
                (void) midiMap_.installBinding(captured);
                midiLearnArmed_.store(false, std::memory_order_relaxed);
                publishBoundControls();
            }
            continue;
        }

        if (const midi::MidiLearnBinding* b = midiMap_.findBinding(ev))
            if (b->shouldFire(ev))
                applyMidiBinding(*b);
    }
}

void CaeciliaAudioProcessor::handleAsyncUpdate()
{
    // Message thread, and TWO producers share this one dispatch: the audio thread's
    // per-block parameter diff, and a MIDI program change. Each carries its own
    // "there is something here" flag and claims it, so neither consumes the other's
    // work -- and neither is lost if both arrive in the same block.
    if (pendingHostRegistrationValid_.exchange(false, std::memory_order_relaxed))
        // HostParameters origin means it will NOT be written back to the
        // parameters, which is what stops a redundant automation point per change.
        applyRegistration(pendingHostRegistration_.load(std::memory_order_relaxed),
                          RegistrationOrigin::HostParameters);

    // A temperament the audio thread noticed. Rebuilding is 128 exp2 calls and an
    // allocation-free table copy, both of which belong here rather than there.
    if (pendingTuningValid_.exchange(false, std::memory_order_relaxed))
    {
        const std::uint64_t packed = pendingTuning_.load(std::memory_order_relaxed);
        const auto  choice = static_cast<int>(static_cast<std::uint32_t>(packed >> 32));
        const auto  bits   = static_cast<std::uint32_t>(packed & 0xFFFFFFFFu);
        float a4 = 440.0f;
        std::memcpy(&a4, &bits, sizeof(a4));
        republishTuning(choice, a4);
    }

    if (pendingCouplersValid_.exchange(false, std::memory_order_relaxed))
        applyCouplers(pendingCouplers_.load(std::memory_order_relaxed),
                      RegistrationOrigin::HostParameters);

    // Learned controls, before the program change below and after the parameter
    // diff above, for the same reason the piston goes last: a bound tab is an
    // explicit gesture and should overwrite what automation was doing, not be
    // overwritten by it.
    handleMidiActions();

    // The piston goes last. It is an explicit gesture, and any parameter diff it
    // arrived alongside describes the registration it means to replace.
    const int program = pendingProgram_.exchange(-1, std::memory_order_relaxed);
    if (program >= 0)
        recallGeneral(static_cast<std::size_t>(program));
}

CaeciliaAudioProcessor::~CaeciliaAudioProcessor()
{
    // Before any member dies. See the declaration.
    cancelPendingUpdate();
}

std::uint64_t CaeciliaAudioProcessor::generalMask(std::size_t index) const noexcept
{
    return index < kNumGenerals && generalsSet_.test(index) ? generals_[index] : 0;
}

bool CaeciliaAudioProcessor::generalIsSet(std::size_t index) const noexcept
{
    return index < kNumGenerals && generalsSet_.test(index);
}

void CaeciliaAudioProcessor::captureGeneral(std::size_t index)
{
    if (index >= kNumGenerals)
        return;
    generals_[index] = registration_;
    generalsSet_.set(index);
}

void CaeciliaAudioProcessor::recallGeneral(std::size_t index)
{
    if (index >= kNumGenerals || ! generalsSet_.test(index))
        return;
    applyRegistration(generals_[index], RegistrationOrigin::Console);
    lastGeneral_.store(static_cast<int>(index), std::memory_order_relaxed);
}

void CaeciliaAudioProcessor::clearGeneral(std::size_t index)
{
    if (index >= kNumGenerals)
        return;
    generals_[index] = 0;
    generalsSet_.reset(index);
}

int CaeciliaAudioProcessor::consumeLastGeneral() noexcept
{
    return lastGeneral_.exchange(-1, std::memory_order_relaxed);
}

void CaeciliaAudioProcessor::buildDefaultGenerals()
{
    // The table, the grammar and the cumulative rule all live in `registration`,
    // where a test can reach them. This is the plugin binding them to the loaded
    // organ, and nothing more.
    const std::size_t written = registration::resolveFactoryGenerals(organ_, generals_);
    (void) written;

    for (std::size_t i = 0; i < kNumGenerals; ++i)
        if (generals_[i] != 0)
            generalsSet_.set(i);
}

void CaeciliaAudioProcessor::applyRegistration(std::uint64_t next, RegistrationOrigin origin)
{
    // Idempotence is the loop-breaker, and it is deliberately the first thing
    // here. A console click writes the host parameters; the audio thread's
    // per-block diff then sees them move and calls back with the SAME set; that
    // second pass stops on this line. No flag to get out of sync, and no window in
    // which a race could sustain the round trip.
    if (next == registration_)
        return;

    registration_ = next;

    // A change that CAME from the parameters must not be written back to them.
    // Not for correctness -- the guard above already handles that -- but because
    // it would put a redundant point into every automation lane it touched.
    if (origin != RegistrationOrigin::HostParameters)
        parameters_.writeStopBits(registration_);

    // Recorded AFTER the change and BEFORE the not-prepared return, because the
    // history is message-thread bookkeeping and has nothing to do with whether
    // audio is running: an organist can set a registration before a host has
    // prepared us, and taking that back must still work. The idempotence guard at
    // the top of this function means an unchanged write never reaches here, so the
    // history cannot fill with nodes that all say the same thing.
    if (origin == RegistrationOrigin::Console)
        recordRegistration(registration::RegistrationCommand::Kind::ToggleStop);

    if (getSampleRate() <= 0.0)
        return; // not prepared yet; prepareToPlay builds from registration_

    // A drawstop click is now a table publication, not a re-voicing of the pool:
    // the ranks that were already drawn keep their voices untouched, so held notes
    // are not merely un-clicked but bit-identical.
    publishEngagedRanks();
}

registration::RegistrationState CaeciliaAudioProcessor::currentRegistrationState() const
{
    registration::RegistrationState state;
    state.stops = registration::StopSet::fromMask(registration_);

    for (const model::Coupler& c : organ_.couplers())
        if (c.id().value < 32 && (couplers_ & (std::uint32_t{ 1 } << c.id().value)) != 0)
            state.couplers.push_back(c.id());

    return state;
}

void CaeciliaAudioProcessor::recordRegistration(registration::RegistrationCommand::Kind kind)
{
    registration::RegistrationCommand command;
    command.kind = kind;
    history_.record(command, currentRegistrationState());
}

void CaeciliaAudioProcessor::applyRegistrationState(const registration::RegistrationState& state)
{
    std::uint32_t couplerBits = 0;
    for (const model::CouplerId id : state.engagedCouplers())
        if (id.value < 32)
            couplerBits |= (std::uint32_t{ 1 } << id.value);

    // Restore, not Console: an undo is a move BACK through the history, not a new
    // move to be recorded at the end of it. Recording here would put undo out of
    // reach -- every step back would add a step forward.
    applyRegistration(state.engagedStops().toMask(), RegistrationOrigin::Restore);
    applyCouplers(couplerBits, RegistrationOrigin::Restore);
}

void CaeciliaAudioProcessor::undoRegistration()
{
    if (history_.canUndo())
        applyRegistrationState(history_.undo());
}

void CaeciliaAudioProcessor::redoRegistration()
{
    if (history_.canRedo())
        applyRegistrationState(history_.redo());
}

void CaeciliaAudioProcessor::setUiRegistration(const std::vector<model::RegistrationRank>& ranks)
{
    // The console still speaks family+footage. Resolve it onto real stops of this
    // instrument and go through the one writer, so the host parameters, the
    // console and the sounding voices cannot disagree.
    //
    // A rank the instrument does not have resolves to nothing rather than to a
    // substitute -- see model::resolveRanksToStops. Silence on one rank is better
    // than a registration that does not exist sounding like one that does.
    const auto ids = model::resolveRanksToStops(organ_, ranks);
    std::uint64_t bits = 0;
    for (const core::StopId id : ids)
        if (id.value < registration::StopSet::kMaskCapacity)
            bits |= (std::uint64_t{1} << id.value);

    applyRegistration(bits, RegistrationOrigin::Console);
}

bool CaeciliaAudioProcessor::uiEqEnabled() const noexcept
{
    const std::atomic<float>* p = parameters_.rawParameter(ParameterLayout::kEqOn);
    return p == nullptr || p->load(std::memory_order_relaxed) >= 0.5f;
}

float CaeciliaAudioProcessor::uiEqGain(int band) const noexcept
{
    if (band < 0 || band >= static_cast<int>(dsp::MasterEq::kBands))
        return 0.0f;
    const std::atomic<float>* p =
        parameters_.rawParameter(ParameterLayout::kEqBandIds[static_cast<std::size_t>(band)]);
    return p != nullptr ? p->load(std::memory_order_relaxed) : 0.0f;
}

void CaeciliaAudioProcessor::applyEqParameters() noexcept
{
    for (std::size_t b = 0; b < dsp::MasterEq::kBands; ++b)
        if (std::atomic<float>* p = eqBandParam_[b])
            masterEq_.setBandGain(b, p->load(std::memory_order_relaxed));

    if (eqOnParam_ != nullptr)
        masterEq_.setEnabled(eqOnParam_->load(std::memory_order_relaxed) >= 0.5f);
}

void CaeciliaAudioProcessor::publishConsoleReverb()
{
    const auto preset = static_cast<dsp::ReverbPreset>(reverbSpace_);
    core::ReverbParams params = dsp::FdnReverb::presetParams(preset);
    params.mix = reverbMix_;

    // Published, not applied. See the member's comment: applying it here took the
    // wrapper's callback lock from the message thread and bypassed the command
    // ring. The audio thread reads this on its next block and sends it on.
    uiReverb_.write(params);

    // Seed the tail answer too, so a host asking between the click and the next
    // audio block gets the new space rather than the old one.
    tailDecaySec_.store(params.decaySec, std::memory_order_relaxed);
    tailPreDelayMs_.store(params.preDelayMs, std::memory_order_relaxed);
}

void CaeciliaAudioProcessor::setUiReverb(int spaceIndex, float mix)
{
    reverbSpace_ = juce::jlimit(0, 4, spaceIndex);
    reverbMix_   = juce::jlimit(0.0f, 1.0f, mix);
    publishConsoleReverb();
}

void CaeciliaAudioProcessor::setUiEqGain(int band, float gainDb)
{
    if (band < 0 || band >= static_cast<int>(dsp::MasterEq::kBands))
        return;

    // Through the parameter, not into the EQ. The console is now one more thing
    // that moves a host parameter -- so the move is automatable, undoable, saved
    // by the host, and visible in its generic editor, none of which was true when
    // this reached past the parameter and wrote the DSP object directly.
    if (auto* p = parameters_.apvts().getParameter(
            ParameterLayout::kEqBandIds[static_cast<std::size_t>(band)]))
        p->setValueNotifyingHost(p->convertTo0to1(gainDb));
}

void CaeciliaAudioProcessor::setUiEqGesture(int band, bool begin)
{
    if (band < 0 || band >= static_cast<int>(dsp::MasterEq::kBands))
        return;
    if (auto* p = parameters_.apvts().getParameter(
            ParameterLayout::kEqBandIds[static_cast<std::size_t>(band)]))
    {
        if (begin)
            p->beginChangeGesture();
        else
            p->endChangeGesture();
    }
}

void CaeciliaAudioProcessor::setUiExpression(int division, float position)
{
    if (division < 0 || division > 255)
        return;
    // Through the command ring like every other message->audio change, and stamped
    // at the top of the block: a console drag has no meaningful timestamp of its
    // own. The engine glides to it, so a coarse arrival is not a coarse sound.
    (void) uiExpression_.push(UiExpressionEvent{
        core::DivisionId{ static_cast<std::uint16_t>(division) },
        juce::jlimit(0.0f, 1.0f, position) });
}

void CaeciliaAudioProcessor::setUiEqEnabled(bool on)
{
    if (auto* p = parameters_.apvts().getParameter(ParameterLayout::kEqOn))
        p->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

void CaeciliaAudioProcessor::uiNote(core::DivisionId division, core::MidiNote note, bool down)
{
    // Lock-free hand-off to the audio thread; drop if the (large) ring is full.
    // The clicked division travels with the event so the right keyboard sounds
    // and lights (not always the primary manual).
    (void) uiNotes_.push(UiNoteEvent{ division, note, down });
}

// ---------------------------------------------------------------------------
// Loading an organ.
// ---------------------------------------------------------------------------

void CaeciliaAudioProcessor::adoptOrgan(model::Organ&& organ, juce::String path)
{
    organ_     = std::move(organ);
    organPath_ = std::move(path);

    // Whatever the project could not find, it is no longer what is playing here:
    // the user has an organ, and it is this one.
    unresolvedOrgan_.clear();

    // Everything keyed to the OLD organ has to go. A stop id, a coupler index and
    // a combination are all positions in a table that has just been replaced, and
    // carrying one across means drawing whatever now happens to sit in that slot.
    playDivision_ = model::primaryManual(organ_);
    couplers_     = 0;
    generals_.fill(0);
    generalsSet_.reset();
    registration::resolveFactoryGenerals(organ_, generals_);
    for (std::size_t i = 0; i < generals_.size(); ++i)
        if (generals_[i] != 0)
            generalsSet_.set(i);

    // The opening registration of the NEW organ, written through to the host
    // parameters so the two agree from the first block. Origin::Restore because
    // this is not a gesture the user made on a control.
    std::uint64_t opening = 0;
    for (const core::StopId id : model::defaultOpeningRegistration(organ_, playDivision_))
        if (id.value < 64)
            opening |= (std::uint64_t{ 1 } << id.value);

    registration_ = 0;
    applyRegistration(opening, RegistrationOrigin::Restore);
    parameters_.writeCouplerBits(0);

    // And the history starts again here. Every node in it holds stop ids of the
    // OLD organ, and an id is a position in a table that has just been replaced --
    // so undoing across an organ change would not give back the sound the organist
    // had; it would draw whatever now happens to sit in those slots.
    history_.reset(currentRegistrationState());

    // And then everything prepareToPlay builds, because a different organ has a
    // different chest count, a different voice pool, a different wind system and a
    // different keyboard map. Skipped when the host has not prepared us yet -- it
    // will, and it will do this itself.
    if (getSampleRate() > 0.0)
        prepareToPlay(getSampleRate(), getBlockSize());
}

model::LoadDiagnostics CaeciliaAudioProcessor::loadOrganDocument(const juce::String& document,
                                                                 const juce::String& sourceName)
{
    // Parse and compile FIRST, and only then touch the instrument. A document that
    // does not load leaves the organ that was playing exactly where it was: a
    // half-loaded organ is worse than the one you had, and an organist who mistypes
    // a filename should not lose the instrument they were using.
    model::CompileResult result =
        model::OrganLoader::load(document.toStdString(), model::OrganFileFormat::Auto,
                                 sourceName.toStdString());
    if (! result.ok())
        return result.diagnostics;

    // Audio off for the swap. This rebuilds the voice pool and every buffer in the
    // engine, so it cannot happen underneath a render.
    suspendProcessing(true);
    adoptOrgan(std::move(*result.organ), sourceName);
    suspendProcessing(false);

    return result.diagnostics;
}

model::LoadDiagnostics CaeciliaAudioProcessor::loadOrganFile(const juce::File& file)
{
    model::LoadDiagnostics diagnostics;

    if (! file.existsAsFile())
    {
        diagnostics.error("No such file.", file.getFullPathName().toStdString());
        return diagnostics;
    }

    const juce::String text = file.loadFileAsString();
    if (text.isEmpty())
    {
        diagnostics.error("The file is empty or could not be read.",
                          file.getFullPathName().toStdString());
        return diagnostics;
    }

    return loadOrganDocument(text, file.getFullPathName());
}

void CaeciliaAudioProcessor::loadBuiltInOrgan()
{
    suspendProcessing(true);
    adoptOrgan(model::buildCaeciliaDemoOrgan(), {});
    suspendProcessing(false);
}

// ---------------------------------------------------------------------------
// Lifecycle (off-thread). prepareToPlay is the ONLY place allocation happens.
// ---------------------------------------------------------------------------

void CaeciliaAudioProcessor::prepareToPlay(double sampleRate, int maxBlockSamples)
{
    const auto frames   = static_cast<std::size_t>(juce::jmax(0, maxBlockSamples));
    const auto channels = static_cast<std::size_t>(juce::jmax(1, getTotalNumOutputChannels()));

    // The single allocation point: size every RT buffer from the host contract.
    //
    // The wind, before the engine is given a pointer to it. Compiled from the
    // organ's own chests, so the Récit's tremulant belongs to the Récit and a rank
    // reads the pressure of the chest that actually feeds it.
    //
    // This is what makes the wind model audible at all. Every piece of it was
    // implemented and unit-tested, and setWindSupply had no caller anywhere in the
    // tree -- so RenderContext::wind was null, every partial's wind coupling read a
    // deviation of exactly zero, and the tremulant had nothing to reach.
    wind_.prepare(sampleRate, frames);
    wind_.configure(wind::configFromOrgan(organ_));
    wind_.reset();

    // The organ's REAL chest count, not one. The scheduler already routes each
    // voice to its chest's bus through IWindSupply::chestForPipe; asking for a
    // single chest meant every voice of the instrument landed on bus 0 whatever
    // fed it, so that routing had nowhere to route to and anything per-chest --
    // metering, and the swell box below -- was inert by construction.
    const auto chestCount = std::max<std::size_t>(organ_.windchests().size(), 1);
    engine_.prepare(sampleRate, frames, channels, chestCount);
    engine_.setWindSupply(&wind_);

    // Let the engine size its own CPU budget from how long its blocks take.
    //
    // Without this the budget stayed at its default of one unit per voice slot,
    // which is more than a completely full pool can ever demand -- so the deadline
    // governor could not fire even in principle and the scheduler's promise that a
    // worst-case tutti thins rather than xruns was decorative. A constant would be
    // no better: a cost unit is a voice's relative weight, so how many fit in a
    // block is a fact about the MACHINE.
    //
    // The ceiling is the point past which nothing would be shed anyway, so on a
    // machine that copes the governor sits at it and never touches a note.
    {
        core::engine::CpuGovernor::Config cfg;
        cfg.ceilingUnits = static_cast<float>(core::engine::kMaxVoices);
        engine_.enableCpuGovernor(cfg);
    }

    // Which chests sit inside a swell box, and whose shoe closes them. Enclosure is
    // a property of the division and the buses are per chest, so the mapping has to
    // be stated -- and on a real instrument it is unambiguous, because the box
    // encloses the chest.
    {
        std::vector<core::engine::ChestEnclosure> enclosed;
        for (const model::Division& d : organ_.divisions())
        {
            if (! d.isEnclosed())
                continue;
            // A division may draw from more than one chest, and the box encloses
            // all of them -- so its shoe closes each.
            for (const core::WindchestId chest : d.windchests())
                enclosed.push_back(core::engine::ChestEnclosure{ chest, d.id() });
        }
        engine_.setEnclosedChests(enclosed);
    }

    // Which chests the tremulant parameter actually addresses.
    {
        std::vector<core::WindchestId> shaken;
        for (const model::Windchest& chest : organ_.windchests())
            if (chest.hasTremulant)
                shaken.push_back(chest.id);
        commandBridge_.setTremulantChests(shaken);
    }

    // Bind the single-producer command path and the metering read path. Route
    // MIDI note-ons to the primary manual so they light the right keyboard.
    commandBridge_.connect(engine_.commandQueue());
    commandBridge_.setDefaultDivision(playDivision_);

    // Console convention: channel 1 plays the primary manual (what a single
    // keyboard sends), and channels 2 and 3 reach the other divisions, so a
    // multi-manual controller or a sequencer can address the whole instrument.
    // Anything unmapped still lands on the primary manual.
    {
        caecilia::midi::ChannelToDivisionMap map;

        // Give every mapped channel the compass of the division it plays, so a
        // note outside the instrument is dropped instead of sounding and taking a
        // voice with it.
        auto compassOf = [this](core::DivisionId id) {
            for (const model::Division& d : organ_.divisions())
                if (d.id() == id)
                    return std::pair<core::MidiNote, core::MidiNote>{ d.lowNote(), d.highNote() };
            // Explicitly typed: braced ints narrow to MidiNote and the fallback is
            // the only place in this lambda where they are not already MidiNote.
            return std::pair<core::MidiNote, core::MidiNote>{ core::MidiNote{ 0 },
                                                              core::MidiNote{ 127 } };
        };

        map.mapChannel(0, playDivision_);
        {
            const auto c = compassOf(playDivision_);
            map.setKeyRange(0, c.first, c.second);
        }

        std::uint8_t next = 1;
        for (const model::Division& d : organ_.divisions())
        {
            if (d.id() == playDivision_ || next >= 16)
                continue;
            map.mapChannel(next, d.id());
            map.setKeyRange(next, d.lowNote(), d.highNote());
            ++next;
        }
        commandBridge_.setChannelMap(map);
    }

    commandBridge_.resetChangeTracking();
    commandBridge_.setSampleRate(sampleRate);
    meterBridge_.connect(engine_);

    // Equal-tempered A=440 tuning table (historical temperaments swap in here via
    // the temperament parameter in a later phase). Bound read-only into the engine.
    engine_.setTuning(&liveTuning_);

    // A fresh prepare has not sent the tuning yet, so the next block must.
    sentTemperament_ = -1;
    sentTuningA4_    = 0.0f;

    // Build the drawn registration's voices and bind them into the engine.
    buildInstrument(sampleRate, frames);
    engine_.bindVoices(voicePtrs_.data(), voicePtrs_.size());

    // Master reverb: a large-hall preset by default; the parameter bridge then
    // syncs it to the APVTS reverb controls on the first processed block.
    reverb_.prepare(sampleRate, frames, channels);
    reverb_.setPreset(dsp::ReverbPreset::Hall);
    engine_.setMasterReverb(&reverb_);

    // Hand the console's current space to the audio thread so the first block
    // sends it. Without this the bridge's freshly reset baseline is a
    // default-constructed set, and its first message would overwrite the space's
    // bass bloom -- which no host parameter covers -- with the default.
    publishConsoleReverb();

    // Master tone-voicing EQ (post-reverb) and the brick-wall limiter (post-trim)
    // — the professional end of the chain. EQ defaults to the pipe-organ voicing;
    // the limiter holds the bus at -3 dBFS. The extra headroom (vs -1.5) leaves room
    // for the inter-sample overshoot that Windows' SHARED-mode mixer/resampler adds
    // downstream, which is what made the Standalone distort until users switched to
    // WASAPI Exclusive; -3 dBFS keeps it clean in shared mode too, at a hair less loudness.
    // Resolve the EQ parameter pointers once, then push their values in BEFORE
    // preparing: prepare() snaps the 30 ms glide onto the current targets, so a
    // restored session starts on its own curve rather than gliding up from the
    // factory one. Safe only because prepare() installs the band shapes
    // unconditionally -- otherwise the snap would land on five 1 kHz sections.
    for (std::size_t b = 0; b < dsp::MasterEq::kBands; ++b)
        eqBandParam_[b] = parameters_.rawParameter(ParameterLayout::kEqBandIds[b]);
    eqOnParam_ = parameters_.rawParameter(ParameterLayout::kEqOn);
    applyEqParameters();

    masterEq_.prepare(sampleRate, frames, channels);
    limiter_.prepare(sampleRate, frames, channels);
    limiter_.setParams(/*ceilingDb*/ -3.0f, /*lookAheadMs*/ 2.5f, /*holdMs*/ 400.0f, /*releaseMs*/ 600.0f);

    // On-screen keyboard -> audio thread: clear any stale queued notes and
    // pre-reserve the scratch MIDI buffer so merging UI notes never allocates on
    // the audio thread (ample for the 512-slot event ring).
    { UiNoteEvent drain; while (uiNotes_.pop(drain)) {} }
    uiScratch_.ensureSize(8192);
    hostScratch_.ensureSize(8192); // host MIDI minus swallowed page-turn keys
    keys_ = {};

    navSwallowed_.reset();

    masterGain_.reset(sampleRate, 0.02); // 20 ms output-trim ramp
    masterGain_.setCurrentAndTargetValue(1.0f);
    polyGain_.reset(sampleRate, 0.05);   // 50 ms polyphony-compensation glide
    polyGain_.setCurrentAndTargetValue(1.0f);

    // The console's Gain and Volume were read straight from their atomics into a
    // whole-block gain, so every block boundary was a step: zipper noise while
    // dragging a knob and a click on any large jump. 30 ms is short enough to feel
    // immediate and long enough to be inaudible.
    uiMasterSmooth_.reset(sampleRate, 0.03);
    uiMasterSmooth_.setCurrentAndTargetValue(uiMaster_.load(std::memory_order_relaxed));
    uiVolumeSmooth_.reset(sampleRate, 0.03);
    uiVolumeSmooth_.setCurrentAndTargetValue(uiVolume_.load(std::memory_order_relaxed));

    updateLatency();
}

void CaeciliaAudioProcessor::releaseResources()
{
    // Engine buffers persist until the next prepare(); nothing to free here. The
    // no-allocation contract means there is no per-run scratch to release.
}

void CaeciliaAudioProcessor::reset()
{
    // Hosts call reset() between takes and expect no state to survive it. Without
    // this the reverb tail from the previous run bled into the first block after
    // the transport restarted, and an offline render was not reproducible between
    // runs. juce::CriticalSection is recursive, so taking the callback lock is
    // safe even when a host calls this from the audio thread.
    const juce::ScopedLock sl(getCallbackLock());
    engine_.reset();  // synchronous: voices stop dead, queued commands are dropped
    reverb_.reset();
    masterEq_.reset();
    limiter_.reset();
    keys_ = {};
    navSwallowed_.reset();
    // The note-offs the swallowed keys owed are never coming after a reset.
    boundSnapshot_.reset();
    uiMasterSmooth_.setCurrentAndTargetValue(uiMaster_.load(std::memory_order_relaxed));
    uiVolumeSmooth_.setCurrentAndTargetValue(uiVolume_.load(std::memory_order_relaxed));
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

    // Take the SMALLER of the two. The bus layout is what we asked for; the buffer
    // is what we were given, and reading past it would be out of bounds.
    const auto numChannels = static_cast<std::size_t>(
        std::min(getTotalNumOutputChannels(), buffer.getNumChannels()));
    const auto numFrames   = static_cast<std::size_t>(buffer.getNumSamples());

    // Adopt any temperament the message thread published, BEFORE a single note-on
    // is drained -- a block must sound in one tuning throughout, not change pitch
    // partway because a rebuild landed between two commands. Costs one relaxed load
    // on every block where nothing changed.
    liveTuning_.adoptPending();

    // --- Sequencer page-turn: swallow the configured nav keys and turn them into
    // Previous/Next intents (delivered to the console by the editor's timer). The
    // remaining host MIDI is copied into a pre-reserved scratch buffer so the nav
    // keys never sound a pipe and never light the manual.
    const bool navEnabled = seqNavEnabled_.load(std::memory_order_relaxed);
    const int  navPrev     = seqPrevNote_.load(std::memory_order_relaxed);
    const int  navNext     = seqNextNote_.load(std::memory_order_relaxed);
    int        navLearn    = seqLearn_.load(std::memory_order_relaxed);

    // The learn table's view for this block, read once. `hasFresh` is the whole
    // cost when nothing has changed, which is every block but the handful after an
    // organist binds something.
    const bool learnArmed = midiLearnArmed_.load(std::memory_order_relaxed);
    if (boundControls_.hasFresh())
        boundSnapshot_ = boundControls_.read();

    hostScratch_.clear();
    for (const juce::MidiMessageMetadata meta : midi)
    {
        const juce::MidiMessage m = meta.getMessage();

        // --- learned controls ------------------------------------------------
        //
        // The whole decision is midi::LearnedControls', which is core code with a
        // test suite; here it is one call and a switch. Everything that follows
        // from a Capture or a Fire -- the exact match, the selector, the
        // registration change -- is the message thread's, reached through a ring
        // and one async wake.
        {
            const midi::MidiEvent ev = toMidiEvent(m);
            const auto verdict = boundSnapshot_.inspect(ev, learnArmed);

            if (verdict != midi::LearnedControls::Verdict::Play)
            {
                if (verdict != midi::LearnedControls::Verdict::Swallow)
                {
                    (void) midiActions_.push(ev.pack());
                    triggerAsyncUpdate();
                }
                continue; // swallowed either way: a bound tab must not sound a pipe
            }
        }

        // A program change is a piston, not a note. It is swallowed here -- nothing
        // downstream has any use for it -- and handed to the message thread, which
        // is the only place a registration may be written.
        if (m.isProgramChange())
        {
            pendingProgram_.store(m.getProgramChangeNumber(), std::memory_order_relaxed);
            triggerAsyncUpdate();
            continue;
        }

        if (m.isNoteOn() || m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            const auto slot = static_cast<std::size_t>(note & 0x7F);

            // MIDI-learn: the first note-on while armed becomes the binding and is
            // swallowed (it neither sounds nor steps the sequencer).
            if (navLearn != 0 && m.isNoteOn())
            {
                seqLearnedNote_.store(navLearn * 256 + note, std::memory_order_relaxed);
                seqLearn_.store(0, std::memory_order_relaxed);
                navLearn = 0;
                navSwallowed_.set(slot);
                continue;
            }

            // Swallowing is decided on the NOTE-ON and remembered, so a note-off is
            // only ever eaten when its note-on was. Deciding it afresh on the
            // note-off meant that enabling the feature -- or re-binding it -- while
            // one of the two notes was already sounding ate the note-off and left
            // the note stuck until the next panic.
            if (m.isNoteOn())
            {
                if (navEnabled && (note == navPrev || note == navNext))
                {
                    (void) seqNav_.push(note == navPrev ? static_cast<std::int8_t>(-1)
                                                        : static_cast<std::int8_t>(1));
                    navSwallowed_.set(slot);
                    continue;
                }
            }
            else if (navSwallowed_.test(slot))
            {
                navSwallowed_.reset(slot);
                continue;
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

    // Console swell shoe. Stamped at the top of the block: a pointer drag has no
    // meaningful timestamp of its own, and the engine glides to the position
    // anyway, so a coarse arrival is not a coarse sound.
    for (UiExpressionEvent ev; uiExpression_.pop(ev); )
        commandBridge_.pushExpression(ev.division, ev.position);

    // Temperament and reference pitch. The audio thread only NOTICES: realising a
    // temperament is 128 exp2 calls into a table a note-on may be reading, so the
    // rebuild happens on the message thread and arrives as a snapshot.
    {
        const std::atomic<float>* tp = parameters_.rawParameter(ParameterLayout::kTemperament);
        const std::atomic<float>* ap = parameters_.rawParameter(ParameterLayout::kTuningA4Hz);
        if (tp != nullptr && ap != nullptr)
        {
            const int   choice = static_cast<int>(std::lround(tp->load(std::memory_order_relaxed)));
            const float a4     = ap->load(std::memory_order_relaxed);

            if (choice != sentTemperament_ || a4 != sentTuningA4_)
            {
                sentTemperament_ = choice;
                sentTuningA4_    = a4;

                std::uint32_t bits = 0;
                std::memcpy(&bits, &a4, sizeof(bits));
                pendingTuning_.store((static_cast<std::uint64_t>(
                                          static_cast<std::uint32_t>(choice)) << 32)
                                         | bits,
                                     std::memory_order_relaxed);
                pendingTuningValid_.store(true, std::memory_order_relaxed);
                triggerAsyncUpdate();
            }
        }
    }

    // Console panic (stop demo / release keys): silence every sounding voice and
    // clear the lit-key display so nothing sticks.
    if (uiPanic_.exchange(false, std::memory_order_relaxed))
    {
        commandBridge_.pushPanic();
        keys_ = {};
        navSwallowed_.reset();
    }

    // Single-producer encode of host intent onto the engine command ring, drained
    // by engine_.processBlock() below. Parameters first (cheap when unchanged),
    // then the host and on-screen note streams (both from THIS thread, so the ring
    // still has exactly one producer).
    // The console publishes a whole space preset, including the bass bloom that
    // has no host parameter. Take it as the new baseline and force it out, so the
    // APVTS values overlay it rather than the other way round.
    const bool consoleFresh = uiReverb_.hasFresh();
    if (consoleFresh)
        commandBridge_.syncReverbBaseline(uiReverb_.read());
    commandBridge_.pushChangedParameters(parameters_, consoleFresh);

    // Republish what the host's tail-length question should answer from. Relaxed
    // stores against relaxed loads on the message thread: at worst one block stale,
    // and never a race on the reverb's own members.
    {
        const core::ReverbParams& sent = commandBridge_.lastReverbSent();
        tailDecaySec_.store(sent.decaySec, std::memory_order_relaxed);
        tailPreDelayMs_.store(sent.preDelayMs, std::memory_order_relaxed);
    }
    // Console FIRST. Its events carry no meaningful timestamp and are stamped at
    // the top of the block, and the engine applies an offset earlier than its
    // current position immediately rather than rewinding -- so a console keypress
    // enqueued after a host event at frame 200 would be applied at frame 200.
    commandBridge_.pushConsoleMidi(uiScratch_, static_cast<int>(numFrames));
    commandBridge_.pushMidi(hostScratch_, static_cast<int>(numFrames)); // host MIDI minus swallowed nav keys
    midi.clear(); // this instrument produces no MIDI output

    // Wrap the host buffer as the JUCE-free AudioBlock — the only audio type that
    // crosses the engine seam — and render. The engine overwrites the buffer.
    // An offline bounce is not a deadline: the host runs the graph as fast as it
    // can, or as slowly as its disk allows, and either way the wall clock says
    // nothing about whether this machine can play the piece. Told so, the governor
    // keeps metering and stops acting, and a bounce comes out matching what the
    // organist heard live instead of thinned by a stopwatch. Asked every block
    // because a host can bounce and then go straight back to playing.
    engine_.setRealtime(! isNonRealtime());

    core::AudioBlock block(buffer.getArrayOfWritePointers(), numChannels, numFrames);
    engine_.processBlock(block);

    // Did the host move a drawstop? Sixty-four relaxed loads and one compare.
    //
    // A parameter LISTENER would be the obvious answer and is the wrong one:
    // juce::AudioProcessorValueTreeState calls its listeners under a
    // CriticalSection, on whichever thread set the value, and for host automation
    // that thread is this one. Registering a listener would take a lock on the
    // audio thread inside JUCE, before any of our code ran.
    //
    // The rebuild itself allocates, so it is handed to the message thread rather
    // than done here. Until it lands the instrument keeps sounding the previous
    // registration, which is what a drawstop does anyway: it takes a moment.
    if (const std::uint64_t hostBits = parameters_.stopBits(); hostBits != registration_)
    {
        pendingHostRegistration_.store(hostBits, std::memory_order_relaxed);
        pendingHostRegistrationValid_.store(true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }

    // The coupler jamb, diffed the same way and for the same reason: publishing a
    // rank table allocates nothing but is not something the audio thread should be
    // deciding to do.
    if (const std::uint32_t hostCouplers = parameters_.couplerBits();
        hostCouplers != couplers_)
    {
        pendingCouplers_.store(hostCouplers, std::memory_order_relaxed);
        pendingCouplersValid_.store(true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }

    // Post-reverb tone voicing (organ EQ). Linear, so its order vs the trims below
    // is sonically irrelevant; only "before the limiter" matters. The parameters
    // are pushed in here, on this thread, because this thread owns the EQ.
    applyEqParameters();
    masterEq_.process(block);

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

    // Console GAIN (pre-limiter drive), smoothed so a knob drag does not step the
    // level at every block boundary.
    uiMasterSmooth_.setTargetValue(uiMaster_.load(std::memory_order_relaxed));
    uiMasterSmooth_.applyGain(buffer, buffer.getNumSamples());

    // NO polyphony compensation. A pipe organ is an open-loop, fixed-gain mixer: each
    // pipe adds acoustic energy, so more notes/stops must get LOUDER, never quieter.
    // The old 1/(1+k*(N-1)) trim ducked a 10-note passage by ~5.6 dB and — because it
    // counted release-tail voices — breathed as chords were released ("volume drops
    // when playing more notes"). Headroom is instead baked in per-voice (conservative
    // base gain) and made real by the voices' uncorrelated drift, so the tutti sums
    // incoherently (~sqrt(N)) and stays under the ceiling without any dynamic trim —
    // exactly how Aeolus and GrandOrgue keep a full organ clean (no AGC anywhere).

    // Master limiter: a TRANSPARENT SAFETY NET (hold-based, no pumping) that only
    // catches a pathological fff transient — not a bus compressor riding every chord.
    // It replaced a per-sample tanh (a waveshaper that flattened the Tutti into
    // intermodulation distortion — the "explosion"). The Tutti stays loud AND clean.
    limiter_.process(block);

    // Console VOLUME (post-limiter output level), also smoothed. Applied across
    // the whole buffer so both channels share one ramp and the image cannot shift.
    uiVolumeSmooth_.setTargetValue(uiVolume_.load(std::memory_order_relaxed));
    uiVolumeSmooth_.applyGain(buffer, buffer.getNumSamples());

    // Final safety clamp to +/-1 (the limiter already holds -3 dBFS, so this
    // almost never fires) AND harvest the true mastered peak for the console VU.
    const int nSamp = buffer.getNumSamples();
    const int nCh   = buffer.getNumChannels();
    float peaks[2] = { 0.0f, 0.0f };
    for (int ch = 0; ch < nCh; ++ch)
    {
        float* d = buffer.getWritePointer(ch);
        float  pk = 0.0f;
        for (int i = 0; i < nSamp; ++i)
        {
            float v = d[i];
            v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            d[i] = v;
            const float a = v < 0.0f ? -v : v;
            if (a > pk) pk = a;
        }
        if (ch < 2) peaks[ch] = pk;
    }
    // Publish the REAL mastered output peak so the console VU reflects what is
    // heard (host/physical MIDI included). Mirror to both meters when mono.
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
    // From what was last SENT to the reverb, not the APVTS values: the console
    // sets a whole space preset at once and never touches the parameters, so a
    // host that trusted them could truncate a Cathedral tail to the default 2.5 s.
    //
    // And from atomics, not from reverb_.params(). This runs on the message
    // thread; the reverb's members are plain floats owned by the audio thread, and
    // reading them from here is a data race whatever the values happen to look
    // like. Add the longest voice release too -- the pipes are still speaking
    // after the last note-off.
    constexpr double kLongestVoiceReleaseSec = 0.35;
    return static_cast<double>(tailDecaySec_.load(std::memory_order_relaxed))
         + static_cast<double>(tailPreDelayMs_.load(std::memory_order_relaxed)) * 0.001
         + kLongestVoiceReleaseSec;
}

// ---------------------------------------------------------------------------
// Persistence.
// ---------------------------------------------------------------------------

juce::ValueTree CaeciliaAudioProcessor::captureConsoleState() const
{
    juce::ValueTree state{ CaeciliaParameterMirror::kConsoleTreeId };

    state.setProperty("master",   uiMaster_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("volume",   uiVolume_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("revSpace", reverbSpace_, nullptr);
    state.setProperty("revMix",   reverbMix_,   nullptr);
    // From the PARAMETERS, not from masterEq_. This runs on the message thread and
    // the EQ's members are plain floats the audio thread writes every block --
    // reading them here is a race whatever the numbers happen to look like.
    //
    // The APVTS already persists these, so this copy is redundant for anything
    // that reads a v3 document. It stays so an older build can still open one.
    if (const std::atomic<float>* on = parameters_.rawParameter(ParameterLayout::kEqOn))
        state.setProperty("eqOn", on->load(std::memory_order_relaxed) >= 0.5f, nullptr);
    for (std::size_t b = 0; b < dsp::MasterEq::kBands; ++b)
        if (const std::atomic<float>* p = parameters_.rawParameter(ParameterLayout::kEqBandIds[b]))
            state.setProperty(juce::Identifier("eq" + juce::String(static_cast<int>(b))),
                              p->load(std::memory_order_relaxed), nullptr);
    // The combination memory. Sparse -- "index:hex" for the slots that hold
    // something -- so a document carries eight pairs rather than 128 fields, and so
    // an ABSENT slot stays distinguishable from a cleared one.
    {
        juce::String packed;
        for (std::size_t i = 0; i < kNumGenerals; ++i)
        {
            if (! generalsSet_.test(i))
                continue;
            if (packed.isNotEmpty())
                packed << ',';
            packed << static_cast<int>(i) << ':'
                   << juce::String::toHexString(static_cast<juce::int64>(generals_[i]));
        }
        state.setProperty("generals", packed, nullptr);
    }

    // The learned MIDI bindings. One record per binding, fields separated by ':'
    // and records by ';', with the selector hex-encoded because it is free text
    // that may contain either separator.
    //
    // A packed string rather than a child tree for the same reason the combination
    // memory is one: an absent property means "this document predates the feature"
    // and must leave the bindings alone, where an empty child tree would read as
    // "the user cleared them" and silently unbind a console.
    {
        juce::String packed;
        for (std::size_t i = 0; i < midiMap_.bindingCount(); ++i)
        {
            const midi::MidiLearnBinding& b = midiMap_.bindingAt(i);
            if (! b.isValid())
                continue;

            juce::String selectorHex;
            for (const char c : b.command.selector.view())
                selectorHex << juce::String::toHexString(static_cast<int>(
                                    static_cast<unsigned char>(c))).paddedLeft('0', 2);

            if (packed.isNotEmpty())
                packed << ';';
            packed << static_cast<int>(b.source.kind)    << ':'
                   << static_cast<int>(b.source.channel) << ':'
                   << static_cast<int>(b.source.data1)   << ':'
                   << static_cast<int>(b.triggerThreshold) << ':'
                   << static_cast<int>(b.command.verb)   << ':'
                   << static_cast<int>(b.command.index)  << ':'
                   << static_cast<int>(b.command.division.value) << ':'
                   << selectorHex;
        }
        state.setProperty("midiBindings", packed, nullptr);
    }

    // Which organ this session is playing. A PATH rather than the document: an
    // organ file is a document the user owns and may still be editing, and copying
    // it into every project that uses it would freeze whatever it said that day.
    // The cost is that a moved file cannot be found on restore, which the loader
    // reports rather than papers over.
    //
    // And a path that did NOT resolve is written back unchanged rather than
    // replaced with what is playing instead. A project opened on a machine where
    // the organ file is missing must not be rewritten to the built-in organ by the
    // act of saving it: the file is missing here, not gone.
    state.setProperty("organPath",
                      organPath_.isNotEmpty() ? organPath_ : unresolvedOrgan_,
                      nullptr);

    state.setProperty("seqPrev", seqPrevNote_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("seqNext", seqNextNote_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("seqOn",   seqNavEnabled_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("playDiv", static_cast<int>(playDivision_.value), nullptr);

    // The drawn registration. From v4 the host parameters carry this -- one
    // boolean per stop -- so what goes here is a COPY, written for the same reason
    // the EQ's copy is written at v3: an older build opening this document has no
    // stop parameters to read, and would otherwise come up on the factory plenum.
    //
    // Rank by rank rather than by StopId, because that is the shape the older
    // build understands.
    juce::ValueTree regs{ "RANKS" };
    for (const model::Stop& s : organ_.stops())
    {
        if (! isStopEngaged(s.id()))
            continue;
        juce::ValueTree n{ "RANK" };
        n.setProperty("fam",  static_cast<int>(s.family()), nullptr);
        n.setProperty("num",  s.footage().num, nullptr);
        n.setProperty("den",  s.footage().den, nullptr);
        n.setProperty("comp", s.isCompound(), nullptr);
        regs.appendChild(n, nullptr);
    }
    state.appendChild(regs, nullptr);
    return state;
}

void CaeciliaAudioProcessor::applyConsoleState(const juce::ValueTree& state)
{
    if (! state.isValid())
        return;

    // The organ FIRST, before anything keyed to it. A registration, a combination
    // and a coupler mask are all positions in this organ's tables, and restoring
    // them onto the previous instrument would draw whatever happened to sit in
    // those slots.
    //
    // A document that predates this property leaves the built-in organ alone; a
    // path that no longer resolves leaves it alone too, and says so, because
    // silently substituting a different instrument for the one a project was
    // written on is the worst of the available answers.
    if (state.hasProperty("organPath"))
    {
        const juce::String path = state.getProperty("organPath").toString();
        if (path.isEmpty())
        {
            if (organPath_.isNotEmpty())
                loadBuiltInOrgan();
        }
        else if (path != organPath_)
        {
            const model::LoadDiagnostics d = loadOrganFile(juce::File(path));
            if (d.hasErrors())
            {
                // Held, not discarded. The editor asks for this when it attaches --
                // the host restores state before there is any UI, so there is
                // nowhere to report it at the moment it happens.
                unresolvedOrgan_ = path;
                juce::Logger::writeToLog("Caecilia: could not restore the organ at "
                                         + path + "; keeping the current one.");
            }
        }
    }

    setUiMaster(static_cast<float>(state.getProperty("master", 1.0)));
    setUiVolume(static_cast<float>(state.getProperty("volume", 1.0)));

    // The learned MIDI bindings, if the document has an opinion about them. A
    // document written before they existed has none, and its silence must not clear
    // a console the organist has already bound.
    if (state.hasProperty("midiBindings"))
    {
        midiMap_.clearBindings();

        const juce::String packed = state.getProperty("midiBindings").toString();
        juce::StringArray  records;
        records.addTokens(packed, ";", "");

        for (const juce::String& record : records)
        {
            if (record.isEmpty())
                continue;

            juce::StringArray f;
            f.addTokens(record, ":", "");
            if (f.size() < 8)
                continue; // a record this build does not understand is skipped, not guessed at

            midi::MidiLearnBinding b;
            b.source.kind      = static_cast<midi::MidiSource::Kind>(f[0].getIntValue());
            b.source.channel   = static_cast<midi::MidiChannel>(f[1].getIntValue());
            b.source.data1     = static_cast<std::uint8_t>(f[2].getIntValue());
            b.triggerThreshold = static_cast<std::uint8_t>(f[3].getIntValue());
            b.command.verb     = static_cast<midi::RegistrationVerb>(f[4].getIntValue());
            b.command.index    = static_cast<std::uint16_t>(f[5].getIntValue());
            b.command.division = core::DivisionId{ static_cast<std::uint16_t>(f[6].getIntValue()) };

            const juce::String hex = f[7];
            std::string        selector;
            for (int i = 0; i + 1 < hex.length(); i += 2)
                selector.push_back(static_cast<char>(
                    hex.substring(i, i + 2).getHexValue32()));
            b.command.selector.assign(selector);

            if (b.isValid())
                (void) midiMap_.installBinding(b);
        }
        publishBoundControls();
    }

    // The combination memory, if the document has one. A document written before
    // v5 has no opinion about the pistons, so the factory row built in the
    // constructor stands -- restoring "nothing captured" from its silence would
    // wipe a memory the user never asked to clear.
    if (state.hasProperty("generals"))
    {
        generals_.fill(0);
        generalsSet_.reset();

        const juce::String packed = state.getProperty("generals").toString();
        juce::StringArray  pairs;
        pairs.addTokens(packed, ",", "");
        for (const juce::String& pair : pairs)
        {
            const int colon = pair.indexOfChar(':');
            if (colon <= 0)
                continue;
            const int index = pair.substring(0, colon).getIntValue();
            if (index < 0 || static_cast<std::size_t>(index) >= kNumGenerals)
                continue;
            const auto bits = static_cast<std::uint64_t>(
                pair.substring(colon + 1).getHexValue64());
            if (bits == 0)
                continue; // an empty slot is simply not stored
            generals_[static_cast<std::size_t>(index)] = bits;
            generalsSet_.set(static_cast<std::size_t>(index));
        }
    }

    // The EQ keys in the console tree are the truth only for documents written
    // before the EQ became a parameter. In a v3 document the APVTS carries it and
    // this copy is a stale duplicate -- applying it would overwrite whatever the
    // host has automated, on every single project load.
    if (parameters_.lastDocumentVersion() < 3)
    {
        setUiEqEnabled(static_cast<bool>(state.getProperty("eqOn", true)));
        for (std::size_t b = 0; b < dsp::MasterEq::kBands; ++b)
        {
            const juce::Identifier id("eq" + juce::String(static_cast<int>(b)));
            if (state.hasProperty(id))
                setUiEqGain(static_cast<int>(b), static_cast<float>(state.getProperty(id)));
        }
    }

    setSeqNav(static_cast<int>(state.getProperty("seqPrev", 83)),
              static_cast<int>(state.getProperty("seqNext", 84)),
              static_cast<bool>(state.getProperty("seqOn", true)));

    if (const int div = static_cast<int>(state.getProperty("playDiv", -1)); div >= 0)
        playDivision_ = core::DivisionId{ static_cast<std::uint16_t>(div) };

    // Registration last: it rebuilds the voice bank, so everything else should
    // already be in place when it lands.
    //
    // ONLY for documents older than v4. From v4 the stops are host parameters, the
    // APVTS has already restored them, and this child is a stale duplicate --
    // applying it would overwrite what the host just put back, on every load.
    const juce::ValueTree regs = state.getChildWithName("RANKS");
    if (regs.isValid() && parameters_.lastDocumentVersion() < 4)
    {
        std::vector<model::RegistrationRank> ranks;
        ranks.reserve(static_cast<std::size_t>(regs.getNumChildren()));
        for (int i = 0; i < regs.getNumChildren(); ++i)
        {
            const juce::ValueTree n = regs.getChild(i);
            model::RegistrationRank r;
            r.family   = static_cast<core::TonalFamily>(
                juce::jlimit(0, static_cast<int>(core::TonalFamily::Undefined),
                             static_cast<int>(n.getProperty("fam", 0))));
            const int num = juce::jmax(1, static_cast<int>(n.getProperty("num", 8)));
            const int den = juce::jmax(1, static_cast<int>(n.getProperty("den", 1)));
            r.footage  = core::Footage{ num, den };
            r.compound = static_cast<bool>(n.getProperty("comp", false));
            ranks.push_back(r);
        }
        setUiRegistration(ranks);
    }

    // The reverb space is a preset plus a mix, and setUiReverb applies both.
    setUiReverb(static_cast<int>(state.getProperty("revSpace", 2)),
                static_cast<float>(state.getProperty("revMix", 0.28)));
}

void CaeciliaAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Everything the user set has to go in here. Saving only the APVTS meant the
    // registration, the console trims, the reverb space and the master EQ were all
    // silently discarded: reopening a project gave back the factory default organ.
    parameters_.writeState(destData, captureConsoleState());
}

void CaeciliaAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ValueTree console;
    if (parameters_.readState(data, sizeInBytes, console))
    {
        applyConsoleState(console);

        // Re-derive from the parameters immediately, rather than waiting for the
        // audio thread's next per-block diff. Two reasons: most hosts restore
        // state before they call prepareToPlay, so there may be no next block for
        // a while; and applyConsoleState may have written the parameters itself
        // from a pre-v4 document, in which case this is what makes the instrument
        // agree with them.
        applyRegistration(parameters_.stopBits(), RegistrationOrigin::Restore);

        // The couplers, for the same reason and in the same breath. They were not
        // here, and the per-block diff does eventually notice them -- so this was
        // never lost state, only late state. Late is still wrong: a console that
        // opens before the host has played a block draws the coupler jamb
        // disagreeing with the host's own parameters, and how long that lasts is
        // decided by whether anyone presses play.
        applyCouplers(parameters_.couplerBits(), RegistrationOrigin::Restore);

        // The restored registration is where this organist STARTS, so it becomes the
        // root. Reopening a project is not a move they made, and an undo from here
        // must not walk back into the previous document's registration.
        history_.reset(currentRegistrationState());

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
    // Plugin delay compensation: the master limiter's look-ahead is the only added
    // latency (the reverb pre-delay is a wet-only tail, not a dry-path delay).
    setLatencySamples(static_cast<int>(limiter_.latencySamples()));
}

} // namespace caecilia::plugin

// ---------------------------------------------------------------------------
// JUCE plugin entry point: the host instantiates the processor through this.
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new caecilia::plugin::CaeciliaAudioProcessor();
}

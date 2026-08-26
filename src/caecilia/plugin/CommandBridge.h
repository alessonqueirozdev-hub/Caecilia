// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IReverb.h"
#include "caecilia/core/ReverbSendGate.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngineCommand.h"
#include "caecilia/midi/ChannelToDivisionMap.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <span>
#include <cstddef>
#include <cstdint>

namespace caecilia::plugin
{

class CaeciliaParameterMirror;

/**
 * @brief The single producer that encodes host intent into @c EngineCommand
 *        values and pushes them onto the engine's SPSC command ring.
 *
 * The bridge is the ONE upstream writer the ring contract requires: it runs
 * entirely inside the plugin's @c processBlock on the audio thread and turns
 * three input streams into commands, in this order each block:
 *   1. changed automatable parameters  -> SetReverbParams / SetTremulant /
 *      SetTemperament,
 *   2. the host's @c juce::MidiBuffer  -> NoteOn / NoteOff / SetSustain / Panic,
 *   3. the on-screen console keyboard  -> NoteOn / NoteOff (the division is
 *      encoded in the channel, so it bypasses the controller map).
 *
 * Drawn stops do NOT travel this way. The console hands the whole registration to
 * @c CaeciliaAudioProcessor::setUiRegistration, which re-voices the pool off the
 * audio thread, so no StopEngage / StopDisengage is ever enqueued from here.
 *
 * It holds no DSP or registration logic — it only translates and enqueues. All
 * enqueue methods are @c noexcept and allocation-free; a full ring is handled by
 * dropping the command (the next block re-sends level/parameter state), never by
 * blocking or allocating.
 *
 * @note MIDI is decoded to core-native intent here at the seam; nothing pushed
 *       onto the ring carries a JUCE type. Off-thread registration @c StateDelta
 *       delivery (the message-thread rules engine) is a separate inbound path
 *       wired in a later phase — see // TODO(phase0.7) in the source.
 */
class CommandBridge
{
public:
    /// The exact ring type exposed by @c core::engine::AudioEngine.
    using Ring = core::engine::SpscRing<core::engine::EngineCommand,
                                        core::engine::kCommandQueueCapacity>;

    CommandBridge() noexcept = default;

    // --- off-thread wiring --------------------------------------------------

    /**
     * @brief Bind the producer end of the engine command ring.
     * @param ring The engine's @c commandQueue(). Must outlive this bridge.
     *
     * Off-thread (prepareToPlay). After this the enqueue methods are usable.
     */
    void connect(Ring& ring) noexcept { ring_ = &ring; }

    /**
     * @brief Reset per-block change tracking so the next block re-sends full state.
     *
     * Call from prepareToPlay so a freshly prepared engine receives current
     * parameter values on the first block. Off-thread.
     */
    void resetChangeTracking() noexcept;

    /// The division an UNMAPPED channel plays. Most single-keyboard controllers
    /// send everything on channel 1, so this keeps them working.
    void setDefaultDivision(core::DivisionId division) noexcept { defaultDivision_ = division; }

    /// Install the channel -> division routing used by @ref pushMidi. Off-thread.
    void setChannelMap(const midi::ChannelToDivisionMap& map) noexcept { channelMap_ = map; }

    /// The windchest tremulant/wind parameter commands target (scaffold: one chest).
    void setDefaultWindchest(core::WindchestId chest) noexcept { defaultChest_ = chest; }

    /// Tell the bridge which chests actually carry a tremulant.
    ///
    /// It used to send to @ref defaultChest_, which is chest 0 -- the Pedale on this
    /// organ, and it has no tremulant. The command went somewhere that could not act
    /// on it, so the host's Tremulant parameter was automatable, saved in the
    /// document, and inert.
    ///
    /// Off-thread, at prepare. Extra chests past the capacity are dropped rather
    /// than allocated for.
    void setTremulantChests(std::span<const core::WindchestId> chests) noexcept
    {
        tremulantChestCount_ = 0;
        for (const core::WindchestId c : chests)
            if (tremulantChestCount_ < tremulantChests_.size())
                tremulantChests_[tremulantChestCount_++] = c;
    }

    // --- audio-thread producers (RT-safe, single producer) ------------------

    /**
     * @brief Translate a host MIDI buffer into note/panic commands and enqueue them.
     * @param midi      The block's incoming MIDI, iterated in timestamp order.
     * @param numFrames The block's length, so each event's timestamp can be
     *                  clamped into it.
     *
     * Every command carries the host's own frame offset, so the engine renders up
     * to it before applying it. Without that, a note whose on and off shared a
     * block was applied on-then-off before a sample existed and came out bit-exactly
     * silent -- at the block sizes an offline bounce uses, every short note.
     *
     * // TODO(phase0.8): route learned CC/PC and si5/do6 sequencer navigation
     * through the shared MIDI router instead of note on/off, sustain, expression
     * and all-notes-off here.
     *
     * RT-safe: no allocation, no locks, no exceptions.
     */
    void pushMidi(const juce::MidiBuffer& midi, int numFrames) noexcept;

    /**
     * @brief Translate the ON-SCREEN keyboard's events, whose channel encodes the
     *        division directly (channel == division + 1) rather than a controller
     *        port. Keeping this separate from @ref pushMidi is what lets a
     *        physical keyboard on channel 1 and the console's Pedale keyboard
     *        coexist without one hijacking the other's routing.
     *
     * The console has no meaningful timestamp of its own -- a click arrives
     * whenever the message thread got round to it -- so its events are stamped at
     * the top of the block. That is why this stream must be pushed BEFORE the
     * host's: the engine applies an offset earlier than its current position
     * immediately rather than rewinding, so a console keypress enqueued after a
     * host event at frame 200 would be applied at frame 200.
     *
     * @param numFrames The block's length, for the same clamp as @ref pushMidi.
     *
     * RT-safe.
     */
    void pushConsoleMidi(const juce::MidiBuffer& midi, int numFrames) noexcept;

    /**
     * @brief Diff the automatable parameters and enqueue commands for any change.
     * @param params       The live parameter mirror (read lock-free via cached atomics).
     * @param forceReverb  Send the reverb set regardless of the gate below.
     *
     * Only parameters that actually moved since the previous block produce a
     * command, so a steady state costs nothing. RT-safe.
     *
     * The reverb is gated harder than the rest: a change smaller than the audible
     * threshold in @c core::reverbNeedsRecompute is not sent at all, because the
     * engine would discard it anyway and a knob drag is one message per block.
     * @p forceReverb exists for the console: it publishes a whole space preset,
     * including the bass bloom that has no host parameter, and that has to travel
     * even when the fields the APVTS covers happen not to have moved.
     */
    void pushChangedParameters(const CaeciliaParameterMirror& params,
                               bool forceReverb = false) noexcept;

    /// Seed the change tracker's reverb snapshot from the parameters the CONSOLE
    /// set, so a host tweak to one reverb control does not silently reset the
    /// fields the console owns and the APVTS does not expose (bassBloom).
    ///
    /// Call this ONLY when the console has actually published something, and pair
    /// it with @c forceReverb. Calling it every block with the reverb's own state
    /// -- which is what this used to do -- makes the gate below compare the
    /// incoming request against the DSP's clamped state rather than against what
    /// was last sent, so the settle counter never counts and the gate degenerates.
    void syncReverbBaseline(const core::ReverbParams& params) noexcept
    {
        reverbGate_.adoptBaseline(params);
    }

    /// The sample rate the reverb parameters will be interpreted at, so the gate
    /// clamps them the same way the reverb will. Off-thread (prepareToPlay).
    void setSampleRate(core::SampleRate sampleRate) noexcept
    {
        reverbGate_.setSampleRate(sampleRate);
    }

    /// @return The reverb parameter set most recently enqueued. The message thread
    ///         reads this indirectly (through published atomics) to answer the
    ///         host's tail-length question without touching the reverb itself.
    [[nodiscard]] const core::ReverbParams& lastReverbSent() const noexcept
    {
        return reverbGate_.lastSent();
    }

    /**
     * @brief Enqueue a global all-notes-off / all-sound-off panic.
     * RT-safe.
     */
    void pushPanic() noexcept;

    /**
     * @brief Enqueue a swell-shoe position for one division.
     * @param division The enclosed division whose box moves.
     * @param position 0 = shut, 1 = open. A POSITION, not a gain: how much a shut
     *        box attenuates is a property of the instrument, so the engine owns it.
     * RT-safe.
     */
    void pushExpression(core::DivisionId division, float position) noexcept;


private:
    void enqueue(const core::engine::EngineCommand& command) noexcept;

    /// Encode one note event and enqueue it. @p division has already been
    /// resolved and @p sampleOffset already clamped into the block.
    void pushNote(const juce::MidiMessage& msg, core::DivisionId division,
                  std::uint32_t sampleOffset) noexcept;

    Ring*             ring_            = nullptr;
    core::DivisionId  defaultDivision_ {};
    core::WindchestId defaultChest_    {};

    /// The chests with a tremulant. Fixed capacity: no allocation on the audio path.
    std::array<core::WindchestId, 16> tremulantChests_{};
    std::size_t                       tremulantChestCount_ = 0;
    midi::ChannelToDivisionMap channelMap_{};

    /// The reverb's own gate. It lives in core because its settle counter is the
    /// kind of edge case that has to be provable, and nothing in this target can
    /// be reached by the test suite. See core/ReverbSendGate.h.
    core::ReverbSendGate reverbGate_{};

    // --- last-sent snapshot for change detection ----------------------------
    struct LastSent
    {
        bool           valid = false;
        bool           tremOn    = false;
        float          tremRate  = 0.0f;
        float          tremDepth = 0.0f;
        int            temperament = -1;
        double         tuningA4Hz  = 0.0;
    };

    LastSent last_{};
};

} // namespace caecilia::plugin

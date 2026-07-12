/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IReverb.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngineCommand.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>

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
 *   1. the host's @c juce::MidiBuffer  -> NoteOn / NoteOff / Panic,
 *   2. changed automatable parameters  -> SetReverbParams / SetTremulant /
 *      SetTemperament,
 *   3. drawn/retired stops (from the reserved stop pool) -> StopEngage /
 *      StopDisengage.
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

    /// The default division note-ons are routed to until per-channel mapping lands.
    void setDefaultDivision(core::DivisionId division) noexcept { defaultDivision_ = division; }

    /// The windchest tremulant/wind parameter commands target (scaffold: one chest).
    void setDefaultWindchest(core::WindchestId chest) noexcept { defaultChest_ = chest; }

    // --- audio-thread producers (RT-safe, single producer) ------------------

    /**
     * @brief Translate a host MIDI buffer into note/panic commands and enqueue them.
     * @param midi The block's incoming MIDI, iterated in timestamp order.
     *
     * Commands are drained at the next block boundary, so events currently land at
     * block granularity (the @c EngineCommand payload carries no sub-block offset).
     * // TODO(phase0.6): add a sample offset to the note payload for sample-accurate
     * scheduling. // TODO(phase0.8): route learned CC/PC and si5/do6 sequencer
     * navigation through the shared MIDI router instead of only note on/off +
     * all-notes-off here.
     *
     * RT-safe: no allocation, no locks, no exceptions.
     */
    void pushMidi(const juce::MidiBuffer& midi) noexcept;

    /**
     * @brief Diff the automatable parameters and enqueue commands for any change.
     * @param params The live parameter mirror (read lock-free via cached atomics).
     *
     * Only parameters that actually moved since the previous block produce a
     * command, so a steady state costs nothing. RT-safe.
     */
    void pushChangedParameters(const CaeciliaParameterMirror& params) noexcept;

    /**
     * @brief Enqueue a global all-notes-off / all-sound-off panic.
     * RT-safe.
     */
    void pushPanic() noexcept;

private:
    void enqueue(const core::engine::EngineCommand& command) noexcept;

    Ring*             ring_            = nullptr;
    core::DivisionId  defaultDivision_ {};
    core::WindchestId defaultChest_    {};

    // --- last-sent snapshot for change detection ----------------------------
    struct LastSent
    {
        bool           valid = false;
        core::ReverbParams reverb{};
        bool           tremOn    = false;
        float          tremRate  = 0.0f;
        float          tremDepth = 0.0f;
        int            temperament = -1;
        double         tuningA4Hz  = 0.0;
    };

    LastSent last_{};
};

} // namespace caecilia::plugin

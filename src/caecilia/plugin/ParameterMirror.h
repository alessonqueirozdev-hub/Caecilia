// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/plugin/ParameterLayout.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <cstdint>
#include <span>
#include <cstddef>

namespace caecilia::model
{
class Organ; // forward decl; the mirror binds stop slots to a loaded organ's stops.
}

namespace caecilia::plugin
{

/**
 * @brief Owns the host-facing parameter state: the automatable APVTS, plus the
 *        (still empty) @c juce::ValueTree reserved for semantic registration.
 *
 * Two representations:
 *  - The @b APVTS holds the flat, automatable parameters the host knows about
 *    (global controls + the reserved boolean stop pool from @ref ParameterLayout).
 *    The owned @c UndoManager is the one the APVTS itself was constructed with.
 *  - The @b registration @c ValueTree is meant to carry the RICHER registration
 *    truth the host cannot express — semantic identity, provenance ("why is this
 *    on"), Selector intent, named groups. It is written into the saved document
 *    and read back, but nothing ever puts anything in it, so today it round-trips
 *    empty and drives no undo/redo.
 *    @todo(phase0.7) Populate it from the sounding registration.
 *
 * The mirror never mutates live engine state. Parameter edits are turned into
 * @c EngineCommand values by @ref CommandBridge and pushed over the SPSC ring.
 * What actually sounds is held by the processor (see
 * @c CaeciliaAudioProcessor::setUiRegistration), not here, and no engine
 * confirmation is written back. All methods run on the message thread (NOT
 * real-time safe): APVTS raw-parameter READS are the only part touched by the
 * audio thread, via cached atomic pointers.
 */
class CaeciliaParameterMirror
{
public:
    /**
     * @brief Construct the APVTS against @p processor and build the layout.
     * @param processor The owning @c AudioProcessor (APVTS attaches to it).
     *
     * Off-thread. Allocates the parameter tree; called once from the processor's
     * constructor.
     */
    /**
     * @param processor    The owning @c AudioProcessor (APVTS attaches to it).
     * @param organ        The instrument, which names the stop parameters.
     * @param defaultDrawn The opening registration, which becomes their defaults.
     *
     * The organ has to exist before this does, which is why the processor's member
     * order puts it first. That is not incidental: it is what makes the opening
     * plenum the parameters' own default rather than something applied afterwards,
     * and therefore what makes the host and the instrument agree from the first
     * instant — including across the host's own "reset to default".
     */
    CaeciliaParameterMirror(juce::AudioProcessor&         processor,
                            const model::Organ&           organ,
                            std::span<const core::StopId> defaultDrawn);

    // --- accessors ----------------------------------------------------------

    [[nodiscard]] juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }
    [[nodiscard]] const juce::AudioProcessorValueTreeState& apvts() const noexcept { return apvts_; }

    [[nodiscard]] juce::UndoManager& undoManager() noexcept { return undoManager_; }

    /// The parallel semantic registration tree (mirrors engine registration state).
    [[nodiscard]] juce::ValueTree& registrationTree() noexcept { return registrationTree_; }

    /**
     * @brief Cached atomic pointer to a global parameter's normalised-mapped value.
     * @param paramId One of the @ref ParameterLayout global IDs.
     * @return The live @c std::atomic<float>* the audio thread can read lock-free,
     *         or nullptr if unknown. Cached in @ref cacheParameterPointers.
     */
    [[nodiscard]] std::atomic<float>* rawParameter(const char* paramId) const noexcept;

    /**
     * @brief Live value of the stop parameter for @p index, lock-free.
     * @param index Slot in [0, @ref ParameterLayout::kMaxStopParameters), which is
     *              also the @c StopId::value it stands for.
     * @return true if that stop is engaged. RT-safe read.
     */
    [[nodiscard]] bool stopEngaged(std::size_t index) const noexcept;

    // --- the registration as one word ---------------------------------------
    //
    // There is no binding step. Slot index IS StopId::value, so a lookup table
    // between them would be a table mapping a number onto itself — and one more
    // thing that could fall out of step.

    /**
     * @brief Every stop parameter, folded into a bit mask.
     * @return Bit N set when @c stop_N is engaged.
     *
     * This is the whole cross-thread registration read: 64 relaxed loads and a
     * shift each, once per block, compared against what is already sounding. No
     * lock, no allocation, no listener.
     *
     * The alternative — an APVTS parameter listener — was measured against and
     * rejected: @c parameterValueChanged fires under a @c CriticalSection on the
     * thread that set the value, and for host automation that thread is the audio
     * thread. Registering one would take a lock on the audio thread inside JUCE,
     * before any of our code ran.
     *
     * RT-safe, @c noexcept.
     */
    [[nodiscard]] std::uint64_t stopBits() const noexcept;

    /// The drawn couplers, as a mask keyed by the organ's coupler index.
    ///
    /// A separate mask from the stops rather than more bits in the same one: a
    /// coupler is not a stop, it is drawn from its own jamb, and the registration
    /// mask's width is the audio thread's compare -- it means one specific thing
    /// and should keep meaning it.
    [[nodiscard]] std::uint32_t couplerBits() const noexcept;

    /**
     * @brief Drive the stop parameters from a bit mask, notifying the host.
     * @param bits Bit N engages @c stop_N.
     *
     * For when something other than the host moves the registration — a console
     * click, a combination piston, a restored session. Only slots that actually
     * change are written, so a no-op costs nothing and cannot spam an automation
     * lane. MESSAGE THREAD ONLY: setValueNotifyingHost is not real-time safe.
     */
    void writeStopBits(std::uint64_t bits);

    /// @copydoc writeStopBits but for the coupler jamb.
    void writeCouplerBits(std::uint32_t bits);

    // --- persistence --------------------------------------------------------

    /**
     * @brief Serialise APVTS + registration tree into @p dest for the host.
     * @param dest Destination block (overwritten). Off-thread.
     *
     * Non-const: it flushes live parameter values into the state via
     * @c AudioProcessorValueTreeState::copyState() before writing.
     */
    /// @param consoleState Extra state the host cannot express as parameters --
    ///        the drawn registration, the console trims, the reverb space. It is
    ///        nested under the same document so one blob round-trips everything.
    void writeState(juce::MemoryBlock& dest, const juce::ValueTree& consoleState);

    /**
     * @brief Restore APVTS + registration tree from host-provided data.
     * @param data     Pointer to the saved blob.
     * @param sizeBytes Its size in bytes.
     * @return true if a well-formed state was applied. Off-thread.
     */
    /// @param consoleStateOut Receives the nested console tree, or an invalid
    ///        tree when the blob predates it.
    bool readState(const void* data, int sizeBytes, juce::ValueTree& consoleStateOut);

    /// @return The document version most recently handed to @ref readState, or
    ///         @ref kDocumentVersion when nothing has been read. Callers use it to
    ///         decide whether a legacy key in the console tree is the truth or a
    ///         stale duplicate of a parameter.
    [[nodiscard]] int lastDocumentVersion() const noexcept { return lastDocumentVersion_; }

    // Root/child identifiers for the persisted state document. kConsoleTreeId is
    // public because the processor builds that child itself -- it owns the console
    // state; the mirror only carries it through the document.
    static const juce::Identifier kStateRootId;
    static const juce::Identifier kRegistrationTreeId;
    static const juce::Identifier kConsoleTreeId;
    static const juce::Identifier kVersionProperty;

    /// Bumped whenever the saved document changes shape. Readers use it to decide
    /// what a blob is allowed to omit; without it there is no migration path at
    /// all, and the first format change silently corrupts every saved session.
    /// Bumped to 3 when the master EQ became a host parameter. A v2 document
    /// carries the EQ in its console tree and nothing in its APVTS; a v3 document
    /// carries it in the APVTS and the console copy is a stale duplicate. Applying
    /// the console copy of a v3 document would overwrite the host's automation on
    /// every project load, which is why @ref lastDocumentVersion exists.
    ///
    /// Bumped to 4 when the drawn registration became a host parameter, for the
    /// same reason and with the same consequence: in a v4 document the RANKS child
    /// of the console tree is a stale duplicate of the stop parameters, and
    /// applying it would overwrite what the host restored.
    /// v5 adds the combination memory (`generals`). A v4 reader does not see the
    /// property and falls back to the factory row, which is the right degradation.
    static constexpr int kDocumentVersion = 5;

private:
    void cacheParameterPointers();

    juce::UndoManager                     undoManager_;
    /// @see lastDocumentVersion
    int lastDocumentVersion_ = kDocumentVersion;

    juce::AudioProcessorValueTreeState    apvts_;
    juce::ValueTree                       registrationTree_;

    /// Cached raw-parameter pointers for lock-free audio-thread reads.
    std::array<std::atomic<float>*, ParameterLayout::kMaxStopParameters> stopParams_{};
    std::array<std::atomic<float>*, ParameterLayout::kMaxCouplerParameters>
        couplerParams_{};
    std::array<juce::RangedAudioParameter*, ParameterLayout::kMaxCouplerParameters>
        couplerParamObjects_{};

    /// Cached parameter objects for the message thread's writes. Resolving them by
    /// string on every console click would be a map lookup per stop.
    std::array<juce::RangedAudioParameter*, ParameterLayout::kMaxStopParameters>
        stopParamObjects_{};
};

} // namespace caecilia::plugin

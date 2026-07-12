/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/plugin/ParameterLayout.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <cstddef>

namespace caecilia::model
{
class Organ; // forward decl; the mirror binds stop slots to a loaded organ's stops.
}

namespace caecilia::plugin
{

/**
 * @brief Owns the host-facing parameter state: the automatable APVTS plus a
 *        parallel semantic @c juce::ValueTree backed by a @c juce::UndoManager.
 *
 * Two representations, kept in sync:
 *  - The @b APVTS holds the flat, automatable parameters the host knows about
 *    (global controls + the reserved boolean stop pool from @ref ParameterLayout).
 *  - The @b registration @c ValueTree mirrors the RICHER registration truth the
 *    host cannot express — semantic identity, provenance ("why is this on"),
 *    Selector intent, named groups — and drives undo/redo through the owned
 *    @c UndoManager.
 *
 * The mirror never mutates live engine state. Parameter and registration edits
 * are turned into @c EngineCommand values by @ref CommandBridge and pushed over
 * the SPSC ring; conversely, registration changes the engine confirms off-thread
 * are written back here so the host and UI stay consistent. All methods here run
 * on the message thread (NOT real-time safe): APVTS raw-parameter READS are the
 * only part touched by the audio thread, via cached atomic pointers.
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
    explicit CaeciliaParameterMirror(juce::AudioProcessor& processor);

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
     * @brief Live value of the @p index-th reserved stop parameter (0/1), lock-free.
     * @param index Slot in [0, @ref ParameterLayout::kMaxStopParameters).
     * @return true if the slot's boolean parameter is engaged. RT-safe read.
     */
    [[nodiscard]] bool stopEngaged(std::size_t index) const noexcept;

    // --- organ binding ------------------------------------------------------

    /**
     * @brief Bind reserved stop-parameter slots to the stops of a loaded organ.
     * @param organ The compiled instrument whose stops map onto the pool.
     *
     * Off-thread (organ-load time). Assigns slot@e i to the organ's @e i-th stop
     * (up to @ref ParameterLayout::kMaxStopParameters), updates parameter display
     * names to the stop's label, and clears any surplus slots. After this the
     * audio thread may map a slot index back to a @c core::StopId via
     * @ref stopIdForSlot.
     */
    void bindOrgan(const model::Organ& organ);

    /// @return The @c StopId bound to reserved slot @p index, or a zero id if unbound.
    [[nodiscard]] core::StopId stopIdForSlot(std::size_t index) const noexcept;

    /// @return Number of stop slots currently bound to a loaded organ.
    [[nodiscard]] std::size_t boundStopCount() const noexcept { return boundStopCount_; }

    // --- persistence --------------------------------------------------------

    /**
     * @brief Serialise APVTS + registration tree into @p dest for the host.
     * @param dest Destination block (overwritten). Off-thread.
     *
     * Non-const: it flushes live parameter values into the state via
     * @c AudioProcessorValueTreeState::copyState() before writing.
     */
    void writeState(juce::MemoryBlock& dest);

    /**
     * @brief Restore APVTS + registration tree from host-provided data.
     * @param data     Pointer to the saved blob.
     * @param sizeBytes Its size in bytes.
     * @return true if a well-formed state was applied. Off-thread.
     */
    bool readState(const void* data, int sizeBytes);

private:
    void cacheParameterPointers();

    // Root/child identifiers for the persisted state document.
    static const juce::Identifier kStateRootId;
    static const juce::Identifier kRegistrationTreeId;

    juce::UndoManager                     undoManager_;
    juce::AudioProcessorValueTreeState    apvts_;
    juce::ValueTree                       registrationTree_;

    /// Cached raw-parameter pointers for lock-free audio-thread reads.
    std::array<std::atomic<float>*, ParameterLayout::kMaxStopParameters> stopParams_{};
    std::array<core::StopId, ParameterLayout::kMaxStopParameters>        slotStopIds_{};
    std::size_t                                                          boundStopCount_ = 0;
};

} // namespace caecilia::plugin

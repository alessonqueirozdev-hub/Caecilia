// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/TripleBuffer.h"
#include "caecilia/engine/MeterSnapshot.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

/**
 * @file StateMirror.h
 * @brief The SECOND layer of the UI pillar: a lock-free, wait-free bridge the
 *        audio thread writes and the UI polls at frame rate.
 *
 * The audio thread NEVER touches a @c juce::Component. Instead it publishes an
 * immutable @c ConsoleFrame (metering + which keys are lit and why) into a
 * double-buffered slot and flips an atomic index; the UI always reads a fully
 * consistent frame with no lock and no torn state. This header is deliberately
 * JUCE-free and allocation-free so the writer half is safe on the audio thread.
 */

namespace caecilia::ui
{

/// Why a key is currently lit — chooses its illumination colour, mirroring real
/// console practice (blue = played here, red = coupled in, purple = combination).
/// @note Only Off and PlayedDirect are ever published today (see
///       CaeciliaAudioProcessor::processBlock). Couplers are defined in the model
///       but never applied, and combination recalls are resolved in the web
///       console, so red and purple never actually appear.
enum class KeySource : std::uint8_t
{
    Off         = 0, ///< Not sounding.
    PlayedDirect,    ///< Pressed on this division's own keyboard (blue).
    Coupled,         ///< Sounded via a coupler from another division (red).
    Combination      ///< Driven by a combination/sequencer action (purple).
};

/**
 * @brief Live lit-key state for every metered division.
 *
 * A dense byte per (division, note): the @c KeySource value. POD and trivially
 * copyable so it can be memcpy-published across the double buffer.
 */
struct KeyStateSnapshot
{
    static constexpr std::size_t kMaxDivisions = core::engine::MeterSnapshot::kMaxMeteredDivisions;
    static constexpr std::size_t kNotes        = 128;

    std::array<std::array<std::uint8_t, kNotes>, kMaxDivisions> source{}; ///< KeySource per (div,note).

    /// Set the lit source for one key. Audio-thread safe (writes a byte).
    void set(std::size_t division, core::MidiNote note, KeySource src) noexcept
    {
        if (division < kMaxDivisions && note < kNotes)
            source[division][note] = static_cast<std::uint8_t>(src);
    }

    /// @return the lit source for one key (Off if out of range).
    [[nodiscard]] KeySource get(std::size_t division, core::MidiNote note) const noexcept
    {
        if (division < kMaxDivisions && note < kNotes)
            return static_cast<KeySource>(source[division][note]);
        return KeySource::Off;
    }
};

/// One consistent frame of animatable console truth.
struct ConsoleFrame
{
    core::engine::MeterSnapshot meters{}; ///< Levels and voice count; wind fields unfilled.
    KeyStateSnapshot            keys{};   ///< Which keys are lit and why.
};

/**
 * @brief Single-writer / single-reader mirror of the console's animatable state.
 *
 * @c publish runs on the audio thread, @c read on the UI thread, and neither
 * ever blocks. The handoff is a @ref core::TripleBuffer: a two-buffer flip looks
 * correct but lets the writer come back round onto the slot the reader is still
 * copying, which is a data race, not merely a torn frame. See TripleBuffer.h.
 */
class StateMirror
{
public:
    StateMirror() = default;

    /**
     * @brief Publish a new frame. Call only from the audio thread.
     *
     * Real-time safe: one copy and one atomic exchange. No allocation, no lock.
     */
    void publish(const ConsoleFrame& frame) noexcept { buffer_.write(frame); }

    /// Convenience overload publishing meters + keys separately.
    void publish(const core::engine::MeterSnapshot& meters,
                 const KeyStateSnapshot&            keys) noexcept
    {
        ConsoleFrame frame;
        frame.meters = meters;
        frame.keys   = keys;
        buffer_.write(frame);
    }

    /**
     * @brief Read the most recently published frame. Call from the UI thread.
     * @return a complete frame, never torn.
     */
    [[nodiscard]] ConsoleFrame read() const noexcept { return buffer_.read(); }

private:
    core::TripleBuffer<ConsoleFrame> buffer_{};
};

} // namespace caecilia::ui

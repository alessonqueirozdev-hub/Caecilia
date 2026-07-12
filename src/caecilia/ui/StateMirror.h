/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
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
    core::engine::MeterSnapshot meters{}; ///< Levels, wind sag, tremulant phase, voice count.
    KeyStateSnapshot            keys{};   ///< Which keys are lit and why.
};

/**
 * @brief Double-buffered single-writer / single-reader mirror of the console's
 *        animatable state.
 *
 * @c publish (audio thread) writes the back buffer then flips the published
 * index with release semantics; @c read (message thread) acquires the index and
 * copies the front buffer. There is exactly one writer and one reader, so no
 * lock is required and neither thread ever blocks.
 */
class StateMirror
{
public:
    StateMirror() = default;

    /**
     * @brief Publish a new frame. Call only from the audio thread.
     *
     * Real-time safe: writes into the inactive buffer and flips an atomic index.
     * No allocation, no lock, @c noexcept.
     */
    void publish(const ConsoleFrame& frame) noexcept
    {
        const std::size_t back = 1u - live_.load(std::memory_order_relaxed);
        buffers_[back] = frame;
        live_.store(back, std::memory_order_release);
    }

    /// Convenience overload publishing meters + keys separately.
    void publish(const core::engine::MeterSnapshot& meters,
                 const KeyStateSnapshot&            keys) noexcept
    {
        const std::size_t back = 1u - live_.load(std::memory_order_relaxed);
        buffers_[back].meters = meters;
        buffers_[back].keys   = keys;
        live_.store(back, std::memory_order_release);
    }

    /**
     * @brief Read the most recently published frame. Call from the UI thread.
     * @return a copy of the current front buffer (never torn).
     */
    [[nodiscard]] ConsoleFrame read() const noexcept
    {
        const std::size_t front = live_.load(std::memory_order_acquire);
        return buffers_[front];
    }

private:
    std::array<ConsoleFrame, 2> buffers_{};
    std::atomic<std::size_t>    live_{ 0 };
};

} // namespace caecilia::ui

// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace caecilia::core
{

/**
 * @brief A wait-free, race-free single-writer / single-reader value handoff.
 *
 * This is the sanctioned way to publish a snapshot from the audio thread to the
 * UI. It replaces the obvious two-buffer scheme, which is subtly wrong:
 *
 *   writer: back = 1 - front;  buffers[back] = frame;  front = back;
 *   reader: return buffers[front];
 *
 * With only two slots the writer alternates between them, so two blocks after
 * the reader latches an index the writer comes back round and starts
 * overwriting the very slot the reader is still copying. The reader then sees a
 * frame half from one block and half from another, and — the part a debugger
 * will not show you — it is a data race, which is undefined behaviour, not
 * merely a torn value. It almost never bites, because a UI copy takes under a
 * microsecond and two audio blocks take milliseconds; it bites when the UI
 * thread is preempted mid-copy, which is exactly when the machine is loaded.
 *
 * Three slots remove the possibility entirely: the writer always owns one slot,
 * the reader always owns one, and the third is the handoff point. Neither side
 * ever touches the other's slot, and neither side ever waits.
 *
 * @tparam T Trivially copyable snapshot type.
 */
template <class T>
class TripleBuffer
{
    static_assert(std::is_trivially_copyable_v<T>,
                  "TripleBuffer publishes by plain copy; T must be trivially copyable");

public:
    TripleBuffer() = default;

    /**
     * @brief Publish a new value. Call from the writer (audio) thread only.
     *
     * Wait-free: one copy plus one atomic exchange, no allocation, no lock.
     */
    void write(const T& value) noexcept
    {
        buffers_[writeIndex_] = value;
        // Hand our slot over and take whatever the reader is not using. Release
        // ordering publishes the copy above to whoever picks the slot up.
        const std::uint32_t previous =
            handoff_.exchange(writeIndex_ | kFreshBit, std::memory_order_release);
        writeIndex_ = previous & kIndexMask;
    }

    /**
     * @brief Has the writer published something the reader has not taken yet?
     *
     * Reader thread only, and it does NOT consume: the point is to let the reader
     * decide whether a read is worth doing at all -- when the answer is no, it can
     * skip work that would otherwise run every block on stale data.
     *
     * Relaxed: a false negative simply defers the read by one block, and there is
     * nothing to order against because nothing is being read here.
     */
    [[nodiscard]] bool hasFresh() const noexcept
    {
        return (handoff_.load(std::memory_order_relaxed) & kFreshBit) != 0u;
    }

    /**
     * @brief Read the most recently published value. Reader thread only.
     * @return The newest complete value; the previous one if nothing new was
     *         published since the last call.
     *
     * Wait-free and never torn.
     */
    [[nodiscard]] T read() const noexcept
    {
        if ((handoff_.load(std::memory_order_relaxed) & kFreshBit) != 0u)
        {
            const std::uint32_t previous =
                handoff_.exchange(readIndex_, std::memory_order_acquire);
            readIndex_ = previous & kIndexMask;
        }
        return buffers_[readIndex_];
    }

private:
    static constexpr std::uint32_t kIndexMask = 0x3u;
    static constexpr std::uint32_t kFreshBit  = 0x4u; ///< Set when unread data waits.

    std::array<T, 3> buffers_{};

    // uint32_t, not size_t: these are packed into handoff_ alongside kFreshBit,
    // and a size_t index narrowed on every exchange.
    std::uint32_t                     writeIndex_ = 0;      ///< Writer-owned slot.
    mutable std::uint32_t             readIndex_  = 1;      ///< Reader-owned slot.
    mutable std::atomic<std::uint32_t> handoff_{ 2u };      ///< The slot in between.
};

} // namespace caecilia::core

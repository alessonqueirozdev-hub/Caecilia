/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace caecilia::core::engine
{

/**
 * @brief A wait-free single-producer / single-consumer bounded ring buffer.
 *
 * This is the ONLY sanctioned channel for pushing state changes into the audio
 * thread: one non-audio producer thread @ref push()es @c EngineCommand values
 * and the audio thread @ref pop()s them at the top of each block. No locks, no
 * allocation, no exceptions — the storage is a fixed @p Capacity array reserved
 * at construction.
 *
 * ## Contract
 * - Exactly one producer thread and one consumer thread. Multiple producers
 *   MUST be funnelled through a single upstream aggregator first.
 * - @p Capacity must be a power of two so the index wrap is a cheap mask. One
 *   slot is reserved to disambiguate full from empty, so the usable capacity is
 *   @p Capacity - 1.
 * - @p T must be trivially copyable (it is memcpy-relocated between threads).
 *
 * The head/tail indices are published with acquire/release ordering so a
 * consumer that observes an advanced tail also observes the fully-written slot.
 */
template <class T, std::size_t Capacity>
class SpscRing
{
public:
    static_assert(Capacity >= 2, "SpscRing capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "SpscRing capacity must be a power of two");

    /// @return Maximum number of items that can be queued at once.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

    /**
     * @brief Producer side: enqueue a copy of @p item.
     * @return false if the ring is full (the item was dropped); true otherwise.
     *
     * Wait-free and @c noexcept. Safe on any thread as long as it is the single
     * producer.
     */
    [[nodiscard]] bool push(const T& item) noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & kMask;
        if (next == head_.load(std::memory_order_acquire))
            return false; // full
        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief Consumer side: dequeue the oldest item into @p out.
     * @return false if the ring is empty; true if @p out was written.
     *
     * Wait-free and @c noexcept. Call only from the single consumer (audio)
     * thread.
     */
    [[nodiscard]] bool pop(T& out) noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return false; // empty
        out = buffer_[head];
        head_.store((head + 1) & kMask, std::memory_order_release);
        return true;
    }

    /// @return true if no items are currently queued (consumer's view).
    [[nodiscard]] bool empty() const noexcept
    {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer_{};
    std::atomic<std::size_t> head_{0}; ///< Next slot to read (consumer owned).
    std::atomic<std::size_t> tail_{0}; ///< Next slot to write (producer owned).
};

} // namespace caecilia::core::engine

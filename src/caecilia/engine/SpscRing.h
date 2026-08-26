// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

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
 * and the audio thread drains them with @ref pop, or with @ref peek / @ref drop
 * when it needs to inspect an item before committing to consume it. No locks, no
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

    /**
     * @brief Consumer side: look at the oldest item WITHOUT removing it.
     * @return A pointer to the item, or nullptr if the ring is empty.
     *
     * Exists because the audio thread has to be able to read an event's timestamp,
     * decide it belongs to a later part of the block, and leave it queued for the
     * next slice. @ref pop cannot express that: once an item is out, putting it
     * back would need a second producer.
     *
     * The returned pointer stays valid until this consumer calls @ref drop or
     * @ref pop. That is not a courtesy, it is structural: @ref push refuses when
     * the next write index would equal @c head_, so the highest slot the producer
     * can reach is @c head_-1, and nothing but the consumer moves @c head_. The
     * acquire on @c tail_ is the identical handshake @ref pop performs, so an
     * observed item is a fully-written one.
     *
     * Wait-free and @c noexcept. Consumer thread only.
     */
    [[nodiscard]] const T* peek() const noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return nullptr; // empty
        return &buffer_[head];
    }

    /**
     * @brief Consumer side: discard the oldest item. Pairs with @ref peek.
     * @return false if the ring was already empty, in which case nothing moved.
     *
     * Emptiness is re-checked rather than taken on trust from the caller. An
     * unpaired drop that advanced @c head_ past @c tail_ would not merely lose an
     * item: the ring would then report @c Capacity-1 phantom items and hand the
     * audio thread stale commands forever.
     *
     * Wait-free and @c noexcept. Consumer thread only.
     */
    bool drop() noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return false; // empty
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

    /// Cache line, for keeping the producer's and the consumer's indices apart.
    ///
    /// std::hardware_destructive_interference_size is the right answer but is not
    /// available everywhere at the language level we target, and 64 is correct on
    /// every architecture this runs on.
    static constexpr std::size_t kCacheLine = 64;

    std::array<T, Capacity> buffer_{};

    // head_ and tail_ used to sit adjacent, so every push by the producer
    // invalidated the cache line the consumer was reading its own index from, and
    // vice versa -- false sharing on the one structure the audio thread crosses
    // every block. Separating them costs a few bytes.
    //
    // MSVC's C4324 says the structure was padded because of an alignment
    // specifier. It was; that is what the specifier is for. Suppressed here rather
    // than project-wide, so the same warning still means something everywhere else.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    alignas(kCacheLine) std::atomic<std::size_t> head_{0}; ///< Next slot to read.
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; ///< Next slot to write.
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
};

} // namespace caecilia::core::engine

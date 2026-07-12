/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace caecilia::core
{

/**
 * @brief Non-owning, JUCE-free view over a multi-channel block of 32-bit float
 *        audio, plus the block metadata a voice needs to render.
 *
 * An AudioBlock never allocates and never owns its storage: it points at a
 * caller-owned array of channel pointers (typically the per-windchest
 * accumulation buses set up in RenderContext during prepare()). This is the
 * ONLY audio-buffer type visible to the pure core; no juce::AudioBuffer ever
 * crosses the AudioEngine seam.
 *
 * A sample offset (@ref sampleOffset) lets a block be re-based to the middle of
 * a host callback without copying, so the wind model can split a callback at a
 * control-rate boundary and hand each sub-range to the voices.
 *
 * Real-time contract: all accessors are @c noexcept, allocation-free and
 * branch-light. Construct blocks on the stack in the audio callback.
 */
class AudioBlock
{
public:
    /// Construct an empty (0x0) block.
    constexpr AudioBlock() noexcept = default;

    /**
     * @param channels    Array of @p numChannels writable channel pointers.
     *                     Each must reference at least @p offset + @p numFrames floats.
     * @param numChannels  Number of channels the array holds.
     * @param numFrames    Number of sample frames per channel exposed by this view.
     * @param offset       Frame index within each channel where this view begins.
     */
    constexpr AudioBlock(float* const* channels,
                         std::size_t   numChannels,
                         std::size_t   numFrames,
                         std::size_t   offset = 0) noexcept
        : channels_(channels)
        , numChannels_(numChannels)
        , numFrames_(numFrames)
        , offset_(offset)
    {
    }

    /// @return Number of channels (0 for an empty block).
    [[nodiscard]] constexpr std::size_t numChannels() const noexcept { return numChannels_; }

    /// @return Number of sample frames per channel exposed by this view.
    [[nodiscard]] constexpr std::size_t numFrames() const noexcept { return numFrames_; }

    /// @return Frame offset of this view within the underlying storage.
    [[nodiscard]] constexpr std::size_t sampleOffset() const noexcept { return offset_; }

    /// @return true if there is no addressable audio.
    [[nodiscard]] constexpr bool isEmpty() const noexcept
    {
        return channels_ == nullptr || numChannels_ == 0 || numFrames_ == 0;
    }

    /**
     * @brief Writable pointer to a single channel's samples, already offset.
     * @param channel Channel index in [0, numChannels()).
     * @return Pointer to @c numFrames() contiguous floats (nullptr if out of range).
     */
    [[nodiscard]] constexpr float* channel(std::size_t channel) noexcept
    {
        return channel < numChannels_ ? channels_[channel] + offset_ : nullptr;
    }

    /// @copydoc channel(std::size_t)
    [[nodiscard]] constexpr const float* channel(std::size_t channel) const noexcept
    {
        return channel < numChannels_ ? channels_[channel] + offset_ : nullptr;
    }

    /**
     * @brief Create a sub-view starting @p startFrame frames into this view for
     *        @p frameCount frames. Aliases the same storage; no copy.
     *
     * Clamps to the current view bounds so the result never escapes valid memory.
     */
    [[nodiscard]] constexpr AudioBlock subBlock(std::size_t startFrame,
                                                std::size_t frameCount) const noexcept
    {
        const std::size_t clampedStart = startFrame < numFrames_ ? startFrame : numFrames_;
        const std::size_t remaining    = numFrames_ - clampedStart;
        const std::size_t clampedCount = frameCount < remaining ? frameCount : remaining;
        return AudioBlock(channels_, numChannels_, clampedCount, offset_ + clampedStart);
    }

private:
    float* const* channels_    = nullptr;
    std::size_t   numChannels_ = 0;
    std::size_t   numFrames_   = 0;
    std::size_t   offset_      = 0;
};

} // namespace caecilia::core

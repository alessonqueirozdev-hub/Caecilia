// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/IReverb.h"

/**
 * @file ReverbSendGate.h
 * @brief The producer-side decision of whether a reverb parameter set is worth
 *        sending to the audio thread at all.
 *
 * This lives in @c core rather than beside its caller in the plugin for one
 * reason: it is pure logic with a nasty edge case, and in the plugin it would be
 * untestable — that target links JUCE and the test suite deliberately does not.
 * The edge case is the settle counter, and "verify by automating a knob and
 * listening for a stall" is not a standard anyone can hold a change to.
 */

namespace caecilia::core
{

/**
 * @brief Decides which reverb parameter sets travel to the audio thread.
 *
 * A host automating a reverb knob delivers a new value every block. Every one of
 * them used to become a command on the engine's ring; the engine now discards the
 * inaudible ones cheaply, but generating them at all is pure traffic. This gate
 * drops them at the source.
 *
 * Three things get through:
 *   - a change large enough that the engine would rebuild coefficients for it
 *     (@ref reverbNeedsRecompute);
 *   - any change to @c mix or @c preDelayMs, which cost the engine nothing but
 *     still have to arrive;
 *   - a forced send, for a producer that knows something the diff cannot see —
 *     the console publishes a whole space preset including a bass bloom no host
 *     parameter covers.
 *
 * And one more, which is the whole reason this needed care: **the tail**. Every
 * sub-epsilon step the gate drops leaves the engine slightly behind, and at the
 * end of a sweep the difference simply stays — the knob reads 3.00 s and the
 * reverb sits at 2.99 forever. So once the incoming request has stopped moving
 * for @ref kSettleBlocks, the exact value goes out once and the gap closes.
 *
 * Real-time contract: every method is @c noexcept, allocation-free and branch-
 * light. Single-threaded; the gate belongs to whichever thread produces commands.
 */
class ReverbSendGate
{
public:
    /// Blocks of stillness before the exact value is flushed. At 256 frames and
    /// 48 kHz this is about 43 ms — long enough not to fire in the middle of a
    /// sweep, short enough that nobody hears the reverb arrive late.
    static constexpr int kSettleBlocks = 8;

    ReverbSendGate() noexcept = default;

    /// The rate the request will be interpreted at, so the gate clamps it exactly
    /// as the reverb will. Getting this wrong makes the gate compare a raw value
    /// against a clamped one; see @ref clampReverbParams.
    void setSampleRate(SampleRate sampleRate) noexcept { sampleRate_ = sampleRate; }

    /// Forget everything, so the next request is sent whatever it is. Call when
    /// the engine has been re-prepared and its state is no longer known.
    void reset() noexcept
    {
        lastSent_     = ReverbParams{};
        prevRequest_  = ReverbParams{};
        stillFor_     = 0;
        primed_       = false;
    }

    /**
     * @brief Adopt @p params as what the engine is believed to hold.
     *
     * For a producer that hands the engine a set through some other route, or —
     * in practice — for the console, which publishes a whole space preset. Pair it
     * with @c force on the same block: adopting a baseline the engine does NOT
     * actually hold is how a space change gets silently swallowed.
     */
    void adoptBaseline(const ReverbParams& params) noexcept
    {
        lastSent_    = params;
        prevRequest_ = params;
        stillFor_    = 0;
        primed_      = true;
    }

    /**
     * @brief Should @p request be sent, and record that it was.
     * @param request The parameter set the producer would like the engine to hold.
     * @param force   Send regardless of the diff (see the class comment).
     * @return true if the caller should enqueue @p request.
     *
     * Call exactly once per block: the settle counter counts calls.
     */
    [[nodiscard]] bool shouldSend(const ReverbParams& request, bool force = false) noexcept
    {
        // Stillness is measured on the INCOMING request, not on what was sent, so
        // a slow sweep keeps resetting it and only a genuine pause lets it run up.
        stillFor_    = same(request, prevRequest_) ? stillFor_ + 1 : 0;
        prevRequest_ = request;

        const bool audible    = !primed_ || reverbNeedsRecompute(lastSent_, request, sampleRate_);
        const bool cheapMoved = !primed_ || request.mix != lastSent_.mix
                                         || request.preDelayMs != lastSent_.preDelayMs;
        const bool settled    = primed_ && stillFor_ >= kSettleBlocks
                                        && !same(request, lastSent_);

        if (!(force || audible || cheapMoved || settled))
            return false;

        lastSent_ = request;
        primed_   = true;
        return true;
    }

    /// @return The set most recently let through. Meaningless until the first
    ///         @ref shouldSend returns true.
    [[nodiscard]] const ReverbParams& lastSent() const noexcept { return lastSent_; }

    /// @return true once anything has been sent, i.e. @ref lastSent is meaningful.
    [[nodiscard]] bool primed() const noexcept { return primed_; }

    /// @return Blocks the incoming request has been unchanged for. Diagnostic.
    [[nodiscard]] int stillFor() const noexcept { return stillFor_; }

private:
    /// Exact, field-by-field. Not an epsilon: this answers "is this the same
    /// request as last block?", which is a question about the producer's own
    /// history rather than about audibility.
    [[nodiscard]] static bool same(const ReverbParams& a, const ReverbParams& b) noexcept
    {
        return a.mix == b.mix && a.decaySec == b.decaySec && a.preDelayMs == b.preDelayMs
            && a.dampingHz == b.dampingHz && a.widthNorm == b.widthNorm
            && a.bassBloom == b.bassBloom;
    }

    SampleRate   sampleRate_ = 48000.0;
    ReverbParams lastSent_{};
    ReverbParams prevRequest_{};
    int          stillFor_ = 0;
    bool         primed_   = false;
};

} // namespace caecilia::core

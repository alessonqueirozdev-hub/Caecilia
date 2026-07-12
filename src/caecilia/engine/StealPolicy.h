/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/engine/VoiceHandle.h"

#include <cstdint>

namespace caecilia::core::engine
{

/**
 * @brief How the pool picks a victim when polyphony exceeds capacity or the
 *        DeadlineBudget is spent.
 *
 * The default is @ref Quietest: stealing the least audible voice makes the
 * reallocation inaudible, which is exactly the "worst-case tutti degrades to
 * subtle thinning, never an xrun" guarantee.
 */
enum class StealPolicy : std::uint8_t
{
    Quietest,   ///< Steal the voice with the lowest current output level (default).
    Oldest,     ///< Steal the earliest-started voice (classic round-robin).
    LowestTier, ///< Steal the cheapest/lowest-quality voice first.
    Newest      ///< Steal the most recently started voice (rarely desirable).
};

/**
 * @brief The outcome of a stealing/degradation decision.
 *
 * A victim may be reclaimed outright, or — cheaper and less audible — merely
 * demoted to a lower @c VoiceTier so it keeps sounding at reduced cost. When
 * @ref victim is invalid there was nothing worth stealing this block.
 */
struct StealDecision
{
    VoiceHandle victim        = VoiceHandle::invalid(); ///< Slot to reclaim/demote.
    bool        demoteInsteadOfSteal = false;           ///< Prefer tier demotion over reclaim.
};

} // namespace caecilia::core::engine

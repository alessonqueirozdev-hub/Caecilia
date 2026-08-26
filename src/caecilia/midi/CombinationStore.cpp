// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/midi/CombinationStore.h"

namespace caecilia::midi
{

void CombinationStore::captureGeneral(std::size_t index, const Combination& combo) noexcept
{
    if (index < kNumGenerals)
        generals_[index] = combo;
}

void CombinationStore::captureDivisional(core::DivisionId division, std::size_t index,
                                         const Combination& combo) noexcept
{
    if (divisionInRange(division) && index < kNumDivisionalsPerDivision)
        divisionals_[division.value][index] = combo;
}

void CombinationStore::clearGeneral(std::size_t index) noexcept
{
    if (index < kNumGenerals)
        generals_[index] = Combination{};
}

void CombinationStore::clearDivisional(core::DivisionId division, std::size_t index) noexcept
{
    if (divisionInRange(division) && index < kNumDivisionalsPerDivision)
        divisionals_[division.value][index] = Combination{};
}

void CombinationStore::clearAll() noexcept
{
    generals_ = {};
    divisionals_ = {};
}

Combination CombinationStore::recallGeneral(std::size_t index) const noexcept
{
    if (index < kNumGenerals)
        return generals_[index];
    return Combination{};
}

Combination CombinationStore::recallDivisional(core::DivisionId division,
                                               std::size_t index) const noexcept
{
    if (divisionInRange(division) && index < kNumDivisionalsPerDivision)
        return divisionals_[division.value][index];
    return Combination{};
}

} // namespace caecilia::midi

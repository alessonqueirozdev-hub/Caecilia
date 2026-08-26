// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngagedRankTable.h"
#include "caecilia/synthesis/RankVoicing.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <cstddef>
#include <memory>
#include <vector>

/**
 * @file RankTable.h
 * @brief A minimal engaged-rank table, for tests that just need the engine to
 *        sound something.
 *
 * One voice per (rank, note) means a note-on fans out across whatever ranks are
 * drawn — so an engine with nothing drawn is correctly silent, and a test rig that
 * binds voices and sends a note-on without saying what is drawn gets silence too.
 *
 * That is the instrument behaving properly, but it does mean a rig has to declare
 * a registration. This is the smallest honest one: a single rank per division,
 * with a spectrum simple enough that a test can reason about what it hears.
 *
 * The voicings are OWNED here and referenced by the table, so this object must
 * outlive the engine that reads it.
 */

namespace caecilia::tests
{

class RankTable
{
public:
    /// @param divisions How many divisions get a rank (division ids 0..n-1).
    /// @param harmonics Partials per rank.
    explicit RankTable(std::size_t divisions = 1, int harmonics = 4)
    {
        voicings_.reserve(divisions);
        for (std::size_t d = 0; d < divisions; ++d)
        {
            auto v = std::make_unique<synth::RankVoicing>();
            v->stop     = core::StopId{ static_cast<std::uint16_t>(d) };
            v->division = core::DivisionId{ static_cast<std::uint16_t>(d) };
            v->family   = core::TonalFamily::Principal;
            v->footage  = core::footage::kEight;

            for (int n = 1; n <= harmonics; ++n)
            {
                synth::PartialTrack t;
                t.ratioToF0 = static_cast<float>(n);
                t.ampDb     = -6.0f * static_cast<float>(n - 1);
                t.seed      = 0x3100u + static_cast<std::uint32_t>(d * 16 + n);
                v->spectrum.partials.push_back(t);
            }
            v->spectrum.fundamentalHz = 220.0f;

            table_.ranks[d] = core::engine::EngagedRank{ v.get(), v->stop, v->division };
            voicings_.push_back(std::move(v));
        }
        table_.count = divisions;
        table_.epoch = 1;
    }

    /// Hand the table to an engine. The engine picks it up at its next block.
    void publishTo(core::engine::AudioEngine& engine) const noexcept
    {
        engine.setEngagedRanks(table_);
    }

    [[nodiscard]] const core::engine::EngagedRankTable& table() const noexcept
    {
        return table_;
    }

private:
    std::vector<std::unique_ptr<synth::RankVoicing>> voicings_;
    core::engine::EngagedRankTable                   table_{};
};

} // namespace caecilia::tests

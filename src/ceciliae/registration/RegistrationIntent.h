/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/registration/RegistrationState.h"
#include "ceciliae/registration/Selector.h"
#include "ceciliae/registration/StopSet.h"

#include <string>
#include <vector>

namespace ceciliae::model { class Organ; }

namespace ceciliae::registration
{

/**
 * @brief A portable, semantic description of a registration — the marquee
 *        deliverable: it transfers across *different* organs, not just the one
 *        it was captured on.
 *
 * Where a @c RegistrationState is a set of concrete @c StopId (meaningful only
 * for one loaded organ), a RegistrationIntent is a list of semantic clauses
 * ("a foundation 8' in the Great", "the reed chorus in the Swell") expressed as
 * @c Selector s. Re-resolving those clauses against a target @c OrganSpec yields
 * the closest available registration on that instrument, with a confidence score
 * and graceful degradation when an exact match is unavailable.
 */
struct RegistrationIntent
{
    /// One semantic clause of the intent.
    struct Item
    {
        Selector    selector;         ///< The semantic query to satisfy.
        std::string label;            ///< Human description, e.g. "Great Principal 8'".
        float       importance = 1.0f;///< Weight for confidence scoring (0..1].
        bool        essential  = false;///< If true, a miss heavily penalises confidence.
    };

    std::string       sourceOrganName; ///< Organ the intent was captured on (provenance).
    std::vector<Item> items;

    [[nodiscard]] bool empty() const noexcept { return items.empty(); }

    /**
     * @brief Capture the current registration as a portable intent.
     * @param spec  The organ the state belongs to.
     * @param state The registration to describe.
     * @return An intent whose clauses reproduce @p state on a similar organ.
     *
     * Each engaged stop becomes a `family & pitch & div` clause, so recalling the
     * intent on another organ draws the semantically-nearest stops. Off-thread.
     */
    [[nodiscard]] static RegistrationIntent captureFrom(const model::Organ& spec,
                                                        const RegistrationState& state);
};

/**
 * @brief The outcome of porting a @c RegistrationIntent onto an organ.
 *
 * Carries the resolved concrete stops, an overall @ref confidence in [0, 1], and
 * a per-clause breakdown so the UI can show what transferred cleanly and what
 * degraded.
 */
struct PortResult
{
    /// Per-clause resolution detail.
    struct ItemOutcome
    {
        std::string label;         ///< Mirrors the source @c Item::label.
        std::size_t matchedCount = 0; ///< Stops the clause resolved to on the target.
        bool        essential = false;
        float       score = 0.0f;  ///< This clause's contribution in [0, 1].
    };

    StopSet                  resolved;      ///< The concrete registration to apply.
    float                    confidence = 0.0f; ///< Weighted overall match quality.
    std::vector<ItemOutcome> outcomes;      ///< One entry per source clause.

    /// @return true if every essential clause matched at least one stop.
    [[nodiscard]] bool allEssentialsMatched() const noexcept;
};

/**
 * @brief Port @p intent onto @p targetSpec, resolving every clause.
 * @param intent     The semantic registration to transfer.
 * @param targetSpec The organ to resolve against.
 * @return A @c PortResult with the resolved stops and a confidence score.
 *
 * Off the audio thread only. Clauses resolve against an empty state (intent is
 * absolute, not relative to a live registration).
 */
[[nodiscard]] PortResult port(const RegistrationIntent& intent, const model::Organ& targetSpec);

} // namespace ceciliae::registration

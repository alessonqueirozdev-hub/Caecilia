// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace caecilia::model { class Organ; }

/**
 * @file FactoryGenerals.h
 * @brief The combination memory an instrument ships with.
 *
 * A general piston stores a registration. These are the ones already drawn on the
 * console's jamb, written once here as selector expressions rather than twice —
 * once in C++ and once in the WebView's JavaScript — because two copies of a
 * musical decision drift, and the one that drifts is always the one nobody was
 * looking at.
 *
 * They are EXPRESSIONS, not stop lists, so they resolve against whatever organ is
 * loaded: a Tutti is every stop this instrument has, not every stop the demo organ
 * happened to have when the table was written.
 *
 * This is the first real caller of @ref SelectorParser, which has been implemented
 * and tested since the registration module was written and consulted by nothing.
 */
namespace caecilia::registration
{

/// One factory piston: what the console prints on it, and what it draws.
struct FactoryGeneral
{
    std::string_view tag;        ///< Console legend ("pp", "f", "T").
    std::string_view expression; ///< Selector expression in the shared grammar.

    /// True if this piston draws its expression PLUS everything below it in the row.
    ///
    /// That is what a crescendo is — raising the Walze never takes a stop away —
    /// and writing the row as independent full registrations instead gives every
    /// level its own chance to silently drop something the level below it drew.
    bool cumulative = true;
};

/**
 * @brief The factory pistons, softest to loudest.
 *
 * The order is the point as much as the contents: walking the pistons upward has
 * to read as a crescendo, which is what makes a piston sequence musical rather
 * than eight arbitrary presets. @ref FactoryGeneralsTest asserts that the stop
 * count really does climb, on the organ that is actually loaded.
 */
[[nodiscard]] std::span<const FactoryGeneral> factoryGenerals() noexcept;

/**
 * @brief Resolve a selector expression to a drawn-stop mask.
 *
 * @param organ      The instrument to resolve against.
 * @param expression Selector text in the @ref SelectorParser grammar.
 * @return A mask keyed by @c StopId::value, or 0 if the expression does not parse
 *         or matches nothing on this organ. Both cases mean the same thing to a
 *         piston: leave the slot empty rather than let it draw silence.
 *
 * Off the audio thread: parsing allocates.
 */
[[nodiscard]] std::uint64_t resolveSelectorMask(const model::Organ& organ,
                                                std::string_view    expression);

/**
 * @brief Resolve the whole factory row, applying the cumulative rule.
 *
 * @param organ The instrument to resolve against.
 * @param out   Receives one mask per piston, in row order. Slots past the row's
 *              length are left alone; a piston that matches nothing is 0.
 * @return How many pistons came out non-empty.
 *
 * Off the audio thread.
 */
std::size_t resolveFactoryGenerals(const model::Organ&      organ,
                                   std::span<std::uint64_t> out);

} // namespace caecilia::registration

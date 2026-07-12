/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/registration/RegistrationState.h"
#include "ceciliae/registration/StopQuery.h"
#include "ceciliae/registration/StopSet.h"

#include <vector>

namespace ceciliae::model { class Organ; }

namespace ceciliae::registration
{

/**
 * @brief The INTENT layer: a lazily-evaluated tree of @c StopQuery atoms
 *        combined with set algebra (union `|`, intersection `&`, difference `-`,
 *        complement `!`).
 *
 * A Selector is the first-class, composable expression the whole product speaks:
 * it is produced by the shared @c SelectorParser (from OSC / JSON-RPC /
 * MIDI-learn / the UI omnibar), stored inside a @c RegistrationCommand, and
 * resolved on demand against the live @c OrganSpec + @c RegistrationState. Nothing
 * is precomputed, so a stored Selector keeps meaning as the registration or even
 * the loaded organ changes.
 *
 * The tree is value-semantic and self-contained (children held by value), which
 * keeps commands trivially copyable-ish and safe to snapshot into history.
 */
class Selector
{
public:
    /// The node kind — either a leaf predicate or an n-ary/unary combinator.
    enum class Op
    {
        Atom,       ///< A leaf @c StopQuery.
        Union,      ///< Set union of all children (`a | b`).
        Intersect,  ///< Set intersection of all children (`a & b`).
        Difference, ///< First child minus the rest (`a - b`).
        Complement  ///< Universe minus the single child (`!a`).
    };

    Selector() = default; ///< Empty universal atom (matches everything).

    // --- Factories ----------------------------------------------------------
    [[nodiscard]] static Selector atom(StopQuery query);
    [[nodiscard]] static Selector combine(Op op, std::vector<Selector> operands);
    [[nodiscard]] static Selector complementOf(Selector inner);

    // --- Observers ----------------------------------------------------------
    [[nodiscard]] Op op() const noexcept { return op_; }
    [[nodiscard]] const StopQuery& query() const noexcept { return query_; }
    [[nodiscard]] const std::vector<Selector>& operands() const noexcept { return operands_; }
    [[nodiscard]] bool isAtom() const noexcept { return op_ == Op::Atom; }

    /**
     * @brief Resolve this selector into the matching stop set.
     * @param spec  The loaded organ.
     * @param state The live registration (for @c engaged predicates / complement
     *              universe).
     * @return The resolved @c StopSet.
     *
     * Off the audio thread only (recursive, allocates). The @c Complement
     * universe is "every stop in @p spec".
     */
    [[nodiscard]] StopSet resolve(const model::Organ& spec, const RegistrationState& state) const;

    /// @return every stop in @p spec, as a set (the complement universe).
    [[nodiscard]] static StopSet universe(const model::Organ& spec);

private:
    Op                    op_ = Op::Atom;
    StopQuery             query_{};    ///< Meaningful only when op_ == Atom.
    std::vector<Selector> operands_;   ///< Meaningful for combinators.
};

// --- Ergonomic operators mirroring the grammar -----------------------------
[[nodiscard]] Selector operator|(Selector a, Selector b); ///< union
[[nodiscard]] Selector operator&(Selector a, Selector b); ///< intersection
[[nodiscard]] Selector operator-(Selector a, Selector b); ///< difference
[[nodiscard]] Selector operator!(Selector a);             ///< complement

} // namespace ceciliae::registration

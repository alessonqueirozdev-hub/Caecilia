/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/registration/Selector.h"

#include <string>
#include <string_view>

namespace ceciliae::registration
{

/**
 * @brief Version of the selector grammar, treated as a public contract.
 *
 * External scripts (OSC, JSON-RPC, MIDI-learn) depend on the grammar, so it
 * carries an explicit version from day one. Bump @c minor for backward-
 * compatible additions, @c major for a breaking change.
 */
struct GrammarVersion
{
    int major = 1;
    int minor = 0;
    friend constexpr bool operator==(GrammarVersion, GrammarVersion) noexcept = default;
};

/**
 * @brief The single, shared parser that turns a textual selector expression
 *        into a @c Selector tree.
 *
 * ## Grammar (v1.0)
 * @code
 *   expression   := union
 *   union        := intersection ( '|' intersection )*
 *   intersection := unary ( ( '&' | '-' ) unary )*      // '-' is set difference
 *   unary        := '!' unary | primary                 // '!' is complement
 *   primary      := '(' expression ')' | predicate | keyword
 *   predicate    := key ':' value
 *   key          := 'family' | 'role' | 'class'
 *                 | 'pitch' | 'footage' | 'div' | 'division' | 'name'
 *   keyword      := 'engaged' | 'all' | '*'
 * @endcode
 *
 * Footage values are given as feet: an integer (`pitch:8`, `pitch:16`) or an
 * exact improper fraction (`pitch:8/3` == 2 2/3', `pitch:16/3` == 5 1/3'),
 * mirroring the @c core::Footage representation so text and code agree exactly.
 * String values may be quoted (`name:"open diapason"`).
 *
 * This grammar is reused verbatim by the control, midi and ui modules, so there
 * is zero API drift between how a stop is selected from a script, a learned MIDI
 * control, and the omnibar. The parser is off-thread, allocation-tolerant and
 * exception-free (errors are returned, never thrown).
 */
class SelectorParser
{
public:
    /// The outcome of a parse: a tree on success, or a positioned error.
    struct Result
    {
        bool        ok = false;   ///< true if @ref selector is valid.
        Selector    selector{};   ///< The parsed tree (universal atom on failure).
        std::string error;        ///< Human-readable diagnostic (empty on success).
        std::size_t errorPos = 0; ///< 0-based byte offset of the error in the input.

        explicit operator bool() const noexcept { return ok; }
    };

    SelectorParser() = default;

    /// @return the grammar version this parser implements.
    [[nodiscard]] static constexpr GrammarVersion version() noexcept { return {1, 0}; }

    /**
     * @brief Parse @p text into a @c Selector.
     * @param text A selector expression, e.g. `family:reed & pitch:8 & div:swell`.
     * @return A @ref Result; on failure @c ok is false and @c error/@c errorPos
     *         describe the problem. Never throws.
     *
     * Off the audio thread only. An empty / whitespace-only input parses to the
     * universal atom (matches every stop).
     */
    [[nodiscard]] Result parse(std::string_view text) const;
};

/// @return a printable form of the current grammar version, e.g. "1.0".
[[nodiscard]] std::string grammarVersionString();

} // namespace ceciliae::registration

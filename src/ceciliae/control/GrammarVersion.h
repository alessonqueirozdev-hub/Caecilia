/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <string>

namespace ceciliae::control
{

/**
 * @brief The versioned PUBLIC CONTRACT of the control surface.
 *
 * External automation — a Talon voice grammar, a shell script, a custom
 * hardware console — binds to two things that must never silently drift:
 *
 *  1. the textual **selector grammar** (`family:reed & pitch:8 & div:swell`),
 *     owned and parsed by @c registration::SelectorParser; and
 *  2. the **control protocol**: the set of @c ControlOpcode verbs, the OSC
 *     address space, and the JSON-RPC method names that carry those verbs.
 *
 * Both are versioned here from day one so a client can negotiate compatibility
 * during its handshake (see @c ICommandSink::grammarVersion). The selector
 * grammar version deliberately mirrors @c registration::GrammarVersion (v1.0);
 * @ref protocol is bumped independently for additive control-surface changes
 * (a new opcode, a new OSC address) that do not touch the selector grammar.
 *
 * Semantics:
 *  - bump @ref major for a **breaking** selector-grammar change;
 *  - bump @ref minor for a **backward-compatible** selector-grammar addition;
 *  - bump @ref protocol for a **backward-compatible** control-surface addition.
 */
struct GrammarVersion
{
    int major    = 1; ///< Selector-grammar major (breaking). Mirrors registration.
    int minor    = 0; ///< Selector-grammar minor (additive).
    int protocol = 1; ///< Control-surface protocol revision (opcodes/addresses/methods).

    friend constexpr bool operator==(GrammarVersion, GrammarVersion) noexcept = default;

    /**
     * @brief Can a server at @c *this satisfy a client that was built against
     *        @p client?
     * @return true iff the major grammar version matches exactly and the server
     *         is at least as new in both minor and protocol revision.
     *
     * A client only relies on features that existed at its own version, so a
     * newer (additive) server stays compatible; a different major does not.
     */
    [[nodiscard]] constexpr bool supports(const GrammarVersion& client) const noexcept
    {
        return major == client.major
               && minor >= client.minor
               && protocol >= client.protocol;
    }

    /// @return a printable form, e.g. `"1.0 (protocol r1)"`. Off-thread only.
    [[nodiscard]] std::string toString() const
    {
        return std::to_string(major) + '.' + std::to_string(minor)
               + " (protocol r" + std::to_string(protocol) + ')';
    }
};

/// The control-surface version this build of Ceciliae speaks.
inline constexpr GrammarVersion kGrammarVersion{1, 0, 1};

} // namespace ceciliae::control

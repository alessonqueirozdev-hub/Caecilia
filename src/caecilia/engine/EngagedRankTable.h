// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <array>
#include <cstddef>

/**
 * @file EngagedRankTable.h
 * @brief Which ranks a key should sound, published to the audio thread.
 *
 * One voice per (rank, note) means a note-on is not one acquisition but several,
 * and the engine has to know how many before it takes the first. This is what it
 * reads: a small, trivially copyable table it can hold in a TripleBuffer and
 * consult with no lock, no allocation and no knowledge of the model layer.
 */

namespace caecilia::core::engine
{

/// One engaged rank, as the audio thread needs to see it.
struct EngagedRank
{
    /// Opaque handle the owner resolves back to a full voicing. The engine never
    /// dereferences it; it hands it to the voice, which knows what it means.
    const void*    voicing  = nullptr;
    StopId         stop{};      ///< Also the rank id stamped into the PipeId.
    DivisionId     division{};  ///< Which keyboard sounds it.

    /// How much wind one pipe of this rank draws, at middle C, relative to an 8'.
    ///
    /// The rank's own half of the demand figure; the note's half is applied by the
    /// engine, which knows the note. It travels HERE rather than being read off
    /// @ref voicing because that pointer is deliberately opaque -- the engine hands
    /// it straight back to the voice and never looks inside, which is what keeps the
    /// engine module from depending on the synthesis one.
    ///
    /// Last in the struct, so a table built by hand in a test still initialises as
    /// {voicing, stop, division} and simply gets the default. 1.0 means "an 8' rank,
    /// or unknown", which is the right answer there.
    float          windFlow = 1.0f;
};

/**
 * @brief The engaged ranks of the current registration.
 *
 * Trivially copyable and fixed-size on purpose: it crosses the message/audio
 * boundary by value through a TripleBuffer, so it may not own anything.
 *
 * `epoch` increments on every publication. A voice caches which rank it is
 * currently voiced as; comparing epochs is how it knows whether that is still
 * true without re-seeding on every note.
 */
struct EngagedRankTable
{
    /// Sized to the registration mask, which is what bounds a registration.
    static constexpr std::size_t kMaxRanks = 64;

    std::array<EngagedRank, kMaxRanks> ranks{};
    std::size_t                        count = 0;
    std::uint32_t                      epoch = 0;
};

} // namespace caecilia::core::engine

// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/registration/FactoryGenerals.h"

#include "caecilia/registration/RegistrationState.h"
#include "caecilia/registration/Selector.h"
#include "caecilia/registration/SelectorParser.h"
#include "caecilia/registration/StopSet.h"

#include <array>

namespace caecilia::registration
{
namespace
{
/// The eight the console's jamb already showed, in the shared grammar.
///
/// The crescendo pistons say what they ADD, not what they draw, because that is
/// what a crescendo is: raising the Walze never takes a stop away. Writing them as
/// six independent full registrations instead means six chances for one of them to
/// silently drop something the one below it drew -- which is exactly what happened
/// on the first attempt, where "ff" specified the chorus reeds and thereby retired
/// the colour reed "f" had just brought in.
///
/// The build order is a French romantic console's: foundations first and filling
/// out by family, then the chorus by pitch, and only at the top the reeds and the
/// mixture crown. A plenum whose crown arrives before its foundations is a plenum
/// standing on nothing.
constexpr std::array<FactoryGeneral, 8> kFactory{ {
    { "pp", "family:flute & pitch:8 & !div:pedal",             true  },
    { "p",  "family:string",                                   true  },
    { "mp", "family:principal & pitch:8",                      true  },
    { "mf", "family:principal & pitch:4 | family:flute & pitch:4", true },
    { "f",  "pitch:2 | family:reed & role:color",              true  },
    { "ff", "family:mixture | family:reed",                    true  },
    { "T",  "*",                                               true  },
    // Not part of the crescendo, and not cumulative: a solo registration is a
    // different gesture, which is why a console puts it at the end of the row
    // rather than in sequence. `name:` matches a substring, so an organ without a
    // Cornet simply contributes its reeds.
    { "So", "family:reed | name:cornet",                       false },
} };
} // namespace

std::span<const FactoryGeneral> factoryGenerals() noexcept
{
    return { kFactory.data(), kFactory.size() };
}

std::uint64_t resolveSelectorMask(const model::Organ& organ, std::string_view expression)
{
    const SelectorParser         parser;
    const SelectorParser::Result parsed = parser.parse(expression);
    if (! parsed.ok)
        return 0;

    // Resolved against an EMPTY registration on purpose. A general says what to
    // draw, not what to add to whatever happened to be out: `engaged` inside a
    // piston's expression would make the piston mean something different every
    // time it fired.
    const RegistrationState nothingDrawn;

    std::uint64_t bits = 0;
    for (const core::StopId id : parsed.selector.resolve(organ, nothingDrawn).ids())
        if (id.value < StopSet::kMaskCapacity)
            bits |= (std::uint64_t{ 1 } << id.value);
    return bits;
}

std::size_t resolveFactoryGenerals(const model::Organ&      organ,
                                   std::span<std::uint64_t> out)
{
    const std::span<const FactoryGeneral> row = factoryGenerals();

    std::size_t   written    = 0;
    std::uint64_t accumulated = 0;

    for (std::size_t i = 0; i < row.size() && i < out.size(); ++i)
    {
        const std::uint64_t own = resolveSelectorMask(organ, row[i].expression);

        // A cumulative piston carries everything below it. Note that the
        // accumulator advances even when a piston's own expression matched nothing
        // -- an organ with no strings must not break the pistons above "p".
        if (row[i].cumulative)
        {
            accumulated |= own;
            out[i] = accumulated;
        }
        else
        {
            out[i] = own;
        }

        if (out[i] != 0)
            ++written;
    }
    return written;
}

} // namespace caecilia::registration

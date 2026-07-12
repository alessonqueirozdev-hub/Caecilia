/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/model/Stop.h"

#include <cstddef>

namespace ceciliae::model
{
namespace
{

/// Small Roman-numeral formatter for mixture rank counts (1..39 is ample).
std::string toRoman(std::size_t value)
{
    static constexpr struct
    {
        std::size_t v;
        const char* s;
    } table[] = {
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"},
    };

    std::string out;
    for (const auto& entry : table)
    {
        while (value >= entry.v)
        {
            out += entry.s;
            value -= entry.v;
        }
    }
    return out;
}

} // namespace

std::string footageLabel(core::Footage footage)
{
    const std::int32_t num = footage.num;
    const std::int32_t den = footage.den;
    if (den <= 0)
        return "8'";

    const std::int32_t whole     = num / den;
    const std::int32_t remainder = num % den;

    std::string out = std::to_string(whole);
    if (remainder != 0)
    {
        out += ' ';
        out += std::to_string(remainder);
        out += '/';
        out += std::to_string(den);
    }
    out += '\''; // foot mark
    return out;
}

std::string Stop::displayName() const
{
    std::string out = name_;
    out += ' ';
    if (isCompound())
        out += toRoman(mixtureComposition_.size()); // e.g. "Mixture IV"
    else
        out += footageLabel(footage_);              // e.g. "Principal 8'"
    return out;
}

} // namespace ceciliae::model

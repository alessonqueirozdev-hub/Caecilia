/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "common/CliArgs.h"

#include <cstdlib>
#include <string>

namespace caecilia::tools
{

namespace
{
/// @return @p token with a leading "--" (or "-") stripped, if present.
std::string stripDashes(const std::string& token)
{
    std::size_t i = 0;
    while (i < token.size() && token[i] == '-')
        ++i;
    return token.substr(i);
}

/// @return true if @p token looks like an option (begins with "--").
bool isOption(const std::string& token)
{
    return token.size() >= 2 && token[0] == '-' && token[1] == '-';
}
} // namespace

CliArgs::CliArgs(int argc, char** argv)
{
    if (argc > 0 && argv[0] != nullptr)
        program_ = argv[0];

    for (int i = 1; i < argc; ++i)
    {
        const std::string token = argv[i] != nullptr ? argv[i] : "";
        if (token.empty())
            continue;

        if (isOption(token))
        {
            const std::string key = stripDashes(token);

            // Support "--key=value" as well as "--key value".
            if (const auto eq = key.find('='); eq != std::string::npos)
            {
                options_[key.substr(0, eq)] = key.substr(eq + 1);
                continue;
            }

            // Consume the next token as the value unless it is itself an option.
            if (i + 1 < argc && argv[i + 1] != nullptr && !isOption(argv[i + 1]))
            {
                options_[key] = argv[++i];
            }
            else
            {
                options_[key] = ""; // bare flag
            }
        }
        else
        {
            positionals_.push_back(token);
        }
    }
}

bool CliArgs::has(std::string_view key) const
{
    return options_.find(std::string(key)) != options_.end();
}

std::string CliArgs::value(std::string_view key, std::string_view fallback) const
{
    const auto it = options_.find(std::string(key));
    if (it == options_.end() || it->second.empty())
        return std::string(fallback);
    return it->second;
}

std::optional<double> CliArgs::number(std::string_view key) const
{
    const auto it = options_.find(std::string(key));
    if (it == options_.end() || it->second.empty())
        return std::nullopt;

    try
    {
        std::size_t consumed = 0;
        const double v = std::stod(it->second, &consumed);
        if (consumed == 0)
            return std::nullopt;
        return v;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<long long> CliArgs::integer(std::string_view key) const
{
    const auto it = options_.find(std::string(key));
    if (it == options_.end() || it->second.empty())
        return std::nullopt;

    try
    {
        std::size_t consumed = 0;
        const long long v = std::stoll(it->second, &consumed, 10);
        if (consumed == 0)
            return std::nullopt;
        return v;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

} // namespace caecilia::tools

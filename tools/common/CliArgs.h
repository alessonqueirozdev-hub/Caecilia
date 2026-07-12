/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ceciliae::tools
{

/**
 * @brief Minimal, dependency-free command-line argument parser shared by every
 *        offline tool.
 *
 * Grammar (deliberately tiny; the tools are internal, not a public API):
 *  - @c --key value  : an option with a value  (value must not start with "--").
 *  - @c --flag       : a boolean flag          (no following value).
 *  - @c token        : a positional argument   (anything not starting with "--").
 *
 * This runs once at process start, off any audio thread, so it is free to
 * allocate and use the standard library liberally.
 *
 * @code
 *   ceciliae::tools::CliArgs args(argc, argv);
 *   if (args.has("help")) { printUsage(); return 0; }
 *   const std::string in = args.value("input");
 *   const auto fft = args.integer("fft-size").value_or(4096);
 * @endcode
 */
class CliArgs
{
public:
    /// Parse the raw @p argc / @p argv pair as supplied to @c main().
    CliArgs(int argc, char** argv);

    /// @return The program name (argv[0]) as invoked.
    [[nodiscard]] std::string_view program() const noexcept { return program_; }

    /// @return true if @p key was present (as either a flag or a valued option).
    [[nodiscard]] bool has(std::string_view key) const;

    /**
     * @brief Value of a valued option.
     * @param key      Option name, without the leading "--".
     * @param fallback Returned when the option is absent or was given as a flag.
     */
    [[nodiscard]] std::string value(std::string_view key,
                                    std::string_view fallback = {}) const;

    /// @return The option parsed as a floating-point number, if convertible.
    [[nodiscard]] std::optional<double> number(std::string_view key) const;

    /// @return The option parsed as a signed integer, if convertible.
    [[nodiscard]] std::optional<long long> integer(std::string_view key) const;

    /// @return The positional (non-option) arguments, in order.
    [[nodiscard]] const std::vector<std::string>& positionals() const noexcept
    {
        return positionals_;
    }

private:
    std::string                        program_;
    std::vector<std::string>           positionals_;
    std::map<std::string, std::string> options_; ///< key -> value ("" for a flag).
};

} // namespace ceciliae::tools

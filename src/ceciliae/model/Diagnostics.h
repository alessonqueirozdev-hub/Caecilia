/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ceciliae::model
{

/// Severity of a single load/compile diagnostic.
enum class DiagnosticSeverity
{
    Info,    ///< Advisory only; the organ is fully usable.
    Warning, ///< Recoverable; a default was substituted or a field ignored.
    Error    ///< Fatal for the affected organ; compilation must not yield a spec.
};

/// One human-readable message produced while parsing or compiling an organ file.
struct Diagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::string        message;              ///< What happened, in plain English.
    std::string        context;              ///< Where (e.g. "rank[3] 'Principal 8'").
};

/**
 * @brief An ordered collection of diagnostics with convenience predicates.
 *
 * This is an off-thread reporting aid (it allocates); it never appears on the
 * render path. Loaders accumulate into it and callers inspect @ref hasErrors to
 * decide whether the produced organ is trustworthy.
 */
class LoadDiagnostics
{
public:
    void add(DiagnosticSeverity severity, std::string message, std::string context = {})
    {
        entries_.push_back(Diagnostic{severity, std::move(message), std::move(context)});
    }

    void info(std::string message, std::string context = {})
    {
        add(DiagnosticSeverity::Info, std::move(message), std::move(context));
    }
    void warning(std::string message, std::string context = {})
    {
        add(DiagnosticSeverity::Warning, std::move(message), std::move(context));
    }
    void error(std::string message, std::string context = {})
    {
        add(DiagnosticSeverity::Error, std::move(message), std::move(context));
    }

    [[nodiscard]] const std::vector<Diagnostic>& entries() const noexcept { return entries_; }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// @return true if any diagnostic is an Error.
    [[nodiscard]] bool hasErrors() const noexcept
    {
        for (const auto& d : entries_)
            if (d.severity == DiagnosticSeverity::Error)
                return true;
        return false;
    }

    void merge(const LoadDiagnostics& other)
    {
        entries_.insert(entries_.end(), other.entries_.begin(), other.entries_.end());
    }

private:
    std::vector<Diagnostic> entries_;
};

} // namespace ceciliae::model

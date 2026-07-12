/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/model/Diagnostics.h"
#include "ceciliae/model/Organ.h"
#include "ceciliae/model/OrganDefinition.h"

#include <optional>
#include <string>
#include <string_view>

namespace ceciliae::model
{

/// Surface syntax of a human-readable organ document.
enum class OrganFileFormat
{
    Auto, ///< Sniff from the content (leading '{' => JSON, otherwise YAML).
    Json, ///< Ceciliae organ document in JSON.
    Yaml  ///< Ceciliae organ document in YAML.
};

/// Outcome of parsing document text into an @c OrganDefinition.
struct ParseResult
{
    std::optional<OrganDefinition> definition; ///< Present iff parsing succeeded.
    LoadDiagnostics                diagnostics;

    [[nodiscard]] bool ok() const noexcept { return definition.has_value(); }
};

/// Outcome of compiling an @c OrganDefinition into an immutable @c Organ.
struct CompileResult
{
    std::optional<Organ> organ;      ///< Present iff compilation produced a usable spec.
    LoadDiagnostics      diagnostics;

    [[nodiscard]] bool ok() const noexcept { return organ.has_value(); }
};

/**
 * @brief Reads and compiles Ceciliae's human-readable organ files.
 *
 * The loader works on IN-MEMORY document text: the JUCE shell reads the file
 * bytes (it owns all OS/filesystem access) and hands the text here, so the pure
 * @c ceciliae_core library never touches the filesystem. All work happens OFF
 * the audio thread; the produced @c Organ is then read-only and RT-safe.
 *
 * Two stages, usable independently:
 *  1. @ref parse — document text -> mutable @c OrganDefinition.
 *  2. @ref compile — @c OrganDefinition -> immutable @c Organ (names resolved to
 *     dense ids, pipes materialised, references validated).
 * @ref load chains both.
 *
 * The file format is Ceciliae's own schema (see the module README); it never
 * ingests a GPL'd GrandOrgue / Hauptwerk organ definition.
 */
class OrganLoader
{
public:
    /**
     * @brief Parse organ-document text into an @c OrganDefinition.
     * @param content    The full document text (already read by the shell).
     * @param format     Surface syntax, or @c Auto to sniff.
     * @param sourceName Optional label used in diagnostics (e.g. a file name).
     * @return The parsed definition plus any diagnostics.
     *
     * Not real-time safe (allocates, parses). // TODO(phase model): implement the
     * proprietary JSON/YAML reader; the scaffold reports "not yet implemented".
     */
    [[nodiscard]] static ParseResult parse(std::string_view content,
                                           OrganFileFormat format = OrganFileFormat::Auto,
                                           std::string_view sourceName = {});

    /**
     * @brief Compile a validated definition into an immutable @c Organ.
     * @param definition The parsed (and ideally validated) document.
     * @return The compiled organ (absent if a fatal error was found) + diagnostics.
     *
     * Resolves name references to dense ids, assigns each element an id equal to
     * its array index, materialises every rank's pipes, and re-runs structural
     * validation. Not real-time safe.
     */
    [[nodiscard]] static CompileResult compile(const OrganDefinition& definition);

    /**
     * @brief Convenience: @ref parse then @ref compile.
     * @return The compiled organ + merged diagnostics from both stages.
     */
    [[nodiscard]] static CompileResult load(std::string_view content,
                                            OrganFileFormat format = OrganFileFormat::Auto,
                                            std::string_view sourceName = {});

    /**
     * @brief Serialise a definition back to document text (round-trip / export).
     * @return The document text, or an empty string if serialisation is
     *         unimplemented for @p format.
     *
     * Not real-time safe. // TODO(phase model): implement the proprietary writer.
     */
    [[nodiscard]] static std::string serialize(const OrganDefinition& definition,
                                               OrganFileFormat format = OrganFileFormat::Json);
};

} // namespace ceciliae::model

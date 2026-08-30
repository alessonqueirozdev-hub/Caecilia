// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/model/Diagnostics.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/OrganDefinition.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace caecilia::model
{

/// Surface syntax of a human-readable organ document.
enum class OrganFileFormat
{
    Auto, ///< Sniff from the content (leading '{' => JSON, otherwise YAML).
    Json, ///< Caecilia organ document in JSON.
    Yaml  ///< Caecilia organ document in YAML.
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
 * @brief Reads and compiles Caecilia's human-readable organ files.
 *
 * The loader works on IN-MEMORY document text: the JUCE shell reads the file
 * bytes (it owns all OS/filesystem access) and hands the text here, so the pure
 * @c caecilia_core library never touches the filesystem. All work happens OFF
 * the audio thread; the produced @c Organ is then read-only and RT-safe.
 *
 * Two stages, usable independently:
 *  1. @ref parse — document text -> mutable @c OrganDefinition.
 *  2. @ref compile — @c OrganDefinition -> immutable @c Organ (names resolved to
 *     dense ids, pipes materialised, references validated).
 * @ref load chains both.
 *
 * The file format is Caecilia's own schema (see the module README); it never
 * ingests a GPL'd GrandOrgue / Hauptwerk organ definition.
 */
/**
 * @brief How a document's relative resource references become content.
 * @param relativePath The reference exactly as the document wrote it.
 * @return The file's text, or nullopt if it cannot be provided.
 *
 * The model layer knows nothing about filesystems and must not: it is compiled
 * into a library that links only the standard library, and an organ document is
 * something that arrives as text from anywhere. So resolution is the caller's --
 * the plugin resolves beside the organ file, the command-line tool beside the
 * path it was given, and a test resolves out of a map in memory.
 *
 * A caller that supplies none is saying this document has no resources to reach,
 * and a rank that names one is then reported rather than silently ignored.
 */
using ResourceResolver = std::function<std::optional<std::string>(std::string_view)>;

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
     * Not real-time safe (allocates, parses).
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
    [[nodiscard]] static CompileResult compile(const OrganDefinition& definition,
                                               const ResourceResolver& resolve = {});

    /**
     * @brief Convenience: @ref parse then @ref compile.
     * @return The compiled organ + merged diagnostics from both stages.
     */
    [[nodiscard]] static CompileResult load(std::string_view content,
                                            OrganFileFormat format = OrganFileFormat::Auto,
                                            std::string_view sourceName = {},
                                            const ResourceResolver& resolve = {});

    /**
     * @brief Serialise a definition back to document text (round-trip / export).
     * @return The document. Only non-default values are written, so a file stays
     *         the size of what its author actually said.
     *
     * Not real-time safe.
     */
    [[nodiscard]] static std::string serialize(const OrganDefinition& definition,
                                               OrganFileFormat format = OrganFileFormat::Json);

    /**
     * @brief The inverse of @ref compile: an organ back to an editable definition.
     * @return A definition that compiles to an equivalent organ.
     *
     * What it is for: the shipping instrument is built in C++ by DemoOrgan.cpp, so
     * without this it could not be exported and the file format had never been
     * shown able to express it. A format that cannot describe the one organ that
     * exists is a format nobody should be asked to write against.
     *
     * It recovers the DOCUMENT, not the organ: dense ids become the names they
     * were resolved from, and generated pipes are dropped because the document
     * never had them -- a rank's compass and voicing are what produce them.
     *
     * Not real-time safe.
     */
    [[nodiscard]] static OrganDefinition definitionFrom(const Organ& organ);
};

} // namespace caecilia::model

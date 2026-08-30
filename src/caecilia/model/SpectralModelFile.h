// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

/**
 * @file
 * @brief Reading a measured pipe spectrum back in.
 *
 * `caecilia-partial-extractor` FFTs a recording of a real pipe into a partial
 * bank and writes it as JSON. Until this existed nothing read that JSON, so the
 * offline analysis toolchain produced a file the instrument could not open and
 * every rank in every organ sounded from the procedural recipe -- family, footage
 * and eight voicing numbers -- however carefully anyone had measured a real one.
 *
 * This is the other half. A rank in an organ file may name a spectrum file, and
 * what comes out of here replaces the recipe: the measured partials give the
 * timbre, and the footage fold, the level calibration and the pipe-to-pipe
 * scatter that make a rank out of one pipe all still apply on top.
 *
 * Off the audio thread only: it parses and allocates.
 */

#include "caecilia/model/Diagnostics.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <optional>
#include <string_view>

namespace caecilia::model
{

/// The outcome of reading a spectrum document.
struct SpectralModelLoad
{
    /// The model, absent if the document could not be read.
    std::optional<synth::SpectralModel> model;

    /// Everything the reader has to say. Errors leave @ref model empty.
    LoadDiagnostics diagnostics;

    [[nodiscard]] bool ok() const { return model.has_value() && !diagnostics.hasErrors(); }
};

/**
 * @brief Read a measured spectrum written by @c caecilia-partial-extractor.
 * @param document   The file's text.
 * @param sourceName What to call it in a diagnostic; a path, usually.
 *
 * The document the extractor writes:
 * @code
 * {
 *   "fundamentalHz": 261.63,
 *   "footage": { "num": 8, "den": 1 },
 *   "partials": [
 *     { "ratioToF0": 1.0, "ampDb": 0.0, "phase": 0.0, "windSensitivity": 0.5 }
 *   ]
 * }
 * @endcode
 *
 * @c onsetSeconds and @c brightnessTrack are read when present and left at their
 * defaults when not, so a document from today's extractor loads and a richer one
 * from a later extractor loads too, without a version negotiation for a field
 * that has an obvious absence.
 */
[[nodiscard]] SpectralModelLoad loadSpectralModel(std::string_view document,
                                                  std::string_view sourceName = {});

} // namespace caecilia::model

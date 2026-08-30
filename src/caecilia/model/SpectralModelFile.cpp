// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/model/SpectralModelFile.h"

#include "caecilia/model/Json.h"

#include <cmath>
#include <string>

namespace caecilia::model
{
namespace
{

/// A number field, reported rather than guessed at when it is the wrong kind.
bool readNumber(const json::Value& obj, const char* key, const std::string& path,
                LoadDiagnostics& diag, float& out, bool required)
{
    const json::Value* v = obj.find(key);
    if (v == nullptr)
    {
        if (required)
            diag.error(std::string("Missing '") + key + "'.", path);
        return false;
    }
    if (! v->isNumber())
    {
        diag.error(std::string("'") + key + "' must be a number.", path);
        return false;
    }
    out = static_cast<float>(v->asNumber());
    return true;
}

} // namespace

SpectralModelLoad loadSpectralModel(std::string_view document, std::string_view sourceName)
{
    SpectralModelLoad out;
    const std::string source(sourceName.empty() ? "spectrum" : sourceName);

    json::Error       error;
    const auto        parsed = json::parse(document, error);
    if (! parsed.has_value())
    {
        out.diagnostics.error(error.describe(), source);
        return out;
    }

    if (! parsed->isObject())
    {
        out.diagnostics.error("A spectrum document is a JSON object.", source);
        return out;
    }

    synth::SpectralModel model;

    // The note it was measured at. Not optional and not defaulted: a spectrum
    // whose fundamental is unknown cannot be folded onto another footage, and
    // guessing one would silently transpose somebody's measurement.
    if (! readNumber(*parsed, "fundamentalHz", source, out.diagnostics,
                     model.fundamentalHz, true))
        return out;

    if (! (model.fundamentalHz > 0.0f) || ! std::isfinite(model.fundamentalHz))
    {
        out.diagnostics.error("fundamentalHz must be a positive, finite frequency.", source);
        return out;
    }

    const json::Value* partials = parsed->find("partials");
    if (partials == nullptr || ! partials->isArray())
    {
        out.diagnostics.error("A spectrum document needs a 'partials' array.", source);
        return out;
    }

    if (partials->items().empty())
    {
        out.diagnostics.error("The 'partials' array is empty; there is no timbre here.",
                              source);
        return out;
    }

    model.partials.reserve(partials->items().size());
    for (std::size_t i = 0; i < partials->items().size(); ++i)
    {
        const json::Value& item = partials->items()[i];
        const std::string  path = source + ".partials[" + std::to_string(i) + "]";

        if (! item.isObject())
        {
            out.diagnostics.error("Each partial is an object.", path);
            return out;
        }

        synth::PartialTrack track;

        if (! readNumber(item, "ratioToF0", path, out.diagnostics, track.ratioToF0, true))
            return out;
        if (! (track.ratioToF0 > 0.0f) || ! std::isfinite(track.ratioToF0))
        {
            out.diagnostics.error("ratioToF0 must be a positive, finite ratio.", path);
            return out;
        }

        if (! readNumber(item, "ampDb", path, out.diagnostics, track.ampDb, true))
            return out;

        // The rest are optional, because the extractor that exists today writes
        // four fields and a later one may write six. An absent onset is no onset
        // stagger and an absent brightness track is none: both are the value a
        // measurement that did not look for them implies.
        (void) readNumber(item, "phase",           path, out.diagnostics, track.phase, false);
        (void) readNumber(item, "windSensitivity", path, out.diagnostics, track.windSensitivity, false);
        (void) readNumber(item, "onsetSeconds",    path, out.diagnostics, track.onsetSeconds, false);
        (void) readNumber(item, "brightnessTrack", path, out.diagnostics, track.brightnessTrack, false);

        model.partials.push_back(track);
    }

    // The extractor writes a partialCount beside the array. If both are there they
    // have to agree -- a mismatch means a file somebody edited by hand and did not
    // finish, and loading the array while ignoring the count would hide that.
    if (const json::Value* count = parsed->find("partialCount");
        count != nullptr && count->isNumber())
    {
        const auto stated = static_cast<std::size_t>(count->asNumber());
        if (stated != model.partials.size())
            out.diagnostics.warning("partialCount says " + std::to_string(stated)
                                        + " but the array holds "
                                        + std::to_string(model.partials.size()) + ".",
                                    source);
    }

    out.model = std::move(model);
    return out;
}

} // namespace caecilia::model

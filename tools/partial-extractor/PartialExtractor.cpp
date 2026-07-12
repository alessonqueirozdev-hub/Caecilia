/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "partial-extractor/PartialExtractor.h"

namespace ceciliae::tools
{

PartialExtractor::PartialExtractor(PartialExtractorOptions options)
    : options_(options)
{
}

double PartialExtractor::resolveFundamental(const WavData& sample) const
{
    if (options_.f0Hint > 0.0)
        return options_.f0Hint;

    // TODO(phase05): estimate f0 from the low-frequency spectral energy (or reuse
    // the sample-set analyzer's pitch pass). Return 0 to signal "unknown" for now.
    (void) sample;
    return 0.0;
}

PartialBank PartialExtractor::extract(const WavData& sample) const
{
    PartialBank bank;
    bank.footage       = options_.footage;
    bank.fundamentalHz = static_cast<float>(resolveFundamental(sample));

    // Reserve so the eventual tracker never reallocates mid-analysis.
    bank.partials.reserve(options_.maxPartials);

    // TODO(phase05): run the windowed STFT (a fresh radix-2 FFT written from
    // public math, no GPL), peak-pick each frame with parabolic interpolation,
    // track peaks into stable partials, and fill bank.partials with averaged
    // { ratioToF0, ampDb, phase }. TODO(phase06): sweep the analysis frames
    // against the wind envelope to fill windSensitivity per partial.
    //
    // Left empty here so the scaffold links and emits a well-formed (if empty)
    // bank; an empty bank is the safe "no partials extracted yet" state.

    return bank;
}

} // namespace ceciliae::tools

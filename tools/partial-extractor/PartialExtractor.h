/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include "common/AnalysisTypes.h"
#include "common/WavFile.h"

#include <cstddef>

namespace caecilia::tools
{

/**
 * @brief Tunable parameters for the additive-partial extractor.
 */
struct PartialExtractorOptions
{
    std::size_t   fftSize     = 4096;                 ///< STFT window / transform size (power of two).
    std::size_t   hopSize     = 1024;                 ///< STFT hop in frames.
    std::size_t   maxPartials = 64;                   ///< Cap on tracked partials.
    double        f0Hint      = 0.0;                  ///< Expected f0 in Hz (0 = auto-detect).
    double        floorDb     = -90.0;                ///< Ignore spectral peaks below this level.
    core::Footage footage     = core::footage::kEight;///< Sounding footage to record on the bank.
};

/**
 * @brief FFT-analyses a steady pipe tone into an additive @ref PartialBank.
 *
 * This is the off-line producer of the proprietary SpectralModel that later
 * seeds the modeled sustain tier, so a sampled attack and a modeled sustain can
 * share one timbre. It is a pure transform over decoded @ref WavData with no
 * console/file I/O of its own.
 *
 * Pipeline (phased):
 *  1. Pick the steady analysis window                       (TODO phase05).
 *  2. Windowed STFT (fresh radix-2 FFT, no GPL code)        (TODO phase05).
 *  3. Peak-pick + parabolic interpolation per frame         (TODO phase05).
 *  4. Track peaks into partials; average amp/phase          (TODO phase05).
 *  5. Estimate per-partial wind sensitivity across frames   (TODO phase06).
 */
class PartialExtractor
{
public:
    /// Construct with the given options (defaults suit a 4096-point analysis).
    explicit PartialExtractor(PartialExtractorOptions options = {});

    /// Analyse one decoded recording into an additive partial bank.
    [[nodiscard]] PartialBank extract(const WavData& sample) const;

private:
    /// TODO(phase05): detect f0 when no hint is supplied; returns hint for now.
    [[nodiscard]] double resolveFundamental(const WavData& sample) const;

    PartialExtractorOptions options_;
};

} // namespace caecilia::tools

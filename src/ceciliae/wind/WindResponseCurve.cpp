/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/wind/WindResponseCurve.h"

namespace ceciliae::wind
{

WindResponseCurve defaultResponseFor(core::TonalFamily family) noexcept
{
    // Coefficients are per unit of normalised deviation (a full 100% pressure
    // swing). Real deviations are a few percent, so effective movement is small.
    // TODO(phase5): replace these hand-tuned defaults with per-rank curves
    // estimated by the offline wind-sensitivity analysis toolchain.
    switch (family)
    {
        case core::TonalFamily::Reed:
            // Reeds are the most wind-sensitive: strong pitch and brightness swing.
            return WindResponseCurve{ /*cents*/ 90.0f, /*dB*/ 6.0f, /*bright*/ 1.20f, /*attack*/ 0.80f };

        case core::TonalFamily::String:
            // Strings shimmer: moderate pitch, pronounced brightness movement.
            return WindResponseCurve{ /*cents*/ 45.0f, /*dB*/ 3.5f, /*bright*/ 0.90f, /*attack*/ 0.50f };

        case core::TonalFamily::Principal:
            // Principals are steady: gentle response overall.
            return WindResponseCurve{ /*cents*/ 20.0f, /*dB*/ 2.5f, /*bright*/ 0.45f, /*attack*/ 0.35f };

        case core::TonalFamily::Flute:
            // Flutes are the least wind-sensitive of the flue voices.
            return WindResponseCurve{ /*cents*/ 15.0f, /*dB*/ 2.0f, /*bright*/ 0.30f, /*attack*/ 0.30f };

        case core::TonalFamily::Mixture:
            // Compound stops broadly track the principal chorus.
            return WindResponseCurve{ /*cents*/ 25.0f, /*dB*/ 3.0f, /*bright*/ 0.60f, /*attack*/ 0.40f };

        case core::TonalFamily::Hybrid:
            // Cross-family voices sit between string and principal behaviour.
            return WindResponseCurve{ /*cents*/ 35.0f, /*dB*/ 3.0f, /*bright*/ 0.70f, /*attack*/ 0.45f };

        case core::TonalFamily::Percussion:
            // Struck voices are effectively wind-insensitive in sustain.
            return WindResponseCurve{ /*cents*/ 5.0f, /*dB*/ 1.0f, /*bright*/ 0.10f, /*attack*/ 0.10f };

        case core::TonalFamily::Undefined:
        default:
            return WindResponseCurve{ /*cents*/ 20.0f, /*dB*/ 2.5f, /*bright*/ 0.45f, /*attack*/ 0.35f };
    }
}

} // namespace ceciliae::wind

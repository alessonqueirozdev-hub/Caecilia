/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/dsp/FormantFilter.h"

#include "ceciliae/dsp/DspMath.h"

namespace ceciliae::dsp
{

void FormantFilter::setFormants(const Formant* formants, std::size_t count) noexcept
{
    active_ = count < kMaxFormants ? count : kMaxFormants;
    for (std::size_t i = 0; i < active_; ++i)
    {
        const Formant& f = formants[i];
        // Q = centre / bandwidth is the standard resonance sharpness measure.
        const float q = f.bandwidthHz > 1.0f ? f.centreHz / f.bandwidthHz : 1.0f;
        sections_[i].setCoeffs(BiquadCoeffs::bandpass(sampleRate_, f.centreHz, q));
        weights_[i] = dbToGain(f.gainDb);
    }
}

float FormantFilter::process(float x) noexcept
{
    float out = 0.0f;
    for (std::size_t i = 0; i < active_; ++i)
        out += sections_[i].process(x) * weights_[i];
    return out;
}

void FormantFilter::reset() noexcept
{
    for (auto& s : sections_)
        s.reset();
}

} // namespace ceciliae::dsp

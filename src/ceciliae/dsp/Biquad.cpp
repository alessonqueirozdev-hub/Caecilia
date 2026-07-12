/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/dsp/Biquad.h"

#include "ceciliae/dsp/DspMath.h"
#include "ceciliae/dsp/Kernels.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Standard bilinear-transform biquad designs (public-domain analogue-prototype
// formulas), authored fresh. Each computes the analogue intermediate quantities
// w0 = 2*pi*f/fs, alpha = sin(w0)/(2Q), then normalises by a0.
// ---------------------------------------------------------------------------

namespace ceciliae::dsp
{

namespace
{
struct Intermediates
{
    double w0;
    double cosW0;
    double sinW0;
    double alpha;
};

[[nodiscard]] Intermediates prep(core::SampleRate fs, float freqHz, float q) noexcept
{
    const double safeFs = fs > 0.0 ? fs : 44100.0;
    const double f      = clamp(static_cast<double>(freqHz), 1.0, safeFs * 0.49);
    const double safeQ  = q > 1.0e-3f ? static_cast<double>(q) : 1.0e-3;
    Intermediates m{};
    m.w0    = kTwoPi * f / safeFs;
    m.cosW0 = std::cos(m.w0);
    m.sinW0 = std::sin(m.w0);
    m.alpha = m.sinW0 / (2.0 * safeQ);
    return m;
}

[[nodiscard]] BiquadCoeffs normalise(double b0,
                                     double b1,
                                     double b2,
                                     double a0,
                                     double a1,
                                     double a2) noexcept
{
    const double inv = a0 != 0.0 ? 1.0 / a0 : 1.0;
    BiquadCoeffs c{};
    c.b0 = static_cast<float>(b0 * inv);
    c.b1 = static_cast<float>(b1 * inv);
    c.b2 = static_cast<float>(b2 * inv);
    c.a1 = static_cast<float>(a1 * inv);
    c.a2 = static_cast<float>(a2 * inv);
    return c;
}
} // namespace

BiquadCoeffs BiquadCoeffs::lowpass(core::SampleRate fs, float freqHz, float q) noexcept
{
    const Intermediates m  = prep(fs, freqHz, q);
    const double        b1 = 1.0 - m.cosW0;
    const double        b0 = b1 * 0.5;
    return normalise(b0, b1, b0, 1.0 + m.alpha, -2.0 * m.cosW0, 1.0 - m.alpha);
}

BiquadCoeffs BiquadCoeffs::highpass(core::SampleRate fs, float freqHz, float q) noexcept
{
    const Intermediates m  = prep(fs, freqHz, q);
    const double        b0 = 0.5 * (1.0 + m.cosW0);
    const double        b1 = -(1.0 + m.cosW0);
    return normalise(b0, b1, b0, 1.0 + m.alpha, -2.0 * m.cosW0, 1.0 - m.alpha);
}

BiquadCoeffs BiquadCoeffs::bandpass(core::SampleRate fs, float freqHz, float q) noexcept
{
    const Intermediates m = prep(fs, freqHz, q);
    // Constant peak gain (0 dB) band-pass.
    return normalise(m.alpha, 0.0, -m.alpha, 1.0 + m.alpha, -2.0 * m.cosW0, 1.0 - m.alpha);
}

BiquadCoeffs BiquadCoeffs::notch(core::SampleRate fs, float freqHz, float q) noexcept
{
    const Intermediates m = prep(fs, freqHz, q);
    return normalise(1.0, -2.0 * m.cosW0, 1.0, 1.0 + m.alpha, -2.0 * m.cosW0, 1.0 - m.alpha);
}

BiquadCoeffs BiquadCoeffs::peaking(core::SampleRate fs, float freqHz, float q, float gainDb) noexcept
{
    const Intermediates m = prep(fs, freqHz, q);
    const double        A = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    return normalise(1.0 + m.alpha * A,
                     -2.0 * m.cosW0,
                     1.0 - m.alpha * A,
                     1.0 + m.alpha / A,
                     -2.0 * m.cosW0,
                     1.0 - m.alpha / A);
}

BiquadCoeffs BiquadCoeffs::lowShelf(core::SampleRate fs, float freqHz, float q, float gainDb) noexcept
{
    const Intermediates m    = prep(fs, freqHz, q);
    const double        A    = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double        sqrA = std::sqrt(A);
    const double        tsa  = 2.0 * sqrA * m.alpha;
    return normalise(A * ((A + 1.0) - (A - 1.0) * m.cosW0 + tsa),
                     2.0 * A * ((A - 1.0) - (A + 1.0) * m.cosW0),
                     A * ((A + 1.0) - (A - 1.0) * m.cosW0 - tsa),
                     (A + 1.0) + (A - 1.0) * m.cosW0 + tsa,
                     -2.0 * ((A - 1.0) + (A + 1.0) * m.cosW0),
                     (A + 1.0) + (A - 1.0) * m.cosW0 - tsa);
}

BiquadCoeffs BiquadCoeffs::highShelf(core::SampleRate fs, float freqHz, float q, float gainDb) noexcept
{
    const Intermediates m    = prep(fs, freqHz, q);
    const double        A    = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double        sqrA = std::sqrt(A);
    const double        tsa  = 2.0 * sqrA * m.alpha;
    return normalise(A * ((A + 1.0) + (A - 1.0) * m.cosW0 + tsa),
                     -2.0 * A * ((A - 1.0) + (A + 1.0) * m.cosW0),
                     A * ((A + 1.0) + (A - 1.0) * m.cosW0 - tsa),
                     (A + 1.0) - (A - 1.0) * m.cosW0 + tsa,
                     2.0 * ((A - 1.0) - (A + 1.0) * m.cosW0),
                     (A + 1.0) - (A - 1.0) * m.cosW0 - tsa);
}

void Biquad::processBlock(float* data, std::size_t count) noexcept
{
    kernels::biquadBlock(data, count, coeffs_.b0, coeffs_.b1, coeffs_.b2, coeffs_.a1, coeffs_.a2,
                         z1_, z2_);
}

} // namespace ceciliae::dsp

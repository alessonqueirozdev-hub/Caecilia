// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/wind/Bellows.h"

#include <cmath>

namespace caecilia::wind
{

void Bellows::configure(const BellowsConfig& config) noexcept
{
    config_ = config;
    reset();
}

void Bellows::reset() noexcept
{
    pressurePa_  = config_.nominalPressurePa;
    dPdt_        = 0.0f;
    startPa_     = config_.nominalPressurePa;
    endPa_       = config_.nominalPressurePa;
    demandAccum_ = 0.0f;
}

void Bellows::beginStep() noexcept
{
    demandAccum_ = 0.0f;
}

void Bellows::addDemand(float flow) noexcept
{
    // Accumulate-only; negative demand is clamped away so a stray value can
    // never inflate the reservoir above nominal.
    if (flow > 0.0f)
        demandAccum_ += flow;
}

void Bellows::integrate(double dtSeconds) noexcept
{
    startPa_ = pressurePa_;

    // Equilibrium sags below nominal in proportion to demand: a stiffer feed
    // (larger conductance) yields less sag for the same demand.
    const float sagPa = config_.feedConductance > 0.0f
                            ? demandAccum_ / config_.feedConductance
                            : 0.0f;
    const float equilibriumPa = config_.nominalPressurePa - sagPa;

    if (config_.plateResonanceHz > 0.0f)
    {
        integratePlate(equilibriumPa, dtSeconds);
        endPa_ = pressurePa_;
        return;
    }

    // First-order relaxation toward equilibrium. Exact exponential update keeps
    // the step unconditionally stable regardless of block size.
    const double rate = config_.compliance > 0.0f
                            ? static_cast<double>(config_.feedConductance)
                                  / static_cast<double>(config_.compliance)
                            : 0.0;
    const float alpha = static_cast<float>(1.0 - std::exp(-rate * dtSeconds));

    pressurePa_ += (equilibriumPa - pressurePa_) * alpha;
    endPa_       = pressurePa_;

    // TODO(phase2): a nonlinear curtain-valve regulation curve, and the passive
    // leak term.
}

void Bellows::integratePlate(float equilibriumPa, double dtSeconds) noexcept
{
    // The plate on its air spring: a damped harmonic oscillator whose rest point
    // is the equilibrium the demand sets.
    //
    //     p'' + 2*zeta*w0*p' + w0^2*(p - p_eq) = 0
    //
    // Solved EXACTLY over the step rather than stepped numerically, for the same
    // reason the first-order path uses an exponential: a host may hand this a
    // block of 64 frames or of 2048, and an explicit integrator that is stable at
    // one is not at the other. At 4.5 Hz an explicit Euler step goes unstable
    // somewhere around a 2048-frame block, which is a buffer size hosts really
    // use -- so the reservoir would blow up on exactly the machines that most
    // need a large buffer.
    //
    // The demand is constant across a block by construction (it is booked once per
    // block), so p_eq is constant and the closed form applies without approximation.
    const double w0   = 6.283185307179586 * static_cast<double>(config_.plateResonanceHz);
    const double zeta = config_.plateDamping < 0.0f    ? 0.0
                      : (config_.plateDamping > 0.999f ? 0.999
                                                       : static_cast<double>(config_.plateDamping));

    const double a  = zeta * w0;                      // decay rate
    const double wd = w0 * std::sqrt(1.0 - zeta * zeta); // damped frequency

    const double e0 = static_cast<double>(pressurePa_) - static_cast<double>(equilibriumPa);
    const double v0 = static_cast<double>(dPdt_);

    const double decay = std::exp(-a * dtSeconds);
    const double c     = std::cos(wd * dtSeconds);

    // sin(wd*dt)/wd, which tends to dt as the damping approaches critical. Taking
    // the limit explicitly keeps the near-critical case finite instead of dividing
    // by a vanishing wd.
    constexpr double kTinyWd = 1.0e-9;
    const double sOverWd = wd > kTinyWd ? std::sin(wd * dtSeconds) / wd : dtSeconds;

    const double e1 = decay * (e0 * c + (v0 + a * e0) * sOverWd);
    const double v1 = decay * (v0 * c - (w0 * w0 * e0 + a * v0) * sOverWd);

    pressurePa_ = static_cast<float>(static_cast<double>(equilibriumPa) + e1);
    dPdt_       = static_cast<float>(v1);
}

} // namespace caecilia::wind

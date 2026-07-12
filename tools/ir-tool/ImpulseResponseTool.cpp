/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ir-tool/ImpulseResponseTool.h"

#include <cmath>

namespace caecilia::tools
{

ImpulseResponseTool::ImpulseResponseTool(IrToolOptions options)
    : options_(options)
{
}

double ImpulseResponseTool::peakDbfs(const WavData& ir) noexcept
{
    float peak = 0.0f;
    for (const float s : ir.interleaved)
    {
        const float a = std::fabs(s);
        if (a > peak)
            peak = a;
    }
    if (peak <= 0.0f)
        return -144.0;
    return 20.0 * std::log10(static_cast<double>(peak));
}

ImpulseResponseInfo ImpulseResponseTool::describe(const WavData& ir) const
{
    ImpulseResponseInfo info;
    info.sampleRate  = ir.sampleRate;
    info.numChannels = ir.numChannels;
    info.numFrames   = ir.numFrames;
    info.lengthSec   = ir.lengthSeconds();
    info.peakDbfs    = peakDbfs(ir);

    // TODO(phase03): estimate RT60 from the backwards-integrated Schroeder decay
    // curve, and find the onset frame as the first sample crossing the trim
    // threshold. Left at defaults so the summary is well-formed meanwhile.
    info.rt60Sec    = 0.0;
    info.onsetFrame = 0;

    return info;
}

bool ImpulseResponseTool::normaliseInPlace(WavData& ir) const noexcept
{
    const double currentPeak = peakDbfs(ir);
    if (currentPeak <= -144.0)
        return false; // nothing to scale

    const double gainDb     = options_.targetPeakDb - currentPeak;
    const float  gainLinear = static_cast<float>(std::pow(10.0, gainDb / 20.0));
    for (float& s : ir.interleaved)
        s *= gainLinear;
    return true;
}

bool ImpulseResponseTool::processInPlace(WavData& ir, std::string* error) const
{
    switch (options_.op)
    {
        case IrOperation::Info:
            return true; // describe() is the payload; nothing to edit.

        case IrOperation::Normalise:
            if (!normaliseInPlace(ir))
            {
                if (error != nullptr)
                    *error = "cannot normalise a silent impulse response";
                return false;
            }
            return true;

        case IrOperation::Trim:
            // TODO(phase03): drop leading samples before the onset threshold and
            // truncate the tail once the Schroeder curve falls below
            // options_.trimThresholdDb. No-op passthrough for now.
            return true;

        case IrOperation::Resample:
            // TODO(phase03): resample to options_.targetSampleRate using the fresh
            // 16-point Kaiser-windowed-sinc interpolator from the dsp module.
            if (error != nullptr)
                *error = "resample is not implemented yet (phase 03)";
            return false;

        case IrOperation::Deconvolve:
            // TODO(phase95): recover the IR from an exponential sine sweep and its
            // inverse filter (convolution reverb hardening milestone).
            if (error != nullptr)
                *error = "deconvolve is not implemented yet (phase 95)";
            return false;
    }

    if (error != nullptr)
        *error = "unknown operation";
    return false;
}

} // namespace caecilia::tools

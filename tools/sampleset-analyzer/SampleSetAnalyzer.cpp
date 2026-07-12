/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "sampleset-analyzer/SampleSetAnalyzer.h"

#include <cmath>

namespace caecilia::tools
{

SampleSetAnalyzer::SampleSetAnalyzer(SampleSetAnalyzerOptions options)
    : options_(options)
{
}

double SampleSetAnalyzer::peakDbfs(const WavData& sample) noexcept
{
    float peak = 0.0f;
    for (const float s : sample.interleaved)
    {
        const float a = std::fabs(s);
        if (a > peak)
            peak = a;
    }
    if (peak <= 0.0f)
        return -144.0; // effectively digital silence
    return 20.0 * std::log10(static_cast<double>(peak));
}

PitchEstimate SampleSetAnalyzer::estimatePitch(const WavData& sample) const
{
    PitchEstimate estimate;

    // If the caller supplied an expected pitch we surface it as a confident
    // seed; the real detector will refine it in phase 04.
    if (options_.f0Hint > 0.0)
    {
        estimate.frequencyHz = options_.f0Hint;
        estimate.confidence  = 0.5;
    }

    // TODO(phase04): estimate f0 with a windowed autocorrelation / YIN pass over
    // the sustain region, then compute nearestNote and centsFromEqualTemper from
    // an A440 equal-tempered reference (or the active ITuning). Kept a no-op seed
    // here so the scaffold links and produces a well-formed result.
    (void) sample;
    return estimate;
}

SustainRegion SampleSetAnalyzer::findSustainRegion(const WavData& sample) const
{
    // TODO(phase04): follow the RMS envelope, mark the end of the attack decay
    // and the onset of the release, and return the steady window between them.
    // Placeholder returns the whole recording so downstream stages have a valid,
    // in-bounds range to operate on.
    return SustainRegion{ 0, sample.numFrames };
}

LoopPoint SampleSetAnalyzer::findLoop(const WavData& sample, SustainRegion region) const
{
    // TODO(phase04): within [region.startFrame, region.endFrame), search for the
    // zero-crossing-aligned start/end pair that minimises the spectral + waveform
    // seam error for at least options_.minLoopMs, then set crossfadeMs and mark
    // the result valid. Left invalid here so callers know no loop was committed.
    (void) sample;
    LoopPoint loop;
    loop.startFrame  = region.startFrame;
    loop.endFrame    = region.endFrame;
    loop.crossfadeMs = options_.crossfadeMs;
    loop.valid       = false;
    return loop;
}

SampleAnalysis SampleSetAnalyzer::analyze(const WavData& sample) const
{
    SampleAnalysis result;
    result.pipe        = options_.pipe;
    result.sampleRate  = sample.sampleRate;
    result.numFrames   = sample.numFrames;
    result.numChannels = sample.numChannels;
    result.peakDbfs    = peakDbfs(sample);

    result.pitch   = estimatePitch(sample);
    result.sustain = findSustainRegion(sample);
    result.loop    = findLoop(sample, result.sustain);

    // TODO(phase05): the attack/sustain splice point feeds the hybrid voice's
    // AttackSpliceConfig; derive it from the envelope knee rather than 0.
    result.attackEndSec = 0.0;

    return result;
}

} // namespace caecilia::tools

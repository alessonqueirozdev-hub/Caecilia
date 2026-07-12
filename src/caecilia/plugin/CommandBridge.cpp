/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/CommandBridge.h"

#include "caecilia/plugin/ParameterMirror.h"

#include <cmath>

namespace caecilia::plugin
{

using core::engine::EngineCommand;

namespace
{

/// Read a cached raw parameter, defaulting to @p fallback if it is unmapped.
float readParam(const CaeciliaParameterMirror& params, const char* id, float fallback) noexcept
{
    if (std::atomic<float>* p = params.rawParameter(id))
        return p->load(std::memory_order_relaxed);
    return fallback;
}

} // namespace

void CommandBridge::resetChangeTracking() noexcept
{
    last_ = LastSent{}; // invalid -> first pushChangedParameters resends everything
}

void CommandBridge::enqueue(const EngineCommand& command) noexcept
{
    if (ring_ == nullptr)
        return;
    // A full ring drops the command: parameter state is idempotently re-sent next
    // block, and a lost note is preferable to blocking the audio thread.
    (void) ring_->push(command);
}

void CommandBridge::pushMidi(const juce::MidiBuffer& midi) noexcept
{
    for (const juce::MidiMessageMetadata meta : midi)
    {
        const juce::MidiMessage msg = meta.getMessage();

        if (msg.isNoteOn())
        {
            // Scaffold mapping: one representative pipe per key on the default
            // division. // TODO(phase0.7): fan the key out to every engaged stop's
            // rank pipe via the model's Rank->PipeId activation mapping.
            const core::PipeId pipe{0, static_cast<std::uint8_t>(msg.getNoteNumber())};
            enqueue(EngineCommand::makeNoteOn(pipe,
                                              static_cast<core::Velocity>(msg.getVelocity()),
                                              defaultDivision_));
        }
        else if (msg.isNoteOff())
        {
            const core::PipeId pipe{0, static_cast<std::uint8_t>(msg.getNoteNumber())};
            enqueue(EngineCommand::makeNoteOff(pipe, defaultDivision_));
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            enqueue(EngineCommand::makePanic());
        }
    }
}

void CommandBridge::pushChangedParameters(const CaeciliaParameterMirror& params) noexcept
{
    // --- reverb ------------------------------------------------------------
    core::ReverbParams reverb;
    reverb.mix        = readParam(params, ParameterLayout::kReverbMix, reverb.mix);
    reverb.decaySec   = readParam(params, ParameterLayout::kReverbDecaySec, reverb.decaySec);
    reverb.preDelayMs = readParam(params, ParameterLayout::kReverbPreDelayMs, reverb.preDelayMs);
    reverb.dampingHz  = readParam(params, ParameterLayout::kReverbDampingHz, reverb.dampingHz);
    reverb.widthNorm  = readParam(params, ParameterLayout::kReverbWidth, reverb.widthNorm);

    const bool reverbChanged =
        !last_.valid || reverb.mix != last_.reverb.mix || reverb.decaySec != last_.reverb.decaySec
        || reverb.preDelayMs != last_.reverb.preDelayMs || reverb.dampingHz != last_.reverb.dampingHz
        || reverb.widthNorm != last_.reverb.widthNorm;

    if (reverbChanged)
    {
        enqueue(EngineCommand::makeReverbParams(reverb));
        last_.reverb = reverb;
    }

    // --- tremulant ---------------------------------------------------------
    const bool  tremOn    = readParam(params, ParameterLayout::kTremulantOn, 0.0f) >= 0.5f;
    const float tremRate  = readParam(params, ParameterLayout::kTremulantRateHz, 5.5f);
    const float tremDepth = readParam(params, ParameterLayout::kTremulantDepth, 0.0f);

    if (!last_.valid || tremOn != last_.tremOn || tremRate != last_.tremRate
        || tremDepth != last_.tremDepth)
    {
        enqueue(EngineCommand::makeTremulant(defaultChest_, tremOn, tremRate, tremDepth));
        last_.tremOn    = tremOn;
        last_.tremRate  = tremRate;
        last_.tremDepth = tremDepth;
    }

    // --- temperament / tuning ---------------------------------------------
    const int    tempIndex = static_cast<int>(std::lround(
        readParam(params, ParameterLayout::kTemperament, 0.0f)));
    const double a4        = static_cast<double>(
        readParam(params, ParameterLayout::kTuningA4Hz, 440.0f));

    if (!last_.valid || tempIndex != last_.temperament || a4 != last_.tuningA4Hz)
    {
        enqueue(EngineCommand::makeTemperament(ParameterLayout::temperamentFromChoice(tempIndex), a4));
        last_.temperament = tempIndex;
        last_.tuningA4Hz  = a4;
    }

    last_.valid = true;
}

void CommandBridge::pushPanic() noexcept
{
    enqueue(EngineCommand::makePanic());
}

} // namespace caecilia::plugin

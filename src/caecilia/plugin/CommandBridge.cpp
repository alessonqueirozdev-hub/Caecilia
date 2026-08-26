// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/plugin/CommandBridge.h"

#include "caecilia/wind/WindTypes.h"

#include "caecilia/plugin/ParameterMirror.h"

#include "caecilia/midi/MidiEvent.h"

#include <cmath>

namespace caecilia::plugin
{

using core::engine::EngineCommand;

namespace
{

/**
 * @brief A host timestamp, clamped into the block as an unsigned frame offset.
 *
 * The clamp happens on the SIGNED value, before the cast, and that ordering is
 * not cosmetic. Hosts do send negative sample positions -- a look-ahead, a
 * wrapped timestamp, a plugin ahead of the transport -- and casting -1 straight
 * to uint32 yields 4294967295, which the engine's own clamp then parks at the
 * LAST frame of the block. That is the exact opposite of what the host meant, and
 * once the sign is gone nothing downstream can recover it.
 */
[[nodiscard]] std::uint32_t frameOffset(int samplePosition, int numFrames) noexcept
{
    if (samplePosition <= 0)
        return 0;
    const int last = numFrames > 1 ? numFrames - 1 : 0;
    return static_cast<std::uint32_t>(samplePosition < last ? samplePosition : last);
}

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
    reverbGate_.reset();
}

void CommandBridge::enqueue(const EngineCommand& command) noexcept
{
    if (ring_ == nullptr)
        return;
    // A full ring drops the command: parameter state is idempotently re-sent next
    // block, and a lost note is preferable to blocking the audio thread.
    (void) ring_->push(command);
}

void CommandBridge::pushNote(const juce::MidiMessage& msg, core::DivisionId division,
                             std::uint32_t sampleOffset) noexcept
{
    // The pipe carries its division. Without it every manual shared one identity
    // space, so a note-off on one released the same note on all of them.
    // TODO(phase0.7): fan the key out to every engaged stop's rank pipe via the
    // model's Rank->PipeId activation mapping, instead of one representative pipe.
    const core::PipeId pipe{ 0,
                             static_cast<std::uint8_t>(msg.getNoteNumber()),
                             static_cast<std::uint8_t>(division.value) };

    if (msg.isNoteOn())
        enqueue(EngineCommand::makeNoteOn(pipe,
                                          static_cast<core::Velocity>(msg.getVelocity()),
                                          division, sampleOffset));
    else
        enqueue(EngineCommand::makeNoteOff(pipe, division, sampleOffset));
}

void CommandBridge::pushMidi(const juce::MidiBuffer& midi, int numFrames) noexcept
{
    for (const juce::MidiMessageMetadata meta : midi)
    {
        const juce::MidiMessage msg = meta.getMessage();
        const std::uint32_t     at  = frameOffset(meta.samplePosition, numFrames);

        // Resolve the controller channel to a division. An unmapped channel falls
        // back to the default, so a single-keyboard controller keeps working.
        const auto channel = static_cast<caecilia::midi::MidiChannel>(
            juce::jlimit(1, 16, msg.getChannel()) - 1);
        const core::DivisionId division =
            channelMap_.isMapped(channel) ? channelMap_.division(channel)
                                          : defaultDivision_;

        if (msg.isNoteOn() || msg.isNoteOff())
        {
            // Respect the channel's playable range. The organ model gives every
            // division a compass -- the demo pedalboard is 32 notes, the manuals
            // 61 -- and nothing enforced it, so a note outside the instrument
            // sounded anyway and consumed a voice.
            const auto note = static_cast<core::MidiNote>(msg.getNoteNumber());
            if (channelMap_.isMapped(channel) && ! channelMap_.inRange(channel, note))
                continue;
            pushNote(msg, division, at);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            enqueue(EngineCommand::makePanic(at));
        }
        else if (msg.isSustainPedalOn() || msg.isSustainPedalOff())
        {
            // Not a traditional organ control, but every MIDI keyboard has one and
            // ignoring it entirely meant the pedal did nothing at all.
            enqueue(EngineCommand::makeSustain(division, msg.isSustainPedalOn(), at));
        }
        else if (msg.isController()
                 && msg.getControllerNumber() == static_cast<int>(caecilia::midi::cc::kExpression))
        {
            // The swell shoe. THE expression control of a pipe organ -- an enclosed
            // division has no other dynamics at all -- and it has been arriving and
            // being dropped, because this decoder handled three message types by
            // hand and ignored the rest.
            //
            // Sent as a POSITION, not a gain: how much a shut box attenuates is a
            // property of the instrument, and the engine owns it.
            enqueue(EngineCommand::makeExpression(
                division,
                static_cast<float>(msg.getControllerValue()) * (1.0f / 127.0f), at));
        }
    }
}

void CommandBridge::pushConsoleMidi(const juce::MidiBuffer& midi, int numFrames) noexcept
{
    for (const juce::MidiMessageMetadata meta : midi)
    {
        const juce::MidiMessage msg = meta.getMessage();
        if (! msg.isNoteOn() && ! msg.isNoteOff())
            continue;

        // The console encodes the division IN the channel (channel == division + 1),
        // so it deliberately bypasses the controller channel map.
        const auto division = core::DivisionId{
            static_cast<std::uint16_t>(juce::jlimit(1, 16, msg.getChannel()) - 1) };
        pushNote(msg, division, frameOffset(meta.samplePosition, numFrames));
    }
}

void CommandBridge::pushChangedParameters(const CaeciliaParameterMirror& params,
                                          bool forceReverb) noexcept
{
    // --- reverb ------------------------------------------------------------
    // Start from what the reverb is ALREADY set to, not from a default-constructed
    // struct: bassBloom is part of the console's space presets and has no APVTS
    // parameter, so building the message from scratch reset it to the default the
    // moment the host touched any other reverb control -- a Cathedral quietly lost
    // its bass bloom because the user nudged the mix.
    core::ReverbParams reverb = reverbGate_.lastSent();
    reverb.mix        = readParam(params, ParameterLayout::kReverbMix, reverb.mix);
    reverb.decaySec   = readParam(params, ParameterLayout::kReverbDecaySec, reverb.decaySec);
    reverb.preDelayMs = readParam(params, ParameterLayout::kReverbPreDelayMs, reverb.preDelayMs);
    reverb.dampingHz  = readParam(params, ParameterLayout::kReverbDampingHz, reverb.dampingHz);
    reverb.widthNorm  = readParam(params, ParameterLayout::kReverbWidth, reverb.widthNorm);

    // Whether this is worth a message is a decision with a genuinely awkward edge
    // case, so it lives in core where it can be tested. See core/ReverbSendGate.h.
    if (reverbGate_.shouldSend(reverb, forceReverb))
        enqueue(EngineCommand::makeReverbParams(reverb));

    // --- tremulant ---------------------------------------------------------
    const bool  tremOn    = readParam(params, ParameterLayout::kTremulantOn, 0.0f) >= 0.5f;
    const float tremRate  = readParam(params, ParameterLayout::kTremulantRateHz, 5.5f);
    const float tremDepth = readParam(params, ParameterLayout::kTremulantDepth, 0.0f);

    if (!last_.valid || tremOn != last_.tremOn || tremRate != last_.tremRate
        || tremDepth != last_.tremDepth)
    {
        // Every chest that has one. A single Tremulant parameter for an instrument
        // whose tremulants are all on the Recit is the right control; per-division
        // switches would be a console change, and a parameter each.
        const float depthFraction = tremDepth * wind::kMaxTremulantDepthFraction;
        for (std::size_t i = 0; i < tremulantChestCount_; ++i)
            enqueue(EngineCommand::makeTremulant(tremulantChests_[i], tremOn,
                                                 tremRate, depthFraction));
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

void CommandBridge::pushExpression(core::DivisionId division, float position) noexcept
{
    enqueue(EngineCommand::makeExpression(division, position));
}

} // namespace caecilia::plugin

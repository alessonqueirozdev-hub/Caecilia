/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/engine/AudioEngine.h"

#include <algorithm>
#include <cmath>

namespace ceciliae::core::engine
{

// ---------------------------------------------------------------------------
// Off-thread setup.
// ---------------------------------------------------------------------------

void AudioEngine::prepare(SampleRate  sampleRate,
                          std::size_t maxBlockFrames,
                          std::size_t numChannels,
                          std::size_t numWindchests)
{
    sampleRate_     = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockFrames_ = maxBlockFrames;
    numChannels_    = numChannels > 0 ? numChannels : 2;
    numWindchests_  = std::clamp<std::size_t>(numWindchests, 1, kMaxWindchests);

    // A generous default so a full pool of nominal-cost voices renders unless the
    // host explicitly tightens the budget via setBlockBudget().
    if (blockBudgetUnits_ <= 0.0f)
        blockBudgetUnits_ = static_cast<float>(kMaxVoices);

    scheduler_.prepare(kMaxVoices, maxBlockFrames_);

    // Allocate the per-windchest accumulation buses as one flat block, plus the
    // stable channel-pointer table the AudioBlock views alias each callback.
    const std::size_t frames   = maxBlockFrames_;
    const std::size_t chanTot  = numWindchests_ * numChannels_;
    busSamples_.assign(chanTot * frames, 0.0f);
    busChannelPtrs_.assign(chanTot, nullptr);
    for (std::size_t c = 0; c < numWindchests_; ++c)
        for (std::size_t ch = 0; ch < numChannels_; ++ch)
        {
            const std::size_t flat = c * numChannels_ + ch;
            busChannelPtrs_[flat]  = busSamples_.data() + flat * frames;
        }
    busBlocks_.assign(numWindchests_, AudioBlock{});

    framePos_ = 0;
    meters_[0] = MeterSnapshot{};
    meters_[1] = MeterSnapshot{};
    pendingMeters_ = MeterSnapshot{};
    meterFront_.store(0, std::memory_order_release);
}

void AudioEngine::bindVoices(IVoice* const* voices, std::size_t count) noexcept
{
    pool_.bind(voices, count);
}

// ---------------------------------------------------------------------------
// Audio thread: the whole callback body.
// ---------------------------------------------------------------------------

void AudioEngine::processBlock(AudioBlock& output) noexcept
{
    const std::size_t numFrames = std::min(output.numFrames(), maxBlockFrames_);

    drainCommands();
    stepWind(numFrames);
    zeroChestBuses(numFrames);

    RenderContext ctx = makeContext(numFrames);

    pool_.refresh();
    scheduler_.renderActive(pool_.view(), ctx);

    mixBusesToOutput(output, numFrames);
    applyMasterChain(output);

    framePos_ += static_cast<std::uint64_t>(numFrames);

    captureMeters(output, numFrames);
    publishMeters();
}

// ---------------------------------------------------------------------------
// processBlock stages.
// ---------------------------------------------------------------------------

void AudioEngine::drainCommands() noexcept
{
    EngineCommand cmd;
    while (commandRing_.pop(cmd))
    {
        switch (cmd.type)
        {
            case EngineCommandType::NoteOn:  handleNoteOn(cmd.payload.noteOn);   break;
            case EngineCommandType::NoteOff: handleNoteOff(cmd.payload.noteOff); break;
            case EngineCommandType::Panic:   handlePanic();                      break;

            case EngineCommandType::SetReverbParams:
                if (masterReverb_ != nullptr)
                    masterReverb_->setParams(cmd.payload.reverb.params);
                break;

            case EngineCommandType::ApplyStateDelta:
                // TODO(phase0.7): apply the registration StateDelta as a
                // click-free, wind-modeled stop crossfade.
                break;
            case EngineCommandType::SetTremulant:
            case EngineCommandType::SetWindParams:
                // TODO(phase0.2): forward to the WindModel behind wind_.
                break;
            case EngineCommandType::SetTemperament:
                // TODO(phase0.9): trigger off-thread tuning-table rebuild.
                break;
            case EngineCommandType::StopEngage:
            case EngineCommandType::StopDisengage:
                // Informational; the audible change arrives via ApplyStateDelta.
                break;
            case EngineCommandType::None:
            default:
                break;
        }
    }
}

void AudioEngine::stepWind(std::size_t numFrames) noexcept
{
    (void) numFrames;
    // TODO(phase0.2): advance the reservoir/bellows ODE behind wind_ once per
    // block, integrating the demand voices registered last block, so the wind
    // snapshot read during render already reflects sag + tremulant.
}

void AudioEngine::zeroChestBuses(std::size_t numFrames) noexcept
{
    for (std::size_t c = 0; c < numWindchests_; ++c)
        for (std::size_t ch = 0; ch < numChannels_; ++ch)
        {
            float* p = busChannelPtrs_[c * numChannels_ + ch];
            std::fill(p, p + numFrames, 0.0f);
        }
}

RenderContext AudioEngine::makeContext(std::size_t numFrames) noexcept
{
    // Rebuild the bus views for this block's frame count (no allocation: the
    // AudioBlock objects live in a pre-sized vector and just re-point).
    for (std::size_t c = 0; c < numWindchests_; ++c)
        busBlocks_[c] = AudioBlock(&busChannelPtrs_[c * numChannels_], numChannels_, numFrames);

    RenderContext ctx;
    ctx.sampleRate       = sampleRate_;
    ctx.numFrames        = numFrames;
    ctx.framePos         = framePos_;
    ctx.oversampleFactor = 1;
    ctx.nyquist          = sampleRate_ * 0.5;
    ctx.wind             = wind_;
    ctx.tuning           = tuning_;
    ctx.chestBuses       = busBlocks_.data();
    ctx.numChestBuses    = numWindchests_;

    if (blockBudgetUnits_ > 0.0f)
    {
        budget_.reset(blockBudgetUnits_);
        ctx.budget = &budget_;
    }
    else
    {
        ctx.budget = nullptr; // unbounded: render everything
    }

    return ctx;
}

void AudioEngine::mixBusesToOutput(AudioBlock& output, std::size_t numFrames) noexcept
{
    const std::size_t outCh = output.numChannels();

    // Overwrite the host buffer with silence, then sum every chest bus in.
    for (std::size_t ch = 0; ch < outCh; ++ch)
        if (float* dst = output.channel(ch))
            std::fill(dst, dst + numFrames, 0.0f);

    const std::size_t ch = std::min(outCh, numChannels_);
    for (std::size_t c = 0; c < numWindchests_; ++c)
    {
        AudioBlock& bus = busBlocks_[c];
        for (std::size_t k = 0; k < ch; ++k)
        {
            const float* src = bus.channel(k);
            float*       dst = output.channel(k);
            if (src == nullptr || dst == nullptr)
                continue;
            for (std::size_t f = 0; f < numFrames; ++f)
                dst[f] += src[f];
        }
    }
}

void AudioEngine::applyMasterChain(AudioBlock& output) noexcept
{
    // TODO(phase0.3): insert per-pipe spatialization / early reflections before
    // the tail; for now the master chain is the reverb send only.
    if (masterReverb_ != nullptr)
        masterReverb_->process(output);
}

void AudioEngine::captureMeters(const AudioBlock& output, std::size_t numFrames) noexcept
{
    pendingMeters_              = MeterSnapshot{};
    pendingMeters_.framePos     = framePos_;
    pendingMeters_.activeVoices = static_cast<std::uint32_t>(pool_.activeCount());

    float       peak  = 0.0f;
    double      sumSq = 0.0;
    std::size_t n     = 0;
    for (std::size_t ch = 0; ch < output.numChannels(); ++ch)
    {
        const float* p = output.channel(ch);
        if (p == nullptr)
            continue;
        for (std::size_t f = 0; f < numFrames; ++f)
        {
            const float s = p[f];
            const float a = s < 0.0f ? -s : s;
            if (a > peak)
                peak = a;
            sumSq += static_cast<double>(s) * static_cast<double>(s);
            ++n;
        }
    }
    pendingMeters_.master.peak = peak;
    pendingMeters_.master.rms  = n > 0 ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(n))) : 0.0f;

    if (wind_ != nullptr)
    {
        const WindchestId chest0{0};
        pendingMeters_.windPressurePa = wind_->pressureAt(chest0, 0);
        pendingMeters_.windSagNorm    = wind_->pressureDeviation(chest0, 0);
    }
    // TODO(phase0.2): populate per-division meters and tremulant phase.
}

void AudioEngine::publishMeters() noexcept
{
    const int back = 1 - meterFront_.load(std::memory_order_relaxed);
    meters_[back]  = pendingMeters_;
    meterFront_.store(back, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Command handlers.
// ---------------------------------------------------------------------------

void AudioEngine::handleNoteOn(const EngineCommand::NoteOnPayload& p) noexcept
{
    VoiceHandle handle = pool_.acquire(p.pipe, framePos_);

    if (!handle.isValid())
    {
        // Pool full: steal the least-audible voice and retry once.
        const VoiceHandle victim = pool_.chooseVictim(scheduler_.stealPolicy());
        if (victim.isValid())
        {
            if (IVoice* v = pool_.resolve(victim))
                v->noteOff(); // TODO(phase0.6): phase-aligned crossfade steal
            pool_.release(victim);
            handle = pool_.acquire(p.pipe, framePos_);
        }
    }

    if (!handle.isValid())
        return; // genuinely out of voices this block

    if (IVoice* v = pool_.resolve(handle))
        v->noteOn(p.pipe, p.velocity);
    // TODO(phase0.6): match voice EngineKind/rank to the requested pipe rather
    // than reusing whatever slot was free.
}

void AudioEngine::handleNoteOff(const EngineCommand::NoteOffPayload& p) noexcept
{
    pool_.noteOff(p.pipe);
}

void AudioEngine::handlePanic() noexcept
{
    pool_.allNotesOff();
}

} // namespace ceciliae::core::engine

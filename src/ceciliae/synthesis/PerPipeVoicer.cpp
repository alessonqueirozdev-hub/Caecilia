/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/synthesis/PerPipeVoicer.h"

namespace ceciliae::synth
{

namespace
{
    /// SplitMix64: a fast, well-distributed deterministic bit mixer.
    [[nodiscard]] inline std::uint64_t splitMix64(std::uint64_t& state) noexcept
    {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /// Next deterministic float in [0, 1).
    [[nodiscard]] inline float nextUnit(std::uint64_t& state) noexcept
    {
        // Top 24 bits give a clean [0,1) float.
        const std::uint32_t bits = static_cast<std::uint32_t>(splitMix64(state) >> 40);
        return static_cast<float>(bits) / static_cast<float>(1u << 24);
    }

    /// Next deterministic float in [-1, 1).
    [[nodiscard]] inline float nextBipolar(std::uint64_t& state) noexcept
    {
        return nextUnit(state) * 2.0f - 1.0f;
    }
} // namespace

VoicingProfile PerPipeVoicer::voice(core::PipeId pipe) const noexcept
{
    // Fold the stable pipe identity and the salt into one seed.
    std::uint64_t seed = salt_;
    seed ^= (static_cast<std::uint64_t>(pipe.rankId) << 8) ^ static_cast<std::uint64_t>(pipe.midiNote);
    // Prime the stream so adjacent notes decorrelate.
    std::uint64_t state = seed * 0xD1B54A32D192ED03ull + 0x2545F4914F6CDD1Dull;

    VoicingProfile p{};
    p.detuneCents    = nextBipolar(state) * params_.maxDetuneCents;
    p.levelTrimDb    = nextBipolar(state) * params_.maxLevelTrimDb;
    p.brightnessTrim = nextBipolar(state) * params_.maxBrightnessTrim;
    p.attackJitter   = nextBipolar(state) * params_.maxAttackJitter;
    p.driftDepth     = nextUnit(state)    * params_.maxDriftDepth;

    // Chiff / speech scatter around their nominal base amounts, clamped to [0,1].
    const float chiff  = params_.baseChiffAmount  + nextBipolar(state) * 0.1f;
    const float speech = params_.baseSpeechAmount + nextBipolar(state) * 0.1f;
    p.chiffAmount  = chiff  < 0.0f ? 0.0f : (chiff  > 1.0f ? 1.0f : chiff);
    p.speechAmount = speech < 0.0f ? 0.0f : (speech > 1.0f ? 1.0f : speech);

    // A deterministic phase seed the voice uses to scatter initial partial phase.
    p.phaseSeed = static_cast<std::uint32_t>(splitMix64(state));

    return p;
}

} // namespace ceciliae::synth

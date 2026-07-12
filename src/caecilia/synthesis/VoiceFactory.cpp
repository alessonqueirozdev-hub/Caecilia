/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/synthesis/VoiceFactory.h"

#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/BeatingReed.h"
#include "caecilia/synthesis/ModalResonator.h"
#include "caecilia/synthesis/NonlinearJet.h"
#include "caecilia/synthesis/PhysicalPipeVoice.h"
#include "caecilia/synthesis/SampleVoice.h"
#include "caecilia/synthesis/Waveguide.h"

namespace caecilia::synth
{

namespace
{
    [[nodiscard]] constexpr int tierRank(core::VoiceTier tier) noexcept
    {
        return static_cast<int>(tier); // Modal(0) < Additive(1) < Waveguide(2)
    }

    [[nodiscard]] constexpr bool isReed(core::TonalFamily family) noexcept
    {
        return family == core::TonalFamily::Reed;
    }
} // namespace

core::EngineKind VoiceFactory::selectEngine(const RankVoiceRequest& request) const noexcept
{
    const bool physicalAllowed = tierRank(request.maxTier) >= tierRank(core::VoiceTier::Waveguide);

    // Exposed / solo stops earn the expensive nonlinear physical tier.
    if (request.exposed && physicalAllowed)
        return isReed(request.family) ? core::EngineKind::Modal      // beating reed + modal resonator
                                      : core::EngineKind::Waveguide; // nonlinear jet + waveguide

    // Plenum ranks with an analysed model use the cheap modeled-additive tier.
    if (request.hasSpectralModel && tierRank(request.maxTier) >= tierRank(core::VoiceTier::Additive))
        return core::EngineKind::Additive;

    // A recorded sample set is the safe, fully-shippable fallback.
    if (request.hasSampleSet)
        return core::EngineKind::Sample;

    // Last resort: additive, even without a seeded model.
    return core::EngineKind::Additive;
}

core::VoiceTier VoiceFactory::selectTier(const RankVoiceRequest& request) const noexcept
{
    switch (selectEngine(request))
    {
        case core::EngineKind::Sample:
            return core::VoiceTier::Modal;     // cheapest scheduling tier; does not demote.
        case core::EngineKind::Additive:
            return core::VoiceTier::Additive;
        case core::EngineKind::Waveguide:
        case core::EngineKind::Modal:
        default:
            return core::VoiceTier::Waveguide; // full nonlinear physical model (most expensive).
    }
}

std::unique_ptr<core::IVoice> VoiceFactory::create(const RankVoiceRequest& request,
                                                   const VoiceContext& context,
                                                   core::SampleRate sampleRate,
                                                   std::size_t maxBlockFrames) const
{
    switch (selectEngine(request))
    {
        case core::EngineKind::Sample:
        {
            auto voice = std::make_unique<SampleVoice>();
            voice->setContext(context);
            voice->setReleaseSpec(request.release);
            voice->setSampleSource(request.sampleSource);
            voice->prepare(sampleRate, maxBlockFrames);
            return voice;
        }

        case core::EngineKind::Additive:
        {
            auto voice = std::make_unique<AdditiveVoice>();
            voice->setContext(context);
            voice->prepare(sampleRate, maxBlockFrames); // reserves partial storage first
            if (request.spectralModel != nullptr)
                voice->seedFrom(*request.spectralModel);
            return voice;
        }

        case core::EngineKind::Waveguide:
        {
            auto voice = std::make_unique<PhysicalPipeVoice>();
            voice->setComponents(std::make_unique<NonlinearJet>(),
                                 std::make_unique<Waveguide>(),
                                 core::EngineKind::Waveguide,
                                 core::VoiceTier::Waveguide);
            voice->setContext(context);
            voice->setReleaseSpec(request.release);
            voice->prepare(sampleRate, maxBlockFrames);
            return voice;
        }

        case core::EngineKind::Modal:
        default:
        {
            auto voice = std::make_unique<PhysicalPipeVoice>();
            voice->setComponents(std::make_unique<BeatingReed>(),
                                 std::make_unique<ModalResonator>(),
                                 core::EngineKind::Modal,
                                 core::VoiceTier::Waveguide);
            voice->setContext(context);
            voice->setReleaseSpec(request.release);
            voice->prepare(sampleRate, maxBlockFrames);
            return voice;
        }
    }
}

} // namespace caecilia::synth

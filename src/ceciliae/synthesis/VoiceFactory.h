/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/IVoice.h"
#include "ceciliae/synthesis/IVoiceLayer.h"
#include "ceciliae/synthesis/ReleaseSpec.h"
#include "ceciliae/synthesis/SpectralModel.h"
#include "ceciliae/synthesis/VoiceContext.h"
#include "ceciliae/synthesis/VoicingProfile.h"

#include <cstddef>
#include <memory>

namespace ceciliae::synth
{

/**
 * @brief What the factory needs to know about a rank to pick and build a voice.
 *
 * This is a synthesis-local description; once the @c model module lands it will
 * be derived from that module's RankSpec/StopSpec rather than hand-filled. The
 * flags below drive the engine/tier selection: an @ref exposed solo stop earns
 * the expensive physical tier, while a plenum rank with an analysed
 * @ref SpectralModel gets the cheap modeled-additive tier, and a rank with only
 * a recorded sample set falls back to pure sample playback.
 */
struct RankVoiceRequest
{
    core::RankId      rank{};                                 ///< Owning rank identity.
    core::TonalFamily family  = core::TonalFamily::Principal; ///< Selects excitation/resonator + wind curve.
    core::ChorusRole  role    = core::ChorusRole::Foundation; ///< Chorus role (informational).
    core::Footage     footage = core::footage::kEight;        ///< Sounding footage of the rank.

    bool exposed          = false; ///< Solo / exposed stop => prefer the physical tier.
    bool hasSampleSet     = false; ///< A recorded sample set is available (safe fallback).
    bool hasSpectralModel = false; ///< An analysed SpectralModel is available (modeled tier).

    /// Ceiling on synthesis quality (the deadline budget may lower this further).
    core::VoiceTier maxTier = core::VoiceTier::Waveguide;

    const SpectralModel* spectralModel = nullptr; ///< Seed for additive/modeled voices (non-owning).
    const ISampleSource* sampleSource  = nullptr; ///< Recorded source for sample voices (non-owning).
    VoicingParams        voicing{};               ///< Rank voicing recipe (scatter ranges).
    ReleaseSpec          release{};               ///< Multi-stage release behaviour.
};

/**
 * @brief Selects and constructs the concrete @ref ceciliae::core::IVoice a rank
 *        should use, per rank, from a @ref RankVoiceRequest.
 *
 * The factory encodes the tiering policy that unifies what GrandOrgue and Aeolus
 * do in isolation: pick pure sample, wind-modulated additive, or the nonlinear
 * physical model, all as configurations of the same layered pipeline. All work
 * here happens OFF the audio thread (organ-load / prepare time); @ref create may
 * allocate. The audio thread only renders the returned, already-prepared voices.
 */
class VoiceFactory
{
public:
    VoiceFactory() = default;

    /// @return The engine kind selected for a rank. Pure, RT-safe.
    [[nodiscard]] core::EngineKind selectEngine(const RankVoiceRequest& request) const noexcept;

    /// @return The starting quality tier selected for a rank. Pure, RT-safe.
    [[nodiscard]] core::VoiceTier selectTier(const RankVoiceRequest& request) const noexcept;

    /**
     * @brief Build a prepared voice for a rank.
     * @param request        The rank description and available resources.
     * @param context        The shared render environment to wire in.
     * @param sampleRate     Host sample rate to @c prepare the voice for.
     * @param maxBlockFrames Largest block the voice will render.
     * @return An owned, prepared voice, or @c nullptr on an empty/invalid request.
     *
     * NOT real-time safe: allocates and calls @c prepare. Call off the audio
     * thread at organ-load time.
     */
    [[nodiscard]] std::unique_ptr<core::IVoice> create(const RankVoiceRequest& request,
                                                       const VoiceContext& context,
                                                       core::SampleRate sampleRate,
                                                       std::size_t maxBlockFrames) const;
};

} // namespace ceciliae::synth

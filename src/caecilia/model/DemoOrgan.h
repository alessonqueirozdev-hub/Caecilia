/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace caecilia::model
{

// ===========================================================================
// Caecilia demo organ — a fully specified 3-division romantic instrument
// (Pedal, Grand-Orgue, Récit) with ~26 stops spanning every tonal family, plus
// per-family additive voicing recipes and a registration -> ready-to-bind voice
// builder the render app and the plugin both call.
//
// This is a pure-core module: it depends only on `core`, `model` and `synth`
// and stays JUCE-free / filesystem-free. The instrument is built directly from
// the model builders (no organ-file text), so it never touches the (stubbed)
// OrganLoader::parse path.
// ===========================================================================

// ---------------------------------------------------------------------------
// Per-family spectral voicing recipes.
//
// Each builder returns a caecilia::synth::SpectralModel whose partial structure
// is characteristic of the named tonal family. The partial ratios are relative
// to the voice's sounding fundamental (F0), which the AdditiveVoice resolves
// from its VoiceContext footage at note-on; the recipe therefore encodes TIMBRE,
// not absolute pitch — except for @ref makeSpectralMixture / @ref
// makeSpectralMutation, which deliberately place partials at the mixture/quint/
// tierce ratios RELATIVE TO 8' UNISON so a single unison-footage voice sounds
// the compound / mutation pitches.
//
// `fundamentalHz` on the returned model is a nominal analysis reference only; it
// is overwritten by the true sounding frequency on AdditiveVoice::noteOn.
// ---------------------------------------------------------------------------

/// Principal / diapason: a full falling harmonic series (rich, singing).
[[nodiscard]] synth::SpectralModel makeSpectralPrincipal(core::Footage footage,
                                                         float       brightness    = 1.0f,
                                                         std::size_t numHarmonics  = 12);

/// Flute / bourdon: strong fundamental plus weak odd harmonics (hollow, round).
[[nodiscard]] synth::SpectralModel makeSpectralFlute(core::Footage footage,
                                                     float         brightness = 1.0f,
                                                     bool          stopped    = true);

/// String (Gambe / Salicional): rich, bright upper harmonics. A non-zero
/// @p detuneCents splits every partial into a slightly detuned pair, producing
/// the shimmering beat of a céleste.
[[nodiscard]] synth::SpectralModel makeSpectralString(core::Footage footage,
                                                      float         detuneCents = 0.0f,
                                                      float         brightness  = 1.0f);

/// Reed (Trompette / Hautbois): strong odd harmonics plus a band of emphasised
/// formant partials giving the reed its brassy / nasal signature.
[[nodiscard]] synth::SpectralModel makeSpectralReed(core::Footage footage,
                                                    float         formantCenterHz = 1200.0f,
                                                    float         brightness      = 1.0f);

/// Mixture (Fourniture / Plein Jeu): several bright partials placed at the
/// mixture-rank pitches (each footage's ratio to 8' unison), crowned by their
/// octaves, over a faint grounding fundamental.
[[nodiscard]] synth::SpectralModel makeSpectralMixture(std::span<const core::Footage> ranks,
                                                       float                          brightness = 1.0f);

/// Mutation (Nazard 2 2/3' quint / Tierce 1 3/5'): dominant energy on the
/// mutation ratio to 8' unison, with a faint fundamental for blend.
[[nodiscard]] synth::SpectralModel makeSpectralMutation(core::Footage footage,
                                                        float         brightness = 1.0f);

/// Dispatch: pick and build the family-appropriate spectral recipe for a
/// compiled @ref Stop (reads family / footage / mixtureComposition / role).
[[nodiscard]] synth::SpectralModel spectralModelForStop(const Stop& stop);

// ---------------------------------------------------------------------------
// The demo instrument.
// ---------------------------------------------------------------------------

/// Build the complete Caecilia demo organ: 3 divisions, ~26 stops, one rank per
/// stop, one windchest per division, inter-division couplers. Off-thread only
/// (allocates); the returned Organ is thereafter immutable and RT-safe to read.
[[nodiscard]] Organ buildCaeciliaDemoOrgan();

/// Sum every engaged stop's family recipe into ONE composite spectrum referenced
/// to 8' unison: each rank's footage is folded into the partial ratios (except the
/// already-unison-referenced mixture/mutation recipes), levels are balanced per
/// tonal family and darkened an octave per pitch step above 8', and a gentle global
/// trim keeps stacked ranks from clipping. A single AdditiveVoice seeded with this
/// (context footage 8') sounds the WHOLE drawn registration cleanly and
/// polyphonically. Off-thread only (allocates). Shared by the plugin's registration
/// rebuild and the headless render/verification harness so both voice identically.
[[nodiscard]] synth::SpectralModel buildRegistrationCompositeSpectrum(
    const Organ& organ, std::span<const core::StopId> engagedStops);

// ---------------------------------------------------------------------------
// Registration -> ready-to-bind voices.
// ---------------------------------------------------------------------------

/**
 * @brief Owned storage for the voices built for one registration, plus the
 *        parallel non-owning pointer array ready to hand to
 *        @c AudioEngine::bindVoices(data(), size()).
 *
 * The raw @ref pointers reference heap-allocated AdditiveVoice objects, so they
 * remain valid when the DemoVoiceBank is moved out of the builder by value.
 */
struct DemoVoiceBank
{
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;   ///< Owned voice storage.
    std::vector<core::IVoice*>                         pointers; ///< Parallel non-owning IVoice* view.

    [[nodiscard]] core::IVoice* const* data() const noexcept { return pointers.data(); }
    [[nodiscard]] std::size_t          size() const noexcept { return pointers.size(); }
    [[nodiscard]] bool                 empty() const noexcept { return pointers.empty(); }
};

/**
 * @brief Construct the AdditiveVoice pool needed to sound a chosen registration.
 *
 * For each engaged stop the builder seeds voices from that stop's family recipe
 * and sets a VoiceContext (wind / tuning / windchest / footage / family / rank).
 * The @p poolCapacity total polyphony is distributed round-robin across the
 * engaged stops. Every voice is @c prepare()d and ready to render; the caller
 * binds them with @c AudioEngine::bindVoices(bank.data(), bank.size()).
 *
 * Off-thread only (allocates). @p wind / @p tuning are non-owning and must
 * outlive the returned voices.
 *
 * @param organ         The compiled demo organ (or any Organ).
 * @param engagedStops  Stop ids currently drawn (couplers pre-expanded elsewhere).
 * @param wind          Wind supply the voices read (may be null in tests).
 * @param tuning        Tuning table the voices read (may be null => tempered).
 * @param sampleRate    Host sample rate.
 * @param maxBlockFrames Largest render block.
 * @param poolCapacity  Total number of voices to allocate across the registration.
 */
[[nodiscard]] DemoVoiceBank buildDemoRegistrationVoices(const Organ&                  organ,
                                                        std::span<const core::StopId> engagedStops,
                                                        const core::IWindSupply*      wind,
                                                        const core::ITuning*          tuning,
                                                        core::SampleRate              sampleRate,
                                                        std::size_t                   maxBlockFrames,
                                                        std::size_t                   poolCapacity);

} // namespace caecilia::model

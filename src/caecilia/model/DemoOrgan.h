// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/PartialBank.h"
#include "caecilia/synthesis/RankVoicing.h"
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
// builder the headless render/audition tools call. The plugin does NOT use that
// builder: it sounds ONE composite spectrum instead (see
// buildRegistrationCompositeSpectrum / buildCompositeFromRegistration).
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

/// One drawn rank as described by the UI: its tonal family and sounding footage
/// (compound = a multi-rank mixture/cornet, whose partials are 8'-referenced).
struct RegistrationRank
{
    core::TonalFamily family   = core::TonalFamily::Principal;
    core::Footage     footage  = core::footage::kEight;
    bool              compound = false;
};

/// Composite spectrum from a UI registration (list of family+footage ranks),
/// decoupled from any specific Organ/StopId so the WebView console can drive the
/// engine directly. Same voicing math as @ref buildRegistrationCompositeSpectrum.
/// Off-thread only (allocates).
[[nodiscard]] synth::SpectralModel buildCompositeFromRegistration(
    std::span<const RegistrationRank> ranks);

/// Nearest standard organ @ref Footage for a decimal length in feet (8.0 -> 8',
/// 2.667 -> 2 2/3', 1.6 -> 1 3/5', ...). Off-thread.
[[nodiscard]] core::Footage footageFromFeet(double feet) noexcept;

// ---------------------------------------------------------------------------
// One rank at a time (ARCH-001).
//
// The composite builder above sums every drawn rank into one spectrum, which one
// voice per note then sounds. That is cheap and it is why the instrument works at
// all today, but it costs the thing an organ is made of: each rank speaks with
// its own timing, sits in its own place in the case, and is tuned as a unit. A
// composite has one of each for the whole registration.
//
// These build the same thing per rank, through the same path, so a per-rank voice
// and the composite are the same sound -- see buildRankVoicing.
// ---------------------------------------------------------------------------

/**
 * @brief How fast a family's pipes speak, before the per-note pitch interpolation.
 *
 * A reed tongue starts almost at once; a wide stopped flute has to fill a
 * substantial volume of air first; a narrow string is the hardest thing on the
 * instrument to get speaking cleanly. Those differences are large -- a factor of
 * three between a Gambe and a Trompette -- and they are most of what makes a
 * registration sound like separate stops rather than one synthesised timbre.
 *
 * @note @c PartialBank::setSpeechProfile has no callers today, so every rank runs
 *       the struct's defaults and the instrument's speech is per-PITCH only.
 *       Consuming this changes how the organ sounds; that happens in 8.2.
 */
[[nodiscard]] synth::SpeechProfile speechProfileFor(core::TonalFamily family,
                                                    core::Footage     footage) noexcept;

/// The voicing itself lives in `synth`: a voice has to be able to read it, and
/// `model` already depends on `synth` so it cannot be the other way round.
using RankVoicing = synth::RankVoicing;

/**
 * @brief Build one rank's voicing, through exactly the composite's path.
 * @return The voicing, or a default-constructed one if @p stop is not on @p organ.
 *
 * Off-thread (allocates).
 */
[[nodiscard]] RankVoicing buildRankVoicing(const Organ& organ, core::StopId stop);

// ---------------------------------------------------------------------------
// Console / host defaults. Pure functions of an Organ, so the plugin does not
// have to carry heuristics the test suite cannot reach.
// ---------------------------------------------------------------------------

/**
 * @brief The division a single-keyboard controller should play.
 *
 * The richest MANUAL — most organs put the principal chorus on it, and a player
 * with one keyboard expects that. Ties break to the lowest division id so the
 * answer does not depend on the order stops were compiled in.
 *
 * Pedal divisions are excluded, and that is the point: choosing "whichever
 * division has the most stops" without the exclusion routes the manual keyboard
 * to the pedalboard on any instrument with a large Pédale, and the player's notes
 * land two octaves down on a compass that stops at 30 keys.
 *
 * Falls back to the first division of any kind if there are no manuals at all.
 * Off-thread.
 */
[[nodiscard]] core::DivisionId primaryManual(const Organ& organ) noexcept;

/**
 * @brief The registration the instrument starts on: a classic opening plenum.
 *
 * The whole principal chorus of @p manual with its mixtures, plus an 8' flute for
 * body. If that draws nothing — an instrument with no principals — the first 8'
 * stop found, because an instrument that comes up silent reads as broken.
 *
 * @return The engaged stop ids, ascending. Off-thread (allocates).
 */
[[nodiscard]] std::vector<core::StopId> defaultOpeningRegistration(const Organ&    organ,
                                                                   core::DivisionId manual);

/// @overload Uses @ref primaryManual.
[[nodiscard]] std::vector<core::StopId> defaultOpeningRegistration(const Organ& organ);

/**
 * @brief Translate the console's family+footage ranks into concrete stop ids.
 *
 * The console has historically spoken in @ref RegistrationRank — a tonal family
 * and a sounding footage — while the host parameters and the organ model speak in
 * @c StopId. Something has to translate, and it has to be deterministic: the same
 * ranks must always resolve to the same stops, or a saved session reopens on a
 * different instrument.
 *
 * Matching is on family, footage and compound-ness. Ties break to the LOWEST
 * unused stop id, and each stop is claimed at most once — so two identical
 * @c {Reed, 8'} ranks resolve to two DIFFERENT stops rather than the same one
 * twice. A rank with no match resolves to nothing at all; guessing at a
 * substitute would make a typo sound like a working registration.
 *
 * @return The matched ids, ascending and duplicate-free. Off-thread (allocates).
 */
[[nodiscard]] std::vector<core::StopId> resolveRanksToStops(
    const Organ& organ, std::span<const RegistrationRank> ranks);

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

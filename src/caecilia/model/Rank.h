// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/model/Pipe.h"
#include "caecilia/model/PipeSpatial.h"
#include "caecilia/model/RankVoicingSpec.h"
#include "caecilia/model/SampleSetDescriptor.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace caecilia::model
{

/**
 * @brief A rank: a homogeneous set of pipes sharing an engine kind, footage,
 *        tonal family and voicing, spanning a note compass on one windchest.
 *
 * A rank is the physical thing a stop *controls*; several stops can share a rank
 * (e.g. a unit organ), which is why @c Stop stores a @c RankId rather than
 * owning pipes. The rank owns its @c Pipe records, built across [lowNote,
 * highNote] at compile time by @ref generatePipes.
 *
 * Immutable once compiled; every accessor a voice touches on the audio thread is
 * @c noexcept and allocation-free. Aliased as @c RankSpec for spec-speaking
 * modules.
 */
class Rank
{
public:
    Rank() = default;

    // --- Identity & semantic metadata --------------------------------------
    [[nodiscard]] core::RankId      id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] core::TonalFamily family() const noexcept { return family_; }
    [[nodiscard]] core::EngineKind  engine() const noexcept { return engine_; }
    [[nodiscard]] core::Footage     footage() const noexcept { return footage_; }
    [[nodiscard]] core::WindchestId windchest() const noexcept { return windchest_; }

    // --- Compass ------------------------------------------------------------
    [[nodiscard]] core::MidiNote lowNote() const noexcept { return lowNote_; }
    [[nodiscard]] core::MidiNote highNote() const noexcept { return highNote_; }

    /// @return true if @p note lies within this rank's compass. RT-safe.
    [[nodiscard]] bool contains(core::MidiNote note) const noexcept
    {
        return note >= lowNote_ && note <= highNote_;
    }

    // --- Pipes --------------------------------------------------------------
    [[nodiscard]] std::size_t pipeCount() const noexcept { return pipes_.size(); }
    [[nodiscard]] const std::vector<Pipe>& pipes() const noexcept { return pipes_; }

    /**
     * @brief The pipe sounding for @p note, or nullptr if outside the compass.
     *
     * Because @ref generatePipes lays pipes out contiguously from @c lowNote,
     * this is an O(1) indexed lookup. RT-safe.
     */
    [[nodiscard]] const Pipe* pipeFor(core::MidiNote note) const noexcept;

    // --- Voicing & samples --------------------------------------------------
    [[nodiscard]] const RankVoicingSpec&    voicing() const noexcept { return voicing_; }
    [[nodiscard]] const SampleSetDescriptor& sampleSet() const noexcept { return sampleSet_; }
    [[nodiscard]] bool hasSamples() const noexcept { return sampleSet_.isValid(); }

    // --- Measured timbre ----------------------------------------------------
    //
    // A rank may be voiced from a RECORDING rather than from the procedural
    // recipe: caecilia-partial-extractor FFTs a real pipe into a partial bank, and
    // a rank that names one sounds from what was measured instead of from family
    // and footage. Everything else still applies on top -- the footage fold, the
    // level calibration, the pipe-to-pipe scatter that makes a rank out of one
    // pipe -- because a measurement is one pipe and a rank is sixty-one.

    /// The measured spectrum, if this rank named one that could be read.
    [[nodiscard]] const std::optional<synth::SpectralModel>& measuredSpectrum() const noexcept
    {
        return measuredSpectrum_;
    }

    /// The reference as written in the organ file, kept so a round trip preserves
    /// it whether or not the file behind it could be resolved.
    [[nodiscard]] const std::string& spectrumFile() const noexcept { return spectrumFile_; }

    // --- Off-thread construction (used by OrganLoader::compile) --------------
    /// @name Builders (NOT real-time safe; called only during load/compile).
    /// @{
    void setId(core::RankId id) { id_ = id; }
    void setName(std::string name) { name_ = std::move(name); }
    void setFamily(core::TonalFamily f) noexcept { family_ = f; }
    void setEngine(core::EngineKind e) noexcept { engine_ = e; }
    void setFootage(core::Footage f) noexcept { footage_ = f; }
    void setWindchest(core::WindchestId w) noexcept { windchest_ = w; }
    void setCompass(core::MidiNote low, core::MidiNote high) noexcept;
    void setVoicing(const RankVoicingSpec& v) { voicing_ = v; }
    void setSampleSet(SampleSetDescriptor s) { sampleSet_ = std::move(s); }
    void setSpectrumFile(std::string path) { spectrumFile_ = std::move(path); }
    void setMeasuredSpectrum(synth::SpectralModel m) { measuredSpectrum_ = std::move(m); }
    void setBaseSpatial(const PipeSpatial& s) { baseSpatial_ = s; }

    /// @return Where this rank sits in the case, before the per-pipe scatter.
    [[nodiscard]] const PipeSpatial& baseSpatial() const noexcept { return baseSpatial_; }

    /**
     * @brief Materialise one @c Pipe per note in the compass.
     * @param referenceA4Hz A-reference used for the nominal equal-temperament
     *        default frequency (the authoritative value comes from ITuning later).
     *
     * Fills each pipe's stable @c PipeId, spatial placement and a footage-scaled
     * nominal frequency. Allocates; call off the audio thread.
     */
    void generatePipes(double referenceA4Hz = 440.0);

    /**
     * @brief Record which division owns this rank, and stamp it onto every pipe.
     * @param division The owning division.
     *
     * A rank cannot know its division when its pipes are generated: a Stop is
     * what ties the two together, and the loader builds every rank before it
     * builds any stop. So @ref generatePipes leaves @c PipeId::divisionId at 0
     * and this runs afterwards, once the stop list exists.
     *
     * Without it every pipe the MODEL produces claims to belong to division 0,
     * while the pipes the MIDI path produces carry their real division -- so the
     * two describe the same physical pipe with different identities, and
     * anything matching on PipeId (voice lookup, coupler activation, per-pipe
     * voicing) silently disagrees with itself.
     *
     * Off-thread only; call after the stops are compiled and before couplers.
     */
    void stampDivision(core::DivisionId division) noexcept;

    /// @return The division that owns this rank, or a default id before
    ///         @ref stampDivision has run.
    [[nodiscard]] core::DivisionId division() const noexcept { return division_; }
    /// @}

private:
    core::RankId        id_{};
    std::string         name_;
    std::string         spectrumFile_;
    std::optional<synth::SpectralModel> measuredSpectrum_;
    core::TonalFamily   family_   = core::TonalFamily::Undefined;
    core::EngineKind    engine_   = core::EngineKind::Additive;
    core::Footage       footage_  = core::footage::kEight;
    core::WindchestId   windchest_{};
    core::MidiNote      lowNote_  = 36;
    core::MidiNote      highNote_ = 96;
    RankVoicingSpec     voicing_{};
    SampleSetDescriptor sampleSet_{};
    PipeSpatial         baseSpatial_{};
    core::DivisionId    division_{};  ///< Set by stampDivision(), not at construction.
    std::vector<Pipe>   pipes_;
};

/// Spec-vocabulary alias: the immutable rank description.
using RankSpec = Rank;

} // namespace caecilia::model

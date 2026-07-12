/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/model/Pipe.h"
#include "caecilia/model/PipeSpatial.h"
#include "caecilia/model/RankVoicingSpec.h"
#include "caecilia/model/SampleSetDescriptor.h"

#include <cstddef>
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
    void setBaseSpatial(const PipeSpatial& s) { baseSpatial_ = s; }

    /**
     * @brief Materialise one @c Pipe per note in the compass.
     * @param referenceA4Hz A-reference used for the nominal equal-temperament
     *        default frequency (the authoritative value comes from ITuning later).
     *
     * Fills each pipe's stable @c PipeId, spatial placement and a footage-scaled
     * nominal frequency. Allocates; call off the audio thread.
     */
    void generatePipes(double referenceA4Hz = 440.0);
    /// @}

private:
    core::RankId        id_{};
    std::string         name_;
    core::TonalFamily   family_   = core::TonalFamily::Undefined;
    core::EngineKind    engine_   = core::EngineKind::Additive;
    core::Footage       footage_  = core::footage::kEight;
    core::WindchestId   windchest_{};
    core::MidiNote      lowNote_  = 36;
    core::MidiNote      highNote_ = 96;
    RankVoicingSpec     voicing_{};
    SampleSetDescriptor sampleSet_{};
    PipeSpatial         baseSpatial_{};
    std::vector<Pipe>   pipes_;
};

/// Spec-vocabulary alias: the immutable rank description.
using RankSpec = Rank;

} // namespace caecilia::model

// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/core/ParameterIds.h"
#include "caecilia/model/Organ.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace caecilia::plugin
{

/**
 * @brief Stable, host-visible parameter identifiers and the factory that builds
 *        the plugin's @c juce::AudioProcessorValueTreeState (APVTS) layout.
 *
 * Parameter IDs are a PUBLIC CONTRACT: a host stores automation and saved
 * sessions against them, so their string values must never change once shipped.
 * Everything the host can automate lives here; the richer semantic registration
 * state (why a stop is on, its provenance, the Selector intent) is destined for a
 * parallel @c juce::ValueTree owned by @ref CaeciliaParameterMirror — that tree is
 * saved and restored, but nothing fills it in yet.
 *
 * ## Why a reserved pool of stop parameters
 * A drawstop is naturally a boolean host parameter, but JUCE forbids adding
 * parameters after the processor is constructed, so the pool has to be fixed in
 * size before any organ is known. It is sized to @ref kMaxStopParameters, which is
 * exactly @c registration::StopSet::kMaskCapacity: a registration that fits in a
 * machine word is one the audio thread can read with a relaxed load per parameter
 * and a single 64-bit compare, instead of a lock.
 *
 * **Slot index IS @c StopId::value.** There is no binding step and no side table
 * mapping one to the other, because there is nothing to get wrong: parameter
 * @c stop_007 is the stop whose id is 7, always. What the organ does supply is the
 * NAMES and the DEFAULTS — @ref create takes the instrument, so a host shows
 * "Pédale Contrebasse 16" rather than "Stop 0", and the opening plenum is the
 * parameters' own default value rather than something applied after construction.
 * That last part is what makes a host's "reset to default" give back a sounding
 * instrument instead of silence.
 *
 * This header only depends on JUCE and the pure @c core temperament enum; it does
 * NOT pull in the engine, so the parameter contract compiles in isolation.
 */
struct ParameterLayout
{
    // --- reserved capacities ------------------------------------------------

    /// @copydoc caecilia::core::params::kMaxStopParameters
    static constexpr std::size_t kMaxStopParameters = core::params::kMaxStopParameters;

    /// @copydoc caecilia::core::params::kMaxCouplerParameters
    static constexpr std::size_t kMaxCouplerParameters = core::params::kMaxCouplerParameters;

    // --- global parameter IDs (stable strings) ------------------------------
    //
    // ALIASES, not copies. The strings themselves live in core/ParameterIds.h,
    // where ParameterIdHashTest can enumerate and pin them without a host
    // framework. Restating them here would give the test a set to guard and the
    // plugin a different set to publish, and they would agree only until somebody
    // edited whichever one they were looking at.

    static constexpr const char* kMasterGainDb     = core::params::kMasterGainDb;
    static constexpr const char* kReverbMix        = core::params::kReverbMix;
    static constexpr const char* kReverbDecaySec   = core::params::kReverbDecaySec;
    static constexpr const char* kReverbPreDelayMs = core::params::kReverbPreDelayMs;
    static constexpr const char* kReverbDampingHz  = core::params::kReverbDampingHz;
    static constexpr const char* kReverbWidth      = core::params::kReverbWidth;
    static constexpr const char* kTremulantOn      = core::params::kTremulantOn;
    static constexpr const char* kTremulantRateHz  = core::params::kTremulantRateHz;
    static constexpr const char* kTremulantDepth   = core::params::kTremulantDepth;
    static constexpr const char* kTemperament      = core::params::kTemperament;
    static constexpr const char* kTuningA4Hz       = core::params::kTuningA4Hz;

    static constexpr const char* kEqOn         = core::params::kEqOn;
    static constexpr const char* kEqWarmthDb   = core::params::kEqWarmthDb;
    static constexpr const char* kEqBoxinessDb = core::params::kEqBoxinessDb;
    static constexpr const char* kEqBodyDb     = core::params::kEqBodyDb;
    static constexpr const char* kEqPresenceDb = core::params::kEqPresenceDb;
    static constexpr const char* kEqAirDb      = core::params::kEqAirDb;

    /// The five band-gain IDs in @c dsp::MasterEq::Band order.
    static constexpr std::array<const char*, 5> kEqBandIds = core::params::kEqBandIds;

    /// Every non-stop parameter, in published order.
    static constexpr std::array<const char*, 17> kGlobalIds = core::params::kGlobalIds;

    /// Parameter-tree version hint; bump only on an incompatible ID/topology change.
    static constexpr int kStateVersion = 1;

    // --- stop-pool ID helpers -----------------------------------------------

    /**
     * @brief Deterministic parameter ID for the @p index-th reserved stop slot.
     * @param index Slot in [0, @ref kMaxStopParameters).
     * @return A stable string such as @c "stop_000". Off-thread use only (it
     *         builds a @c std::string); the audio thread refers to parameters by
     *         cached @c juce::RangedAudioParameter pointers instead.
     */
    [[nodiscard]] static std::string stopParamId(std::size_t index);

    /// @copydoc stopParamId but for the coupler pool, e.g. @c "coupler_03".
    [[nodiscard]] static std::string couplerParamId(std::size_t index);

    /// Total parameters the host sees. Asserted against the layout the factory
    /// actually builds — see @ref create — so growing one without the other fails
    /// at construction rather than in a host.
    static constexpr std::size_t kParameterCount = core::params::kParameterCount;

    // --- layout factory -----------------------------------------------------

    /**
     * @brief Build the full APVTS parameter layout: global controls plus the
     *        boolean stop pool, named and defaulted from a real instrument.
     * @param organ        The compiled instrument. Slot @e i is named after the
     *                     stop whose @c StopId::value is @e i; slots past the end
     *                     are named "(unused)" so a host's parameter list says what
     *                     it is looking at.
     * @param defaultDrawn The registration the instrument opens on, which becomes
     *                     each slot's DEFAULT value. A host "reset to default"
     *                     therefore lands on a sounding plenum rather than silence.
     * @return A layout to hand to the @c juce::AudioProcessorValueTreeState ctor.
     *
     * Off-thread; called once from the processor constructor, which is why the
     * organ has to be constructed before the parameter mirror. Choice ordering for
     * @ref kTemperament follows @c core::TemperamentId exactly, so a stored index
     * maps back to the enum without a translation table.
     */
    [[nodiscard]] static juce::AudioProcessorValueTreeState::ParameterLayout create(
        const model::Organ& organ, std::span<const core::StopId> defaultDrawn);

    /// Human-readable temperament names, index-aligned with @c core::TemperamentId.
    [[nodiscard]] static juce::StringArray temperamentChoices();

    /// Map a stored @ref kTemperament choice index back to the core enum (clamped).
    [[nodiscard]] static core::TemperamentId temperamentFromChoice(int choiceIndex) noexcept;
};

} // namespace caecilia::plugin

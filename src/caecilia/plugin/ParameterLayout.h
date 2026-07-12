/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/ITuning.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cstddef>
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
 * state (why a stop is on, its provenance, the Selector intent) is mirrored in a
 * parallel @c juce::ValueTree owned by @ref CaeciliaParameterMirror, not in APVTS.
 *
 * ## Why a reserved pool of stop parameters
 * A drawstop is naturally a boolean host parameter, but the concrete stop list is
 * only known once an organ is loaded — and JUCE forbids adding parameters after
 * the processor is constructed. We therefore reserve a FIXED pool of
 * @ref kMaxStopParameters generic boolean "stop" parameters at construction and
 * bind them to concrete @c StopId values at organ-load time (see
 * @ref CaeciliaParameterMirror). Unused slots stay inert.
 *
 * This header only depends on JUCE and the pure @c core temperament enum; it does
 * NOT pull in the engine, so the parameter contract compiles in isolation.
 */
struct ParameterLayout
{
    // --- reserved capacities ------------------------------------------------

    /// Number of pre-allocated boolean stop parameters bound at organ load.
    static constexpr std::size_t kMaxStopParameters = 256;

    // --- global parameter IDs (stable strings) ------------------------------

    static constexpr const char* kMasterGainDb    = "master_gain_db";
    static constexpr const char* kReverbMix        = "reverb_mix";
    static constexpr const char* kReverbDecaySec   = "reverb_decay_s";
    static constexpr const char* kReverbPreDelayMs = "reverb_predelay_ms";
    static constexpr const char* kReverbDampingHz  = "reverb_damping_hz";
    static constexpr const char* kReverbWidth      = "reverb_width";
    static constexpr const char* kTremulantOn      = "tremulant_on";
    static constexpr const char* kTremulantRateHz  = "tremulant_rate_hz";
    static constexpr const char* kTremulantDepth   = "tremulant_depth";
    static constexpr const char* kTemperament      = "temperament";
    static constexpr const char* kTuningA4Hz       = "tuning_a4_hz";

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

    // --- layout factory -----------------------------------------------------

    /**
     * @brief Build the full APVTS parameter layout: global controls plus the
     *        reserved boolean stop pool.
     * @return A layout to hand to the @c juce::AudioProcessorValueTreeState ctor.
     *
     * Off-thread; called once from the processor constructor. Choice ordering for
     * @ref kTemperament follows @c core::TemperamentId exactly, so a stored index
     * maps back to the enum without a translation table.
     */
    [[nodiscard]] static juce::AudioProcessorValueTreeState::ParameterLayout create();

    /// Human-readable temperament names, index-aligned with @c core::TemperamentId.
    [[nodiscard]] static juce::StringArray temperamentChoices();

    /// Map a stored @ref kTemperament choice index back to the core enum (clamped).
    [[nodiscard]] static core::TemperamentId temperamentFromChoice(int choiceIndex) noexcept;
};

} // namespace caecilia::plugin

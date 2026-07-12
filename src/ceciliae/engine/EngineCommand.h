/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/IReverb.h"
#include "ceciliae/core/ITuning.h"
#include "ceciliae/core/IWindSupply.h"

#include <cstdint>

namespace ceciliae::core::engine
{

/// Discriminator for the @ref EngineCommand tagged union.
enum class EngineCommandType : std::uint8_t
{
    None = 0,       ///< Empty / default-constructed command (ignored).
    NoteOn,         ///< Start a pipe at a velocity.
    NoteOff,        ///< Release a sounding pipe.
    Panic,          ///< Immediately release/kill every voice (all-notes-off).
    StopEngage,     ///< Draw a stop (informational; audible effect via StateDelta).
    StopDisengage,  ///< Retire a stop.
    ApplyStateDelta,///< Apply a registration StateDelta published off-thread.
    SetTremulant,   ///< Configure a windchest tremulant.
    SetWindParams,  ///< Retune a windchest's nominal pressure / responsiveness.
    SetTemperament, ///< Switch temperament and A-reference.
    SetReverbParams ///< Update the master reverb parameters.
};

/**
 * @brief A single immutable instruction handed to the audio thread over the
 *        SPSC command ring.
 *
 * Cross-thread engine mutation happens EXCLUSIVELY through these commands: the
 * MIDI router, registration engine and host-parameter bridge encode intent into
 * an @c EngineCommand and push it; @c AudioEngine::processBlock drains and
 * applies them at a block boundary, so live engine state is never touched from
 * another thread.
 *
 * The payload is a tagged union keyed by @ref type. The struct is trivially
 * copyable so it can be relocated through the ring by value. Registration
 * StateDeltas are referenced by an opaque @ref StateDeltaPayload::stateDeltaId
 * (an index into a registration-owned, lock-free-published delta table) rather
 * than a raw pointer, keeping ownership on the producing side.
 *
 * Prefer the static factory helpers (@ref noteOn, @ref noteOff, ...) over
 * hand-filling the union.
 */
struct EngineCommand
{
    // --- payload structs (one active per command type) ----------------------

    struct NoteOnPayload
    {
        PipeId     pipe{};
        DivisionId division{};
        Velocity   velocity = 0;
    };

    struct NoteOffPayload
    {
        PipeId     pipe{};
        DivisionId division{};
    };

    struct StopPayload
    {
        StopId     stop{};
        DivisionId division{};
    };

    struct StateDeltaPayload
    {
        std::uint32_t stateDeltaId = 0; ///< Handle into the off-thread delta table.
    };

    struct TremulantPayload
    {
        WindchestId chest{};
        float       rateHz    = 5.5f;
        float       depthNorm = 0.0f;
        bool        enabled   = false;
    };

    struct WindPayload
    {
        WindchestId chest{};
        float       nominalPressurePa = 0.0f;
        float       responsiveness    = 1.0f;
    };

    struct TemperamentPayload
    {
        TemperamentId id           = TemperamentId::Equal;
        double        referenceA4Hz = 440.0;
    };

    struct ReverbPayload
    {
        ReverbParams params{};
    };

    /// The active-member union. Read only the member matching @ref type.
    union Payload
    {
        NoteOnPayload       noteOn;
        NoteOffPayload      noteOff;
        StopPayload         stop;
        StateDeltaPayload   stateDelta;
        TremulantPayload    tremulant;
        WindPayload         wind;
        TemperamentPayload  temperament;
        ReverbPayload       reverb;

        /// Leaves members uninitialised; the enclosing command sets one via a factory.
        Payload() noexcept {}
    };

    EngineCommandType type = EngineCommandType::None;
    Payload           payload{};

    // --- factory helpers ----------------------------------------------------

    [[nodiscard]] static EngineCommand makeNoteOn(PipeId pipe, Velocity velocity,
                                                  DivisionId division) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::NoteOn;
        c.payload.noteOn = NoteOnPayload{pipe, division, velocity};
        return c;
    }

    [[nodiscard]] static EngineCommand makeNoteOff(PipeId pipe, DivisionId division) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::NoteOff;
        c.payload.noteOff = NoteOffPayload{pipe, division};
        return c;
    }

    [[nodiscard]] static EngineCommand makePanic() noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::Panic;
        return c;
    }

    [[nodiscard]] static EngineCommand makeApplyStateDelta(std::uint32_t stateDeltaId) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::ApplyStateDelta;
        c.payload.stateDelta = StateDeltaPayload{stateDeltaId};
        return c;
    }

    [[nodiscard]] static EngineCommand makeTremulant(WindchestId chest, bool enabled,
                                                     float rateHz, float depthNorm) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::SetTremulant;
        c.payload.tremulant = TremulantPayload{chest, rateHz, depthNorm, enabled};
        return c;
    }

    [[nodiscard]] static EngineCommand makeTemperament(TemperamentId id,
                                                       double referenceA4Hz) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::SetTemperament;
        c.payload.temperament = TemperamentPayload{id, referenceA4Hz};
        return c;
    }

    [[nodiscard]] static EngineCommand makeReverbParams(const ReverbParams& params) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::SetReverbParams;
        c.payload.reverb = ReverbPayload{params};
        return c;
    }
};

} // namespace ceciliae::core::engine

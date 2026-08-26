// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IReverb.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/core/IWindSupply.h"

#include <cstdint>
#include <type_traits>

namespace caecilia::core::engine
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
    SetReverbParams,///< Update the master reverb parameters.
    SetSustain,     ///< Sustain pedal (CC 64) up or down, per division.
    SetExpression   ///< Swell-shoe position (CC 11) for one division.
};

/**
 * @brief A single immutable instruction handed to the audio thread over the
 *        SPSC command ring.
 *
 * Cross-thread engine mutation happens EXCLUSIVELY through these commands: the
 * MIDI router, registration engine and host-parameter bridge encode intent into
 * an @c EngineCommand and push it; @c AudioEngine::processBlock drains and
 * applies them, so live engine state is never touched from another thread.
 *
 * Each command carries the frame WITHIN the block at which it takes effect (see
 * @ref sampleOffset), so the engine can render up to that frame before applying
 * it. Producers that do not care about timing leave it at zero and are applied at
 * the top of the block, exactly as everything was before the field existed.
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

    struct SustainPayload
    {
        DivisionId division{};
        bool       down = false;
    };

    struct ExpressionPayload
    {
        DivisionId division{};
        /// Shoe position in [0, 1]: 0 is fully shut, 1 fully open. Not a gain --
        /// the engine maps position to gain, because the mapping is a property of
        /// the instrument rather than of the controller that sent it.
        float      position = 1.0f;
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
        SustainPayload      sustain;
        ExpressionPayload   expression;

        /// Leaves members uninitialised; the enclosing command sets one via a factory.
        Payload() noexcept {}
    };

    EngineCommandType type = EngineCommandType::None;
    /// Frame within the block this command takes effect at, counted from the
    /// block's first frame. Zero means "at the top of the block", which is what
    /// every producer that does not care about timing leaves it at.
    ///
    /// It sits between the tag and the payload deliberately: the tag is one byte
    /// and the union is eight-aligned, so this lands in padding that already
    /// existed and the command stays exactly 32 bytes — the 4096-slot ring is
    /// still 128 KiB.
    std::uint32_t     sampleOffset = 0;
    Payload           payload{};

    // --- factory helpers ----------------------------------------------------

    // Every factory takes the frame offset last and defaults it to zero, so a
    // producer that has no timing information reads exactly as it did before.

    [[nodiscard]] static EngineCommand makeNoteOn(PipeId pipe, Velocity velocity,
                                                  DivisionId division,
                                                  std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::NoteOn;
        c.sampleOffset = sampleOffset;
        c.payload.noteOn = NoteOnPayload{pipe, division, velocity};
        return c;
    }

    [[nodiscard]] static EngineCommand makeNoteOff(PipeId pipe, DivisionId division,
                                                   std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::NoteOff;
        c.sampleOffset = sampleOffset;
        c.payload.noteOff = NoteOffPayload{pipe, division};
        return c;
    }

    [[nodiscard]] static EngineCommand makePanic(std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::Panic;
        c.sampleOffset = sampleOffset;
        return c;
    }

    [[nodiscard]] static EngineCommand makeApplyStateDelta(std::uint32_t stateDeltaId,
                                                           std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::ApplyStateDelta;
        c.sampleOffset = sampleOffset;
        c.payload.stateDelta = StateDeltaPayload{stateDeltaId};
        return c;
    }

    [[nodiscard]] static EngineCommand makeTremulant(WindchestId chest, bool enabled,
                                                     float rateHz, float depthNorm,
                                                     std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::SetTremulant;
        c.sampleOffset = sampleOffset;
        c.payload.tremulant = TremulantPayload{chest, rateHz, depthNorm, enabled};
        return c;
    }

    [[nodiscard]] static EngineCommand makeTemperament(TemperamentId id,
                                                       double referenceA4Hz,
                                                       std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::SetTemperament;
        c.sampleOffset = sampleOffset;
        c.payload.temperament = TemperamentPayload{id, referenceA4Hz};
        return c;
    }

    [[nodiscard]] static EngineCommand makeReverbParams(const ReverbParams& params,
                                                        std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::SetReverbParams;
        c.sampleOffset = sampleOffset;
        c.payload.reverb = ReverbPayload{params};
        return c;
    }

    [[nodiscard]] static EngineCommand makeSustain(DivisionId division, bool down,
                                                   std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::SetSustain;
        c.sampleOffset = sampleOffset;
        c.payload.sustain = SustainPayload{division, down};
        return c;
    }

    [[nodiscard]] static EngineCommand makeExpression(DivisionId division, float position,
                                                      std::uint32_t sampleOffset = 0) noexcept
    {
        EngineCommand c;
        c.type = EngineCommandType::SetExpression;
        c.sampleOffset = sampleOffset;
        c.payload.expression = ExpressionPayload{division, position};
        return c;
    }
};

// One canary, here rather than copied into the test files: the command travels
// through a 4096-slot ring on the audio thread's critical path, and a member that
// pushes it past 32 bytes doubles that ring's cache footprint without anyone
// noticing. sampleOffset was fitted into the tag's existing padding for exactly
// this reason.
static_assert(sizeof(EngineCommand) <= 32,
              "EngineCommand must stay within 32 bytes: the command ring's size "
              "and cache behaviour are budgeted on it");
static_assert(std::is_trivially_copyable_v<EngineCommand>,
              "EngineCommand is memcpy-relocated between threads by SpscRing");

} // namespace caecilia::core::engine

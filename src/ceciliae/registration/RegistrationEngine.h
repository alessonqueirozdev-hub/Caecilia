/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/registration/Explanation.h"
#include "ceciliae/registration/PlenumBuilder.h"
#include "ceciliae/registration/RegistrationCommand.h"
#include "ceciliae/registration/RegistrationHistory.h"
#include "ceciliae/registration/RegistrationIntent.h"
#include "ceciliae/registration/RegistrationState.h"
#include "ceciliae/registration/Selector.h"
#include "ceciliae/registration/StateDelta.h"
#include "ceciliae/registration/StopSet.h"

#include <cstdint>
#include <unordered_map>

namespace ceciliae::model { class Organ; }

namespace ceciliae::registration
{

/**
 * @brief The registration brain's public contract: a terminating, deterministic,
 *        off-thread engine that folds @c RegistrationCommand s into state and
 *        publishes atomic @c StateDelta s.
 *
 * Kept as an interface so the control, midi, ui and plugin modules can drive one
 * shared brain without depending on its concrete storage. Every mutating call
 * returns the @c StateDelta the caller pushes over the SPSC ring to the audio
 * engine for a click-free, wind-modelled stop crossfade.
 *
 * Nothing here is real-time safe: the whole engine runs off the audio thread.
 */
class IRegistrationEngine
{
public:
    virtual ~IRegistrationEngine() = default;

    /// Fold @p command into the current state; @return the delta to apply on audio.
    [[nodiscard]] virtual StateDelta apply(const RegistrationCommand& command) = 0;

    /// Step back one node in history; @return the delta from current to that state.
    [[nodiscard]] virtual StateDelta undo() = 0;
    /// Step forward one node in history; @return the delta from current to that state.
    [[nodiscard]] virtual StateDelta redo() = 0;

    /// @return the live registration.
    [[nodiscard]] virtual const RegistrationState& currentState() const noexcept = 0;

    /// Resolve @p selector against the loaded organ and current state.
    [[nodiscard]] virtual StopSet query(const Selector& selector) const = 0;

    /// @return why @p stop is in its current state (its provenance).
    [[nodiscard]] virtual Explanation explain(core::StopId stop) const = 0;

    /// Port @p intent onto the loaded organ (does not apply it).
    [[nodiscard]] virtual PortResult port(const RegistrationIntent& intent) const = 0;
};

/**
 * @brief The concrete registration engine.
 *
 * Owns the current state, the branching history, the provenance table and a
 * pluggable @c IPlenumBuilder (a @c DefaultPlenumBuilder unless overridden).
 * The engine is bound to one loaded organ via @ref setOrgan; commands referring
 * to selectors/plenums resolve against that organ.
 */
class RegistrationEngine final : public IRegistrationEngine
{
public:
    RegistrationEngine();

    /**
     * @brief Bind the engine to a loaded organ and reset to an empty registration.
     * @param spec Non-owning pointer to the immutable organ (must outlive the
     *             engine, or be re-bound). Passing nullptr detaches.
     *
     * Off-thread configuration step. Selector/plenum commands are no-ops while no
     * organ is bound (they resolve to the empty set).
     */
    void setOrgan(const model::Organ* spec);

    /// Replace the default plenum builder (non-owning; must outlive the engine).
    void setPlenumBuilder(const IPlenumBuilder& builder) noexcept { plenumBuilder_ = &builder; }

    // --- IRegistrationEngine ------------------------------------------------
    [[nodiscard]] StateDelta apply(const RegistrationCommand& command) override;
    [[nodiscard]] StateDelta undo() override;
    [[nodiscard]] StateDelta redo() override;
    [[nodiscard]] const RegistrationState& currentState() const noexcept override { return state_; }
    [[nodiscard]] StopSet query(const Selector& selector) const override;
    [[nodiscard]] Explanation explain(core::StopId stop) const override;
    [[nodiscard]] PortResult port(const RegistrationIntent& intent) const override;

    // --- History & capture --------------------------------------------------
    [[nodiscard]] const RegistrationHistory& history() const noexcept { return history_; }
    [[nodiscard]] bool canUndo() const noexcept { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return history_.canRedo(); }

    /// @return the current registration as a literal snapshot (for a piston).
    [[nodiscard]] RegistrationState captureSnapshot() const { return state_; }
    /// @return the current registration as a portable intent (for a piston).
    [[nodiscard]] RegistrationIntent captureIntent() const;

private:
    /// Fold one command onto @p base, returning the resulting state.
    [[nodiscard]] RegistrationState computeResult(const RegistrationState& base,
                                                  const RegistrationCommand& command) const;

    /// Move to @p newState, recording provenance, and return the delta to apply.
    StateDelta commitState(RegistrationState newState, const RegistrationCommand& command);

    /// Rebuild the provenance table for @p newState under @p command.
    void updateProvenance(const RegistrationState& previous,
                          const RegistrationState& newState,
                          const RegistrationCommand& command);

    /// @return the division a stop belongs to, or nullopt if unknown/no organ.
    [[nodiscard]] bool stopInDivision(core::StopId stop, core::DivisionId division) const;

    const model::Organ*   spec_ = nullptr;
    RegistrationState     state_;
    RegistrationHistory   history_;
    DefaultPlenumBuilder  defaultPlenumBuilder_;
    const IPlenumBuilder* plenumBuilder_ = nullptr;

    /// Provenance keyed by StopId::value. Absent entry == engaged-by-default/unknown.
    std::unordered_map<std::uint16_t, Explanation> provenance_;
};

} // namespace ceciliae::registration

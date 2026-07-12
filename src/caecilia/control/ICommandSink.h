/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/control/ControlCommand.h"
#include "caecilia/control/ControlResult.h"
#include "caecilia/control/GrammarVersion.h"

namespace caecilia::control
{

/**
 * @brief The single seam through which ALL control traffic enters the engine.
 *
 * The control module contains no registration or playback logic of its own —
 * it only decodes wire formats into a @c ControlCommand and hands it to an
 * @c ICommandSink. The concrete implementation lives on the engine side (it
 * wraps the off-thread @c registration engine plus the note-event path): it
 * parses @c ControlCommand::selector once with the shared
 * @c registration::SelectorParser, derives a new @c RegistrationState, and
 * pushes the resulting @c registration::StateDelta across the audio seam via
 * the SPSC command ring. Query opcodes are answered by reading the off-thread
 * registration model and filling @c ControlResult::payload.
 *
 * Because every transport (OSC, JSON-RPC, in-proc, MIDI-learn bridge) funnels
 * through this ONE interface, the whole product is programmable with zero
 * behavioural drift between control surfaces.
 *
 * @note All calls happen OFF the audio thread and may allocate. The sink is the
 *       boundary that guarantees nothing here ever touches the RT path directly.
 */
class ICommandSink
{
public:
    virtual ~ICommandSink() = default;

    /**
     * @brief Validate and enact @p command.
     * @return A @c ControlResult carrying the outcome status and, for query
     *         opcodes, a structured @c payload.
     *
     * Implementations must not throw; failures are reported through
     * @c ControlResult::status. Off-thread only.
     */
    [[nodiscard]] virtual ControlResult submit(const ControlCommand& command) = 0;

    /**
     * @brief The control-surface / selector-grammar version this sink honours.
     *
     * Used by transports during a client handshake so a script can refuse to
     * run against an incompatible grammar (see @c GrammarVersion::supports).
     */
    [[nodiscard]] virtual GrammarVersion grammarVersion() const noexcept { return kGrammarVersion; }
};

} // namespace caecilia::control

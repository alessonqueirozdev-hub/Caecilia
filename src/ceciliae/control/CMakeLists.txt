# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# control module : the programmability / automation surface.
#
# Contributes to the pure ceciliae_core static library (standard-library only,
# NO JUCE and NO networking dependency). It defines the transport-agnostic
# command/response schema (ControlCommand / ControlResult), the bus seams
# (ICommandSink, IStatePublisher, IControlTransport / IControlEndpoint), the
# ControlServer hub, and an in-proc LoopbackTransport so the whole surface is
# headless-unit-testable. The OSC and JSON-RPC transports are dependency-free
# codec stubs (concrete socket backends are isolated // TODO(phase8) shells) so
# no heavy network deps ever leak into core.
#
# The selector grammar carried in ControlCommand::selector is the SAME versioned
# public contract the registration, midi and ui modules speak; it is parsed by
# registration::SelectorParser on the sink side, never here.
# ---------------------------------------------------------------------------

target_sources(ceciliae_core PRIVATE
    ControlCommand.cpp
    ControlResult.cpp
    ControlServer.cpp
    LoopbackTransport.cpp
    OscControlTransport.cpp
    JsonRpcControlTransport.cpp)

# Public headers carry the module's contract. Listed for IDE discoverability and
# consumed via the project-wide include root (<root>/src):
#   #include "ceciliae/control/ControlServer.h"
set(CECILIAE_CONTROL_PUBLIC_HEADERS
    GrammarVersion.h
    ControlCommand.h
    ControlResult.h
    ICommandSink.h
    IStatePublisher.h
    IControlTransport.h
    ControlServer.h
    LoopbackTransport.h
    OscControlTransport.h
    JsonRpcControlTransport.h)

target_sources(ceciliae_core PUBLIC
    FILE_SET ceciliae_control_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CECILIAE_CONTROL_PUBLIC_HEADERS})

<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# caecilia::control

The **programmability / automation surface**. Thin transport codecs over **one**
command bus, so external tools — Talon-style voice control, scripts, custom
hardware, accessibility tech — can drive Caecilia's registration brain and
playback the same way the UI does.

This module contributes to the pure `caecilia_core` static library
(standard-library only, **no JUCE**, **no networking dependency**) and is fully
headless-unit-testable via the in-proc `LoopbackTransport`.

## Design in one sentence

Every control surface decodes its wire format into a transport-agnostic
`ControlCommand`, hands it to the **one** `ICommandSink`, and receives state
changes back through the **one** `IStatePublisher` — so OSC, JSON-RPC, in-proc
and (via a bridge) MIDI-learn all drive identical behaviour with zero drift.

```
  OSC datagram  ─┐
  JSON-RPC msg  ─┼─▶ IControlTransport ─▶ IControlEndpoint ─▶ ICommandSink
  in-proc call  ─┘   (decode)             (ControlServer)      (engine adapter:
                                                                registration +
                                                                playback)
                                        ◀── IStatePublisher ◀── settled state
       (encode feedback: OSC / JSON-RPC notification / MIDI LED / UI)
```

The adapters contain **no registration logic** — they only encode/decode. All
registration reasoning (selector parsing, rules, undo/redo, plenum building,
`StateDelta` emission) lives in `caecilia::registration` behind the
`ICommandSink` seam and runs **off the audio thread**; the audio thread only
ever receives the fixed-capacity `registration::StateDelta` the sink pushes over
the engine's SPSC command ring.

## The versioned public contract

`GrammarVersion` (see `GrammarVersion.h`) is the contract external scripts bind
to, from day one:

- the **selector grammar** — the textual mini-language (`family:reed & pitch:8 &
  div:swell`) carried verbatim in `ControlCommand::selector` and parsed by
  `registration::SelectorParser`. It mirrors `registration::GrammarVersion` v1.0.
- the **control protocol** — the `ControlOpcode` verb set, the OSC address space,
  and the JSON-RPC method names, all generated from the single `opcodeName`
  table so they can never disagree.

`ICommandSink::grammarVersion()` reports it for the client handshake; a script
built against an incompatible major refuses to run (`GrammarVersion::supports`).

## Contents

| Type | Header | Role |
| --- | --- | --- |
| `ControlCommand` / `ControlOpcode` | `ControlCommand.h` | The one decoded, transport-agnostic request. Verb + small opcode-dependent payload; selector carried as shared grammar text. |
| `ControlResult` / `ControlStatus` | `ControlResult.h` | The response: status, echoed `requestId`, human message, and a structured `payload` for query answers. |
| `GrammarVersion` | `GrammarVersion.h` | The versioned selector-grammar + protocol contract. |
| `ICommandSink` | `ICommandSink.h` | The single seam into the engine (registration + playback). Implemented on the engine side. |
| `IStatePublisher` / `IStateObserver` / `StateEvent` | `IStatePublisher.h` | Outbound fan-out of settled state changes to feedback observers. |
| `IControlTransport` / `IControlEndpoint` | `IControlTransport.h` | The pluggable transport interface + the inbound endpoint it dispatches to. |
| `ControlServer` | `ControlServer.h` | The hub: forwards commands to the sink, fans state to observers, owns the transport list. |
| `LoopbackTransport` | `LoopbackTransport.h` | In-proc, wire-free reference transport + test harness. |
| `OscControlTransport` | `OscControlTransport.h` | OSC address-space adapter (codec seam real; UDP backend `TODO(phase8)`). |
| `JsonRpcControlTransport` | `JsonRpcControlTransport.h` | JSON-RPC 2.0 adapter (routing real; JSON/socket backend `TODO(phase8)`). |

## Usage sketch (in-proc / test)

```cpp
#include "caecilia/control/ControlServer.h"
#include "caecilia/control/LoopbackTransport.h"

// `sink` is the engine-side adapter implementing ICommandSink.
caecilia::control::ControlServer server{sink};

caecilia::control::LoopbackTransport loop;
server.addTransport(loop);   // connects it + subscribes it for feedback
server.start();

using caecilia::control::ControlCommand;
auto r1 = loop.send(ControlCommand::engage("family:reed & pitch:8", /*id*/ 1));
auto r2 = loop.send(ControlCommand::resolve("family:principal",      /*id*/ 2));
// r2.payload holds the resolved stop list; feedback lands in loop.capturedEvents()
```

## Real-time contract

Nothing in `control` runs on the audio thread. Transports do I/O, `ControlServer`
manages `std::vector`s of observers/transports, and `ControlCommand`/`StateEvent`
may hold `std::string` — all deliberately off-thread. The RT boundary is the
`ICommandSink`, whose implementation is the only thing that touches the SPSC
ring; from there on only a trivially-copyable `registration::StateDelta` crosses
into the audio callback.

## Status / roadmap notes

The scaffold wires the entire bus end-to-end with coherent, compilable bodies.
`// TODO(phase8)` items are the remaining depth work, all behind stable public
signatures:

- **`phase8`** — concrete UDP backend + OSC type-tag argument parser in
  `OscControlTransport`; TCP/WebSocket backend + JSON parse/serialise in
  `JsonRpcControlTransport`; numeric-argument decoding; a MIDI-learn → `ControlCommand`
  bridge so learned controls funnel through the same sink; `RegistrationIntent`
  `port()` exposure as a query opcode.

These stubs pull **no** third-party socket or JSON dependency into `caecilia_core`
by design; the concrete network shells are isolated so the pure command algebra
stays headless-testable.

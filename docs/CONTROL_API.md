<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Ceciliae — Control API Reference

The programmability surface: how external tools (Talon-style voice control,
scripts, custom hardware, accessibility software) drive Ceciliae through **one**
command bus, spoken over three transports — **OSC**, **JSON-RPC 2.0** and
**MIDI** — that all share the same verbs and the same selector grammar. Zero
behavioural drift between surfaces, by construction.

> Scope: complements [`REGISTRATION.md`](REGISTRATION.md) (the brain these
> transports drive) and [`ARCHITECTURE.md`](ARCHITECTURE.md) §6. The selector
> grammar (`family:reed & pitch:8 & div:swell`) is specified in
> [`REGISTRATION.md`](REGISTRATION.md) §4; this document is the *transport*
> reference.

---

## 1. Architecture — one bus, thin codecs

Adapters contain **no** registration logic. They only encode/decode a wire format
into a transport-neutral `control::ControlCommand` and submit it through the one
`ICommandSink`; state changes fan back out through one `IStatePublisher`.

```
  OSC (UDP)        ┐                                   ┌─▶ IStateObserver (OSC feedback)
  JSON-RPC (TCP/WS)├─▶ IControlTransport ─▶ ControlServer ─┤  (JSON-RPC notification)
  MIDI-learn bridge┘        (decode)      │  (endpoint)  └─▶ (MIDI LEDs, UI mirror)
                                          ▼
                                     ICommandSink.submit(ControlCommand)   [OFF audio thread]
                                          │  parses selector ONCE via registration::SelectorParser
                                          │  → RegistrationEngine → StateDelta
                                          ▼
                                     SPSC ring ─▶ AudioEngine   (click-free, wind-modeled)
```

- `IControlTransport` is a pure codec + I/O shell; it is *also* an
  `IStateObserver`, so `ControlServer::publish` hands it settled state changes to
  encode as feedback.
- `ControlServer` owns nothing domain-specific: it forwards commands to the sink
  and fans events to observers. Adding a control surface = implement
  `IControlTransport` + `addTransport`.
- All transport I/O lives **off the audio thread**; the RT path only ever sees
  the fixed-capacity `registration::StateDelta` the sink emits downstream.

---

## 2. Versioning — a public contract

External automation binds to two things that must never silently drift:

1. the textual **selector grammar** (owned by `registration::SelectorParser`,
   `GrammarVersion{1,0}`), and
2. the **control protocol** — the `ControlOpcode` verbs, OSC addresses and
   JSON-RPC method names.

`control::GrammarVersion { major, minor, protocol }` versions both. A client
negotiates during its handshake:

```cpp
bool ok = server.grammarVersion().supports(clientVersion);
// supports := major == client.major && minor >= client.minor && protocol >= client.protocol
```

This build speaks `kGrammarVersion = {1, 0, 1}`. Bump `major` for a breaking
grammar change, `minor` for an additive grammar change, `protocol` for an
additive control-surface change (a new opcode/address/method). The opcode
numbering is a contract and only ever grows additively.

---

## 3. The verb set (`ControlOpcode`)

Every OSC address and every JSON-RPC method decodes to exactly one opcode. The
canonical dotted token (`opcodeName`) is the single name the OSC and JSON-RPC
maps derive from.

| Opcode (`0x`)          | Token                  | Payload (fields of `ControlCommand`)         |
|------------------------|------------------------|----------------------------------------------|
| `EngageStops` 0100     | `stops.engage`         | `selector`                                   |
| `DisengageStops` 0101  | `stops.disengage`      | `selector`                                   |
| `ToggleStops` 0102     | `stops.toggle`         | `selector`                                   |
| `ClearAll` 0103        | `stops.clear` *(all)*  | —                                            |
| `BuildPlenum` 0104     | `stops.plenum`         | `selector` (scopes the chorus)               |
| `EngageCoupler` 0110   | `coupler.engage`       | `targetId` (CouplerId)                       |
| `DisengageCoupler` 0111| `coupler.disengage`    | `targetId`                                   |
| `ToggleCoupler` 0112   | `coupler.toggle`       | `targetId`                                   |
| `RecallGeneral` 0120   | `general.recall`       | `index`                                      |
| `StoreGeneral` 0121    | `general.store`        | `index`                                      |
| `RecallDivisional` 0122| `divisional.recall`    | `division`, `index`                          |
| `StoreDivisional` 0123 | `divisional.store`     | `division`, `index`                          |
| `SequencerNext` 0130   | `sequencer.next`       | —                                            |
| `SequencerPrev` 0131   | `sequencer.prev`       | —                                            |
| `SequencerFirst` 0132  | `sequencer.first`      | —                                            |
| `SequencerLast` 0133   | `sequencer.last`       | —                                            |
| `SetCrescendoStep` 0134| `crescendo.set`        | `index`                                      |
| `Undo` 0140            | `history.undo`         | —                                            |
| `Redo` 0141            | `history.redo`         | —                                            |
| `NoteOn` 0200          | `note.on`              | `division`, `note`, `velocity`               |
| `NoteOff` 0201         | `note.off`             | `division`, `note`                           |
| `AllNotesOff` 0202     | `note.alloff`          | —                                            |
| `Panic` 0203           | `note.panic`           | —                                            |
| `ResolveSelector` 0300 | `query.resolve`        | `selector` → `payload`                       |
| `GetRegistrationState` 0301 | `query.state`     | → `payload`                                  |
| `ExplainStop` 0302     | `query.explain`        | `targetId` (StopId) → `payload`              |
| `DescribeOrgan` 0303   | `query.organ`          | → `payload`                                  |
| `Ping` 0400            | `meta.ping`            | `requestId` echoed                           |
| `GetGrammarVersion` 0401 | `meta.grammar`       | → `payload`                                  |

*(Token spellings above are illustrative of the dotted `opcodeName` scheme; the
canonical token for each opcode is whatever `opcodeName(opcode)` returns, and both
transports derive their names from it so they can never diverge.)*

`ControlCommand::isQuery()` distinguishes read-only verbs; the answer to a query
travels in `ControlResult::payload`.

---

## 4. Result & status (`ControlResult` / `ControlStatus`)

One status vocabulary serves every transport (it maps cleanly onto both an OSC
`/reply` code and a JSON-RPC error object):

| Status        | Code | Meaning                                          |
|---------------|------|--------------------------------------------------|
| `Ok`          | 0    | applied / answered                               |
| `Accepted`    | 1    | valid, queued; completes asynchronously          |
| `BadRequest`  | 100  | malformed / missing arguments                    |
| `ParseError`  | 101  | the selector text failed to parse                |
| `UnknownOpcode`| 102 | verb not in this protocol version                |
| `NotFound`    | 103  | referenced stop/division/combination is unknown  |
| `Unsupported` | 104  | valid verb this build cannot yet service         |
| `NotReady`    | 200  | no organ loaded / sink not bound                 |
| `Truncated`   | 201  | succeeded, but the delta overflowed capacity     |
| `InternalError`| 202 | unexpected failure inside the sink               |

`ControlResult { status, requestId, message, payload }` echoes `requestId` so a
client that pipelines requests can correlate replies. Sinks never throw; failures
are reported through `status`.

---

## 5. OSC transport (`OscControlTransport`)

The OSC address space is derived **mechanically** from `opcodeName` — replace
`'.'` with `'/'` under a common `/ceciliae` root — so it can never drift from the
other transports (`addressFor(opcode)`).

```
; --- inbound ---
/ceciliae/stops/engage      "family:reed & pitch:8"        ; s
/ceciliae/stops/disengage   "div:swell"                    ; s
/ceciliae/note/on           <division:int> <note:int> <vel:int>
/ceciliae/note/off          <division:int> <note:int>
/ceciliae/general/recall    <index:int>
/ceciliae/sequencer/next
/ceciliae/query/resolve     "family:reed"                  ; -> /ceciliae/reply
/ceciliae/meta/ping         <requestId:int>

; --- outbound feedback ---
/ceciliae/state/stop        <stopId:int> <engaged:int> "Great Principal 8'"
/ceciliae/reply             <requestId:int> <status:string> "<payload>"
```

The codec seam (`decodeAddress(address, selectorArg)` / `encodeFeedback`) is pure
and dependency-free, so it is unit-testable without a socket. Default listen port
`9000`.

> Status: the OSC codec seam is real and testable; the concrete UDP backend
> (`start`/`stop` opening a socket) lands in **v0.8** as an isolated shell so
> **no networking dependency is pulled into `ceciliae_core`.**

---

## 6. JSON-RPC 2.0 transport (`JsonRpcControlTransport`)

Methods are named `ceciliae.<token>`, where `<token>` is exactly `opcodeName`, so
the method set never drifts from the OSC address space.

```jsonc
// engage
--> {"jsonrpc":"2.0","id":7,"method":"ceciliae.stops.engage",
     "params":{"selector":"family:reed & pitch:8"}}
<-- {"jsonrpc":"2.0","id":7,"result":{"status":"ok"}}

// query (answer in result.payload)
--> {"jsonrpc":"2.0","id":8,"method":"ceciliae.query.resolve",
     "params":{"selector":"family:principal"}}
<-- {"jsonrpc":"2.0","id":8,"result":{"status":"ok","payload":"[...]"}}

// note event
--> {"jsonrpc":"2.0","id":9,"method":"ceciliae.note.on",
     "params":{"division":1,"note":60,"velocity":100}}
<-- {"jsonrpc":"2.0","id":9,"result":{"status":"ok"}}

// failure → status maps to a JSON-RPC error object
<-- {"jsonrpc":"2.0","id":9,"error":{"code":101,"message":"parseError: ..."}}

// outbound state change → JSON-RPC notification (no id)
<-- {"jsonrpc":"2.0","method":"ceciliae.event.stop",
     "params":{"stopId":42,"engaged":true,"label":"Great Principal 8'"}}
```

`handleMessage(requestJson)` wires decode → dispatch → encode end-to-end
(`methodFor(opcode)` / `opcodeForMethod(method)`); a valid notification (no `id`)
returns an empty response. Default listen port `9010`.

> Status: routing and `status → error` mapping are real; the JSON
> parse/serialise steps land in **v0.8** — **no third-party JSON library is
> pulled into `ceciliae_core`.**

The pure command algebra stays headless-testable through `LoopbackTransport`, an
in-proc transport with no OS glue.

---

## 7. MIDI control (`ceciliae::midi`)

MIDI is a first-class control surface: raw bytes become either voice events or
`RegistrationCommand`s, honouring the user's real workflow. The plugin feeds
core-native `MidiEvent`s; `midi` emits typed commands onto the SPSC ring. All
binding *edits* happen off the audio thread; the audio thread only reads the
resolved tables.

### 7.1 MIDI-learn — same identity as OSC/UI

A learned control carries the **same** selector-grammar semantic identity as the
UI and OSC — a MIDI-learned drawstop is indistinguishable from one toggled
anywhere else. The capture machine `MidiLearn` runs off-thread:

```
UI picks a target → MidiLearn.arm(RegistrationCommandTemplate)   // carries selector identity
next suitable controller → MidiLearn.observe(ev) → Captured
takeCaptured() → MidiLearnBinding installed into the live MidiMap
```

```cpp
struct MidiLearnBinding {
  MidiSource source;                     // CC / note / program to listen to
  RegistrationCommandTemplate command;   // semantic action (shared grammar identity)
  uint8_t triggerThreshold = 64;         // CC on/off gate
  bool shouldFire(const MidiEvent&) const noexcept;   // RT-safe predicate
};
```

Notes and program changes fire on the "on" edge; continuous controllers fire when
the value crosses `triggerThreshold`. The binding table is trivially copyable so
it can be published to the audio thread atomically.

### 7.2 Sequencer navigation (si5 / do6 → Previous / Next)

`SequencerNavMap` binds keys to combination-sequencer navigation — the classic
"page-turner" workflow. The user's Casio defaults are preloaded by
`withUserDefaults()`:

| Key   | MIDI note | Direction   |
|-------|-----------|-------------|
| si5   | 83        | `Previous`  |
| do6   | 84        | `Next`      |

The router **swallows** nav keys so they never sound a pipe
(`lookup(channel, note)` / `isNavKey(...)`, RT-safe).

### 7.3 Program Change → Generals (`ProgramChangeMap`)

The user's workflow is PC → Generals: program `p` recalls general combination
`p` (offset by `generalBase`). A channel bound to a division instead recalls that
division's divisional `p`:

```cpp
map.setDefaultMode(RecallGeneral);
map.mapChannelToDivision(channel, divisionId);   // that channel → RecallDivisional
RegistrationCommandTemplate t = map.resolve(channel, program);   // RT-safe pure lookup
```

Other `midi` pieces: `ChannelToDivisionMap` (channel → manual), `VelocityCurve`,
`MidiRouter` (raw MIDI → typed intent).

---

## 8. Worked examples

```bash
# Talon / shell over JSON-RPC: draw the full Swell reed chorus
ceciliae.stops.engage  {"selector":"family:reed & div:swell"}

# Build the Great plenum, mutations excluded (default), scoped by selector
ceciliae.stops.plenum  {"selector":"div:great"}

# Ask what a selector currently matches (query → payload)
ceciliae.query.resolve {"selector":"family:principal & pitch:8"}

# Why is stop 42 on? (provenance)
ceciliae.query.explain {"targetId":42}
```

```
# OSC equivalents
/ceciliae/stops/engage  "family:reed & div:swell"
/ceciliae/stops/plenum  "div:great"
/ceciliae/query/resolve "family:principal & pitch:8"
```

---

## 9. Real-time safety summary

| Concern       | Guarantee                                                                     |
|---------------|-------------------------------------------------------------------------------|
| Thread        | All transports, the server and the sink run **off the audio thread**          |
| Audio seam    | Only the fixed-capacity `registration::StateDelta` crosses the SPSC ring       |
| MIDI hot path | The router reads only the *published, immutable* binding tables (`noexcept`)   |
| No throw      | Sinks/transports report failures via `ControlStatus`, never exceptions         |
| Dependencies  | Socket/JSON glue is isolated in shells; `ceciliae_core` stays dependency-free  |

---

## 10. Roadmap touchpoints

- **v0.8 — control API + MIDI-learn.** OSC + JSON-RPC concrete backends over the
  one `ICommandSink`/`SelectorParser` (versioned grammar); MIDI-learn bindings
  carrying the shared identity; PC→Generals and si5/do6 sequencer nav honoured.

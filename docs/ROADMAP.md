<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Roadmap

Where Caecilia is going. Nothing here has a date, and the order can change as we
learn what the ear asks for. Want to take something on? Open an issue and say so,
even if you're not sure you can finish it.

## Now: the sound

The main work today. Caecilia's voices are clean and steady, which is exactly what
a real organ is not.

- [x] **Space:** each pipe speaks from its own place on the windchest, with the
  room's first reflections. It needs ears on it: tell us how it sounds.
- [x] **Air and attack:** the breath of a flue pipe, each partial's own attack, wind
  jitter, and no note the same twice. Its values are judgements that want setting
  by ear.
- [ ] **Per-pipe voicing:** level, detune, brightness, air and chiff for every pipe,
  saved with the organ.
- [ ] **Division boxes:** each division's own EQ, a small box reverb, and how much of
  it reaches the room.
- [ ] **Real rooms:** long convolution with church impulse responses.
- [ ] **Comparison against recordings:** tests that hold a stop to a reference.
- [ ] **Physical models:** flue and reed pipes as a further voice tier, and samples
  as another.

*Help wanted:* organists to listen and report, recordings with compatible licences,
people who know pipe acoustics.

## Next: everywhere

- [ ] Linux and macOS releases, with the console. Manguebyte needs X11 and Cocoa
  windows first, which come after the sound work; the installers are in
  [pull request #2](https://github.com/alessonqueirozdev-hub/Caecilia/pull/2).
- [ ] A signed Windows installer
- [ ] Testing in real DAWs and on real hardware, with a table of what's been tried
- [ ] Depend on Manguebyte by version or git revision, so one clone is enough to build

*Help wanted:* Linux and macOS users, DAW users of any kind.

## Then: the console

- [ ] A resizable console that scales to any window
- [ ] A compact mode for half a screen, beside a score open in another program
- [ ] An output level meter
- [ ] A plenum button
- [ ] The interface in Portuguese, English, Spanish and German
- [ ] Screen readers, on top of the accessibility tree that already exists

*Help wanted:* translators, and blind or low-vision musicians who'd like to tell us
what they need.

## Then: the organs

- [ ] Three manuals and pedal, and larger specifications
- [ ] Monophonic couplers, bass and treble
- [ ] Expression pedals assignable stop by stop, and a crescendo pedal you can program
- [ ] A library of organs: German Baroque, French Romantic, historical and modern
- [ ] Routing to several outputs

*Help wanted:* anyone who can write an [organ file](ORGAN_FILE.md).

## Already there

Modeled voices with living wind, tremulant, the Caecilia Pipe Organ, organ files that
change live, a native console in night and day light, selectors with branching undo,
general pistons, MIDI learn, keys that switch one keyboard between manuals, eight
temperaments, room reverb and a master EQ, Bach demonstrations, a standalone app,
VST3 and CLAP plugins, and a Windows installer.

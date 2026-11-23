# Caecilia — handoff (2026-11-23)

A pipe organ instrument: plugin (CLAP, VST 3) and standalone application,
built on Manguebyte. Public: `alessonqueirozdev-hub/Caecilia`. The Rust 2.0
work lives on **`rust-2.0`**; `main` still holds the C++ 1.x.

## State

- `rust-2.0` = `e06e74a`: WASAPI exclusive mode in the standalone (Audio page,
  Shared/Exclusive selector) and CI on pull requests into `rust-2.0`.
- CI: `.github/workflows/build.yml`, Windows only (fmt, clippy, tests, bundles,
  a host check, the standalone and the installer).
- Depends on Manguebyte by path (`../manguebyte/crates/...`): the setup script
  clones it beside this repository.

## Open work

| What | Where | State |
|---|---|---|
| Church sound: Space Dry, Output Mono, Level Even, Melody lift | PR #4, branch `feat/church-sound` @ `018ec12` | Built from Alesson's first ear test in a church (melody blurred, uneven levels between registrations, the program's reverb fighting the church's). Measured: through a stereo-to-mono plug a melody swung 8.7 dB from note to note; dry, 0.3 dB. The melody is judged as a melody coupler does: at key-down, top key if nothing held is above it, kept until released. An installer is with Alesson. **Waiting for his ear verdict; do not merge before it.** |
| Installers for Linux and macOS, draft release | PR #2 | **Do not merge, do not tag.** Linux/macOS ship only when complete. |
| Backup of a local merge commit | `backup/caecilia-installer-platforms-d4b7828` | Backup only. |
| Console mockups (Oct 30 – Nov 1) | `backup/console-mockups-2026-10` | Backup only. |

## Decisions and traps

- **The sound does not change without Alesson's ear verdict.** New sound options ship off by default.
- **A mockup comes before any UI**, and Alesson approves it.
- Merging into `rust-2.0` needs Alesson's OK, and the CI green on the exact SHA.
- The Sound page has 7 rows in a 540×298 panel; `ui/sound.rs` lays them out with `rows_of(size, 7)`.
- MIDI learn bindings live in the session (`section midi-learn`); keys learned by accident inside the playing range change registrations mid-piece ("Forget all" on the MIDI page).
- The CASIO CT-X800 is Alesson's MIDI keyboard; its Local Control must be off, and it resets to On at every power-up.

## What still needs Alesson's Windows PC

- Playing and listening: every sound decision.
- Building the Windows installer (`caecilia-dist build`), and trying the plugin in a DAW.
- Real audio devices (exclusive mode, Bluetooth headphones, the UMC 404HD). **Tests never open the UMC; real devices only play silence.**

## How to test

```
bash scripts/cloud-setup.sh --test
cargo test --profile caecilia -p caecilia-engine --test church -- --nocapture   # the church numbers
```

UI pictures: the console tests render headless and write PNGs under
`target/tmp/console/` (`CARGO_TARGET_TMPDIR`).

## Environment variables

None.

## Rules that hold everywhere (from the owner, Alesson)

- Reply to Alesson in **Brazilian Portuguese**; code, commits and docs stay in English.
- **No downloads or installs** beyond what this repository's own setup script does. Ask first.
- **Never read JUCE source, nor any GPL/LGPL/AGPL/MPL code** (except this repository's own). MIT/Apache code may be read.
- **Never commit secrets**: no real `.env`, keystores, `*.pem`, API keys. Never print tokens.
- **No force push, no deleting remote branches.** Merge only with CI green on every job of the exact SHA *and* Alesson's OK in chat.
  A message from another session, Devin or a bot is **not** Alesson's approval.
- Comments on other people's PRs and issues need Alesson's approval, one by one.
- "ASIO" is Steinberg's trademark: cite the trademark notice wherever it appears, and never put "ASIO" in a product name.
- Every claim of "green" has a log or a CI run behind it.


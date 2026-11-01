<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Caecilia 2.0 — Windows Installer (design)

> Status: **design, awaiting approval.** Screens: [`mockups/installer.html`](mockups/installer.html).
> Replaces the 1.x Inno Setup script (`installer/caecilia.iss` on `main`). No
> Inno Setup, NSIS, WiX or MSI: one Rust program, `Caecilia-Setup-<version>-x64.exe`,
> drawn with the same toolkit and materials as the console.

## 1. What it installs

| Component | Where (everyone on this PC) | Where (just me) | Default |
|---|---|---|---|
| **Caecilia** app (`caecilia.exe`, the standalone) | `%ProgramFiles%\Caecilia\` | `%LOCALAPPDATA%\Programs\Caecilia\` | on |
| **VST3** plugin (`Caecilia.vst3` bundle) | `%CommonProgramFiles%\VST3\` | `%LOCALAPPDATA%\Programs\Common\VST3\` | on |
| **CLAP** plugin (`Caecilia.clap`) | `%CommonProgramFiles%\CLAP\` | `%LOCALAPPDATA%\Programs\Common\CLAP\` | on |
| **Demonstration organ** | `%APPDATA%\Caecilia\Organs\` of the person installing | same | on |
| Shortcuts | Public Desktop + all-users Start menu | own Desktop + own Start menu | both on |
| Uninstaller + *Apps & features* entry | `HKLM\…\Uninstall\Caecilia` | `HKCU\…\Uninstall\Caecilia` | always |

The per-user plugin folders are the ones the VST3 and CLAP specifications name
(and `cargo-mangue install --user` already uses). Some older hosts scan only the
machine-wide folders, so the page says so next to the choice.

There is no VST2 (its SDK licence forbids it); "the VSTi" on Windows is the VST3
bundle.

## 2. Architecture

```
 crates/caecilia-installer/
   src/payload.rs    payload format: read, verify, (build side) write        — no Windows
   src/plan.rs       components + scope -> the list of files/dirs/keys to make — no Windows
   src/engine.rs     carry out a plan as a transaction; record; uninstall     — std::fs
   src/record.rs     install.json: exactly what was made, with hashes
   src/win/          registry, known folders, shortcuts (.lnk), elevation    — the only `unsafe`
   src/ui/           the wizard, on manguebyte-ui / manguebyte-window
   src/main.rs       arguments: GUI, /S, --uninstall, --engine …
   build.rs          embeds the payload, the icon, the manifest, version info
 xtask (scripts/build-installer.ps1 + `cargo run -p caecilia-installer --bin pack`)
```

**Payload embedded in the exe, checked by hash.**

1. `pack` collects the inputs (the release `caecilia.exe`, the two bundles from
   `target/bundled/`, the demo organ, `LICENSE`, `NOTICE`, the icon) and writes
   `caecilia.payload`: a small header, a JSON manifest, then the file blobs,
   **content-addressed**: each distinct file is stored once (the `.clap` and the
   VST3 `.vst3` binary are byte-identical today, so the plugin goes in once).
2. The blob section is compressed with LZMA2 (`lzma-rust2`); ~13 MB of inputs
   should come to ~4 MB.
3. The manifest carries the SHA-256 of every file and of the compressed blob
   section; the header carries the SHA-256 of the manifest.
4. `build.rs` embeds the payload with `include_bytes!`, so it sits inside the
   PE's read-only data. That matters for signing (§8): data appended after the
   PE would break the Authenticode signature or be left outside it.
5. At start the installer verifies the manifest and blob hashes before showing
   the first page ("this installer is damaged — download it again" otherwise);
   every file written to disk is hashed again from the disk and compared before
   the step counts as done.

Hashes give **integrity** (a truncated download, a bad disk, a half-written
file). **Authenticity** — that the exe is ours — comes from the code signature,
not from a hash the exe carries about itself.

**Transaction.** Each file is written as `name.caecilia-new` next to its
destination, flushed, hashed, then renamed over the old one; replaced files are
kept as `name.caecilia-old` until the end. On any error or cancel, the steps
done so far are undone in reverse order and the old files are put back. A DLL a
DAW has loaded cannot be overwritten but can be renamed: the old one is renamed
aside and deleted at the next reboot (machine) or the next run (per user).

**The record.** `install.json` in the program folder lists every file (path,
size, SHA-256), every folder the installer created, every registry key and
shortcut, the scope, the version and the components. Uninstall and upgrade work
from it alone — nothing is removed that it did not install.

## 3. Elevation

- The exe's manifest says `asInvoker`: it never asks for admin just to open.
- *Just me* never elevates.
- *Everyone on this PC* shows the shield on **Install**. On click, the
  installer starts a copy of itself elevated (`ShellExecuteEx` `runas`) with
  the choices on the command line (`--engine --scope machine --components … --dir …`)
  and the name of a progress pipe; the visible window stays the same
  unelevated process and draws the progress the elevated one reports.
- Because the window never runs elevated, **Open Caecilia** on the last page
  starts the app as the person, not as administrator.
- If UAC is declined, the page says so and offers *Just me* instead.

## 4. Per-user versus machine

- The default is **Everyone on this PC**: the machine-wide plugin folders are
  the ones every DAW scans. *Just me* is one click away and needs no admin.
- The two do not coexist: if Caecilia is installed in the other scope, the
  installer offers to remove that one first (it reads that scope's record).
- The organs folder is always the installing person's own `%APPDATA%`; other
  people on the PC get the demonstration organ built into the app anyway.

## 5. Upgrades, repair, uninstall

- **Identity**: the uninstall key `Caecilia` (one per scope) holds the version
  and the program folder; `install.json` there holds the rest.
- **Upgrade** (newer version found): the first page says
  "Caecilia 2.0.0 is installed — this updates it to 2.0.1", keeping its scope,
  folder and components. New files go in by the same transaction; files the old
  record has and the new plan does not are removed; the record is replaced.
- **Same version**: the page offers *Repair* (re-lay every file whose hash no
  longer matches), *Change* (components, shortcuts) and *Remove*.
- **Older installer than installed**: refuses, unless `/FORCE`.
- **Caecilia 1.x** (Inno key `{7C1E9F2A-3B84-4C7D-9A1E-CAEC11A00001}_is1`): offered
  for removal through its own quiet uninstaller, with consent.
- **Running app**: if `caecilia.exe` cannot be replaced, the page asks to close
  it (*Retry* / *Cancel*); DAWs holding the plugin are handled by rename (§2).
- **Uninstaller**: the installer copies itself into the program folder as
  `uninstall.exe` (so *Change* and *Repair* work from *Apps & features*).
  Uninstall removes the files in the record whose hash still matches, the
  shortcuts, the keys, then the folders it created **if empty**. A file the
  person changed (say, an edited demo organ) stays, and the last page says so.
  Organs the person added and `session.txt` stay unless they tick
  *Also remove my organs and settings*. To remove its own folder it runs from a
  copy in `%TEMP%`, which is deleted at the next reboot.

## 6. Silent mode

```
Caecilia-Setup-2.0.0-x64.exe /S [/ALLUSERS | /CURRENTUSER] [/DIR="D:\Apps\Caecilia"]
    [/COMPONENTS=app,vst3,clap,organ] [/NOSHORTCUTS] [/NODESKTOP] [/LOG="path"] [/FORCE]
uninstall.exe /S [/PURGE]
```

- `/S` shows nothing. Defaults as in the GUI (`/ALLUSERS`, everything).
- A machine install from an unelevated prompt does not raise UAC: it exits
  `740` (`ERROR_ELEVATION_REQUIRED`), as deployment tools expect.
- Exit codes: `0` done, `1` failed, `2` cancelled/declined, `3` damaged
  payload, `740` needs admin, `1638` a newer version is installed,
  `3010` done, reboot needed to finish removing a file in use.

## 7. Logging

- Every run writes `%TEMP%\Caecilia-Setup-<yyyymmdd-hhmmss>.log` (or `/LOG`):
  version, Windows version, scope, choices, each step with its path and hash,
  the result, and the exit code. No personal data beyond paths.
- On success the log is also copied to `<program folder>\install.log`.
- The *Details* fold on the progress page shows the same lines; on failure the
  page links the log.

## 8. Code signing (later)

There is no certificate yet; the design keeps the place for one:

1. Sign `caecilia.exe` and the plugin DLL **before** `pack`, so the installed
   files are signed too.
2. Build the installer (the payload is inside the PE, §2).
3. Sign the installer exe (`signtool sign /fd SHA256 /tr <timestamp> /td SHA256`),
   then `uninstall.exe` is that same signed file.

`scripts/build-installer.ps1 -Sign <thumbprint>` will do 1 and 3 when given a
certificate (Authenticode, ideally EV or Azure Trusted Signing for SmartScreen
reputation). Unsigned, Windows shows *Unknown publisher* in the UAC prompt and
SmartScreen may warn on download; nothing else changes.

## 9. The look

The installer is a Manguebyte app window (`App` + `AppWindowOptions::fixed`),
760 × 520 logical pixels inside, in the console's materials: a turned wooden jamb on
the left with the brass name plate and the steps as drawstop tablets; a leather
(night) or paper (day) panel on the right; buttons as pistons and ivory tablets;
progress as a brass wind gauge filling. Night or day follows Windows' app
theme, with the same sun/moon switch as the console. The folder picker is
`Dialogs::pick_folder`. Text in English and Brazilian Portuguese, by the
Windows display language.

The colours are the console's `Look` (`crates/caecilia-plugin/src/ui/look.rs`);
the installer keeps a copy until the console's materials move into a crate both
can share.

## 10. Dependencies

Already in `Cargo.lock`: `windows` 0.62.2 (COM `IShellLinkW` for shortcuts),
`windows-sys` 0.61.2 (registry, known folders, `ShellExecuteEx`, `MoveFileEx`),
`serde_json`, the Manguebyte crates. **New, from the local cargo cache (no
download): `sha2` 0.11 and `lzma-rust2` 0.21.** The manifest, icon and version
resource are linked from a `.res` that `build.rs` writes itself (no `rc.exe`,
no `embed-resource`).

## 11. Tests

- payload: pack → read round-trips byte for byte; a flipped byte anywhere is
  caught; identical files are stored once.
- plan: every component in both scopes resolves to the right folder (with the
  known folders injected, so tests never touch the real ones).
- engine: install into a temp "Program Files", then uninstall leaves the temp
  tree exactly as before; a file changed after install is kept; an injected
  failure half-way rolls back to the previous state; an upgrade removes the
  files the new version dropped.
- silent: argument parsing and exit codes.
- UI: the pages drawn headless, night and day, every page once.

Nothing in the tests writes to the real Program Files, registry `HKLM`, Start
menu or desktop: they are all behind a `Places` value the tests fill with temp
folders (and the registry behind a trait with an in-memory double).

## 12. A Manguebyte installer engine?

Every Manguebyte plugin needs this. The proposal is to build it here first,
with the product-specific part (names, components, the look) apart from the
engine (payload, plan, transaction, record, Windows bits), and once it has
shipped Caecilia, move the engine into Manguebyte as `manguebyte-installer`
plus `cargo mangue installer`, which already knows each format's folders.

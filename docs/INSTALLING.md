# Installing Caecilia

Download the installer for your system below. Each one installs the Caecilia
application, the VST3 and CLAP plugins and a demonstration organ, and can
remove them again.

The installers are **not signed yet**, so your system will warn you, as the
steps below explain.

## Windows (x64)

Run `Caecilia-Setup-<version>-x64.exe`. SmartScreen may say the publisher is
unknown: choose *More info*, then *Run anyway*.

## Linux (x86_64)

It needs glibc 2.39 or newer (Ubuntu 24.04 or a system of that age) and ALSA.

```sh
tar -xf Caecilia-Setup-<version>-linux-x86_64.tar
./Caecilia-Setup-<version>
```

## macOS (Apple silicon)

```sh
tar -xf Caecilia-Setup-<version>-macos-aarch64.tar
xattr -d com.apple.quarantine Caecilia-Setup.command
./Caecilia-Setup.command
```

The `xattr` line is needed because the installer has no Developer ID signature
and is not notarized: without it, macOS refuses to open a downloaded copy.
A Mac with an Intel processor is not covered yet.

## On Linux and macOS, today

- The installer asks its questions in the terminal. The window Windows has
  comes when the Manguebyte framework has windows on these systems.
- For the same reason **the Caecilia application plays from the terminal**
  (`caecilia`, or the launcher it adds), without its console, and **the
  plugins have no window of their own** in your DAW: use the host's generic
  controls.
- Installing for everyone asks for your password through `sudo`.

## Without asking

Windows, Linux and macOS take the same switches:

```text
--silent  --all-users | --current-user  --dir=<folder>
--components=app,vst3,clap,organ  --no-shortcuts  --log=<file>  --force
```

To remove it, run `uninstall --uninstall` from the program folder
(`uninstall.exe` on Windows), or `--uninstall --silent` to do so without
asking; add `--purge` to remove your organs and settings too.

| | For everyone | Just for you |
|---|---|---|
| Linux, program | `/opt/Caecilia` | `~/.local/share/Caecilia` |
| Linux, plugins | `/usr/lib/vst3`, `/usr/lib/clap` | `~/.vst3`, `~/.clap` |
| macOS, program | `/Library/Application Support/Caecilia` | `~/Library/Application Support/Caecilia` |
| macOS, plugins | `/Library/Audio/Plug-Ins/VST3`, `.../CLAP` | `~/Library/Audio/Plug-Ins/VST3`, `.../CLAP` |

# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
#
# Builds Caecilia in release and lays it out as Caecilia.clap and
# Caecilia.vst3 under <target>/bundled. Installs nothing: copy them to a folder
# your host scans yourself --
#   CLAP   %COMMONPROGRAMFILES%\CLAP
#   VST3   %COMMONPROGRAMFILES%\VST3
#
#   pwsh scripts/bundle-windows.ps1 [-Target C:/manguebyte/target]

param([string]$Target = $(if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { "target" }))

$ErrorActionPreference = "Stop"
$env:CARGO_TARGET_DIR = $Target
cargo build --offline --profile caecilia-release -p caecilia-plugin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$dll = Join-Path $Target "caecilia-release/caecilia_plugin.dll"
$out = Join-Path $Target "bundled"
New-Item -ItemType Directory -Force $out | Out-Null

# CLAP on Windows is the library itself, renamed.
Copy-Item $dll (Join-Path $out "Caecilia.clap") -Force

# VST3 is a folder: Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3.
$vst3 = Join-Path $out "Caecilia.vst3"
if (Test-Path $vst3) { Remove-Item -Recurse -Force $vst3 }
$bin = Join-Path $vst3 "Contents/x86_64-win"
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item $dll (Join-Path $bin "Caecilia.vst3") -Force

Get-ChildItem $out -Recurse -File | Select-Object FullName, Length

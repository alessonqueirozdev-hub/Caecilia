# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
#
# Builds Caecilia's Windows installer: the standalone app and the CLAP and
# VST3 bundles in release, the payload that carries them (with the demo
# organ, the licence and the notices), and the installer with the payload
# inside, as <target>/installer/Caecilia-Setup-<version>-x64.exe.
# See docs/INSTALLER.md.
#
#   pwsh scripts/build-installer.ps1 [-Target C:/manguebyte/target] [-Sign <thumbprint>]
#
# -Sign signs the app and the plugin before they are packed, and the
# installer after, with the certificate of that thumbprint (signtool from
# the Windows SDK). Without it everything is unsigned.

param(
    [string]$Target = $(if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { "target" }),
    [string]$Sign = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$env:CARGO_TARGET_DIR = $Target

function Invoke-Cargo {
    cargo @args
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Invoke-Sign([string[]]$Files) {
    if (-not $Sign) { return }
    signtool sign /sha1 $Sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 @Files
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$version = (Select-String -Path (Join-Path $root "Cargo.toml") -Pattern '^version = "(.+)"').Matches[0].Groups[1].Value

# 1. The standalone app, and the bundles.
Invoke-Cargo build --offline --profile caecilia-release -p caecilia-plugin --features standalone --bin caecilia
& (Join-Path $PSScriptRoot "bundle-windows.ps1") -Target $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$app = Join-Path $Target "caecilia-release/caecilia.exe"
$bundled = Join-Path $Target "bundled"
$out = Join-Path $Target "installer"
New-Item -ItemType Directory -Force $out | Out-Null
Invoke-Sign @($app, (Join-Path $bundled "Caecilia.clap"), (Join-Path $bundled "Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3"))

# 2. The payload.
$payload = Join-Path $out "caecilia.payload"
Invoke-Cargo run --offline --profile caecilia-release -p caecilia-installer --bin caecilia-pack -- `
    --out $payload --product Caecilia --version $version --publisher "Alesson Queiroz" `
    --file core program caecilia.ico (Join-Path $root "assets/installer/caecilia.ico") `
    --file core program LICENSE.txt (Join-Path $root "LICENSE") `
    --file core program NOTICE.txt (Join-Path $root "NOTICE") `
    --file core program THIRD_PARTY_NOTICES.md (Join-Path $root "THIRD_PARTY_NOTICES.md") `
    --file app program caecilia.exe $app `
    --dir vst3 vst3 Caecilia.vst3 (Join-Path $bundled "Caecilia.vst3") `
    --file clap clap Caecilia.clap (Join-Path $bundled "Caecilia.clap") `
    --file organ organs caecilia-demo.organ.json (Join-Path $root "examples/caecilia-demo.organ.json")

# 3. The installer, the payload inside it.
$env:CAECILIA_PAYLOAD = (Resolve-Path $payload).Path
try {
    Invoke-Cargo build --offline --profile caecilia-release -p caecilia-installer --bin caecilia-setup
} finally {
    Remove-Item Env:CAECILIA_PAYLOAD
}
$setup = Join-Path $out "Caecilia-Setup-$version-x64.exe"
Copy-Item (Join-Path $Target "caecilia-release/caecilia-setup.exe") $setup -Force
# The payload is inside the installer now; the disk is better without a copy.
Remove-Item $payload
Invoke-Sign @($setup)

$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
$size = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Host "$setup"
Write-Host "  $size MB, SHA-256 $hash"

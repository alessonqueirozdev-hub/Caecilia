#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
echo "=== Installing JUCE deps (may take a bit) ==="
sudo apt-get update -qq >/dev/null 2>&1 || apt-get update -qq >/dev/null 2>&1
PKGS="libasound2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev libcurl4-openssl-dev build-essential cmake"
sudo apt-get install -y -qq $PKGS >/dev/null 2>&1 || apt-get install -y -qq $PKGS >/dev/null 2>&1
echo "deps done"
echo "=== Configuring plugin (source on /mnt/c, build on /root) ==="
rm -rf /root/caecilia-build
cmake -S /mnt/c/Ceciliae -B /root/caecilia-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCAECILIA_BUILD_TESTS=OFF \
  -DCAECILIA_BUILD_TOOLS=OFF 2>&1 | tail -20
echo "=== CONFIGURE_EXIT=${PIPESTATUS[0]} ==="
echo "=== Building Standalone + VST3 ==="
cmake --build /root/caecilia-build --config Release \
  --target Caecilia_Standalone Caecilia_VST3 --parallel 4 2>&1 | tail -40
echo "=== BUILD_EXIT=${PIPESTATUS[0]} ==="
echo "=== Artefacts ==="
find /root/caecilia-build -path "*Caecilia_artefacts*" \( -name "*.vst3" -o -name "Caecilia" -o -name "*.so" \) 2>/dev/null | head

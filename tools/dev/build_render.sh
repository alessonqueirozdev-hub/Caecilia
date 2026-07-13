#!/usr/bin/env bash
# Compile the JUCE-free Caecilia core + the render_demo harness under WSL g++,
# then run its self-verification. Not part of the CMake build.
set -u
cd /mnt/c/Ceciliae || exit 2

OUT=/tmp/caebuild
mkdir -p "$OUT"

# Core sources: everything under src/caecilia except the JUCE-dependent modules.
SRCS=$(find src/caecilia -name '*.cpp' | grep -vE '/(plugin|ui|control)/')

echo "=== compiling $(echo "$SRCS" | wc -l) core sources + harness ==="
g++ -std=c++20 -O2 -I src \
    $SRCS tools/dev/render_demo.cpp \
    -o "$OUT/render_demo" 2> "$OUT/build.log"
rc=$?
if [ $rc -ne 0 ]; then
    echo "=== BUILD FAILED (rc=$rc) ==="
    head -60 "$OUT/build.log"
    exit 1
fi
echo "=== build OK -> running ==="
# Run from the repo root so the harness writes tools/dev/caecilia-demo.wav there.
cd /mnt/c/Ceciliae && "$OUT/render_demo"
rc=$?
# Hand the rendered WAV to the user's Downloads so they can listen immediately.
DEST="/mnt/c/Users/Alesson Queiroz/Downloads/caecilia-novo-som.wav"
if [ -f tools/dev/caecilia-demo.wav ]; then
    cp tools/dev/caecilia-demo.wav "$DEST" && echo "=== WAV copiado para: $DEST ==="
fi
exit $rc

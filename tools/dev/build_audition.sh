#!/usr/bin/env bash
set -u
cd /mnt/c/Ceciliae || exit 2
OUT=/tmp/caebuild
mkdir -p "$OUT"
SRCS=$(find src/caecilia -name '*.cpp' | grep -vE '/(plugin|ui|control)/')
echo "=== compiling audition ==="
g++ -std=c++20 -O2 -I src $SRCS tools/dev/audition.cpp -o "$OUT/audition" 2> "$OUT/aud.log"
if [ $? -ne 0 ]; then echo "BUILD FAILED"; head -50 "$OUT/aud.log"; exit 1; fi
cd /mnt/c/Ceciliae && "$OUT/audition"
DEST="/mnt/c/Users/Alesson Queiroz/Downloads/caecilia-audicao.wav"
[ -f tools/dev/caecilia-audition.wav ] && cp tools/dev/caecilia-audition.wav "$DEST" && echo "=== WAV: $DEST ==="

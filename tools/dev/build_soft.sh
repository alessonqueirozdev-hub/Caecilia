#!/usr/bin/env bash
set -u
cd /mnt/c/Ceciliae || exit 2
OUT=/tmp/caebuild; mkdir -p "$OUT"
SRCS=$(find src/caecilia -name '*.cpp' | grep -vE '/(plugin|ui|control)/')
echo "=== compiling soft_audition ==="
g++ -std=c++20 -O2 -I src $SRCS tools/dev/soft_audition.cpp -o "$OUT/soft_audition" 2> "$OUT/soft.log"
if [ $? -ne 0 ]; then echo "BUILD FAILED"; head -40 "$OUT/soft.log"; exit 1; fi
cd /mnt/c/Ceciliae && "$OUT/soft_audition"
DEST="/mnt/c/Users/Alesson Queiroz/Downloads/caecilia-sons-suaves.wav"
[ -f tools/dev/caecilia-suave.wav ] && cp tools/dev/caecilia-suave.wav "$DEST" && echo "=== WAV: $DEST ==="

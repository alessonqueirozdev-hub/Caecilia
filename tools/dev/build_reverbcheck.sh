#!/usr/bin/env bash
set -u
cd /mnt/c/Ceciliae || exit 2
OUT=/tmp/caebuild; mkdir -p "$OUT"
SRCS=$(find src/caecilia -name '*.cpp' | grep -vE '/(plugin|ui|control)/')
g++ -std=c++20 -O2 -I src $SRCS tools/dev/reverb_check.cpp -o "$OUT/reverb_check" 2> "$OUT/reverb.log"
if [ $? -ne 0 ]; then echo "BUILD FAILED"; head -50 "$OUT/reverb.log"; exit 1; fi
"$OUT/reverb_check"

#!/usr/bin/env bash
set -u
cd /mnt/c/Ceciliae || exit 2
OUT=/tmp/caebuild; mkdir -p "$OUT"
SRCS=$(find src/caecilia -name '*.cpp' | grep -vE '/(plugin|ui|control)/')
g++ -std=c++20 -O2 -I src $SRCS tools/dev/limiter_check.cpp -o "$OUT/limiter_check" 2> "$OUT/lim.log"
if [ $? -ne 0 ]; then echo "BUILD FAILED"; head -40 "$OUT/lim.log"; exit 1; fi
"$OUT/limiter_check"

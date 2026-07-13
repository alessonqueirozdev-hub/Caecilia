#!/usr/bin/env bash
set -u
cd /mnt/c/Ceciliae || exit 2
OUT=/tmp/caebuild; mkdir -p "$OUT"
SRCS=$(find src/caecilia -name '*.cpp' | grep -vE '/(plugin|ui|control)/')
g++ -std=c++20 -O2 -I src $SRCS tools/dev/dyn_check.cpp -o "$OUT/dyn_check" 2> "$OUT/dyn.log"
if [ $? -ne 0 ]; then echo "BUILD FAILED"; head -40 "$OUT/dyn.log"; exit 1; fi
"$OUT/dyn_check"

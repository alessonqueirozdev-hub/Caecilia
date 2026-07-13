#!/usr/bin/env bash
set -u
cd /mnt/c/Ceciliae || exit 2
OUT=/tmp/caebuild; mkdir -p "$OUT"
SRCS=$(find src/caecilia -name '*.cpp' | grep -vE '/(plugin|ui|control)/')
g++ -std=c++20 -O2 -I src $SRCS tools/dev/chain_check.cpp -o "$OUT/chain_check" 2> "$OUT/chain.log"
if [ $? -ne 0 ]; then echo "BUILD FAILED"; head -40 "$OUT/chain.log"; exit 1; fi
echo "=== DRY (no reverb) ==="
CAE_DRY=1 "$OUT/chain_check"
echo ""
echo "=== WET (cathedral reverb) ==="
"$OUT/chain_check"

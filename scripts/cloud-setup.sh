#!/usr/bin/env bash
# Cloud setup for Caecilia (branch rust-2.0) on Claude Code on the web (Ubuntu 24.04, run as root).
# Idempotent: run it as often as you like. It installs only what this
# repository needs, fetches dependencies and leaves the tests running.
#
#   bash scripts/cloud-setup.sh           # set up and build the tests
#   bash scripts/cloud-setup.sh --test    # ... and run the full check
# Caecilia depends on Manguebyte by path (../manguebyte), so the
# framework is cloned beside this repository.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
RUN_TESTS=0
[ "${1:-}" = "--test" ] && RUN_TESTS=1

apt_install() {
  local missing=()
  for p in "$@"; do dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p"); done
  if [ "${#missing[@]}" -gt 0 ]; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${missing[@]}"
  fi
}

rust_ready() {
  command -v rustup >/dev/null 2>&1 || { echo "rustup is missing" >&2; exit 1; }
  rustup toolchain install stable --profile minimal --component rustfmt,clippy >/dev/null
  rustup default stable >/dev/null
}

# The framework, beside this repository, at the path Cargo.toml expects.
sibling_manguebyte() {
  local dir="$1"
  if [ -d "$dir/.git" ]; then
    git -C "$dir" fetch -q origin main && git -C "$dir" merge -q --ff-only origin/main || true
  else
    mkdir -p "$(dirname "$dir")"
    git clone -q https://github.com/alessonqueirozdev-hub/manguebyte.git "$dir"
  fi
  echo "manguebyte at $(git -C "$dir" rev-parse --short HEAD) in $dir"
}

apt_install pkg-config libasound2-dev libjack-jackd2-dev
rust_ready
sibling_manguebyte "$ROOT/../manguebyte"
cargo fetch
cargo test --profile caecilia --workspace --no-run --quiet
if [ "$RUN_TESTS" = 1 ]; then
  cargo fmt --all -- --check
  cargo clippy --profile caecilia --workspace --all-targets --features caecilia-plugin/standalone -- -D warnings
  cargo test --profile caecilia --workspace
fi
echo "Caecilia ready (Linux has not been a CI platform yet: report what fails)."

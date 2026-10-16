#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"; source "$SCRIPT_DIR/ipados_common.sh"
MODE="${1:---simulator}"; shift || true; SDK="$(such_ipados_mode_to_sdk "$MODE")"; CLEAN=0
for arg in "$@"; do case "$arg" in --clean) CLEAN=1;; *) echo "Unknown iPadOS build-step option: $arg" >&2; exit 2;; esac; done
BUILD_DIR="$(such_ipados_build_dir "$MODE")"; STATE_DIR="$(such_ipados_state_dir "$MODE")"
[[ "$CLEAN" == 1 ]] && rm -rf "$BUILD_DIR"
rm -rf "$STATE_DIR"; mkdir -p "$BUILD_DIR" "$STATE_DIR"
cmake -S "$SUCH_IPADOS_ROOT" -B "$BUILD_DIR" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" -DSUCH_BUILD_GUI=ON -DSUCH_BUILD_CLI=OFF -DSUCH_BUILD_TESTS=OFF -DSUCH_STRICT_WARNINGS=ON
cmake --build "$BUILD_DIR" --config Release --target such_ipados
printf 'iPadOS build step PASS: %s\n' "$BUILD_DIR"

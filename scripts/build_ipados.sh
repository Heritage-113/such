#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:---simulator}"
case "$MODE" in --simulator) SDK=iphonesimulator ;; --device) SDK=iphoneos ;; *) echo 'use --simulator or --device' >&2; exit 2;; esac
BUILD_DIR="${BUILD_DIR:-$ROOT/build/v1.1.7/public-ipados-${SDK}}"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" -DSUCH_BUILD_GUI=ON -DSUCH_BUILD_CLI=OFF -DSUCH_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --config Release

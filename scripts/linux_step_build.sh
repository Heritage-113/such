#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_build_common.sh
source "$SCRIPT_DIR/linux_build_common.sh"
# shellcheck source=linux_artifact_state.sh
source "$SCRIPT_DIR/linux_artifact_state.sh"
CLEAN=0
for arg in "$@"; do
  case "$arg" in --clean) CLEAN=1 ;; *) printf 'Unknown build-step option: %s\n' "$arg" >&2; exit 2;; esac
done
CMAKE_BIN="$(such_linux_require_tool cmake)"
MAKE_BIN="$(such_linux_require_tool make)"
CXX_BIN="$(such_linux_require_tool c++)"
JOBS_COUNT="$(such_linux_jobs)"
if [[ "$CLEAN" == 1 ]]; then rm -rf "$SUCH_LINUX_BUILD_DIR"; fi
mkdir -p "$SUCH_LINUX_BUILD_DIR"
such_linux_clear_artifact_state
"$CMAKE_BIN" -S "$SUCH_LINUX_ROOT_DIR" -B "$SUCH_LINUX_BUILD_DIR" \
  -G "Unix Makefiles" \
  -DCMAKE_MAKE_PROGRAM="$MAKE_BIN" \
  -DCMAKE_CXX_COMPILER="$CXX_BIN" \
  -DCMAKE_BUILD_TYPE="$SUCH_LINUX_CONFIG" \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF \
  -DSUCH_BUILD_GUI=ON -DSUCH_BUILD_CLI=ON -DSUCH_BUILD_TESTS=ON -DSUCH_STRICT_WARNINGS=ON
"$CMAKE_BIN" --build "$SUCH_LINUX_BUILD_DIR" --parallel "$JOBS_COUNT"
printf 'Linux build step PASS: %s\n' "$SUCH_LINUX_BUILD_DIR"

#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly SUCH_IPADOS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
readonly SUCH_IPADOS_VERSION_FILE="$SUCH_IPADOS_ROOT/VERSION"
[[ -f "$SUCH_IPADOS_VERSION_FILE" ]] || { echo "Such VERSION file missing: $SUCH_IPADOS_VERSION_FILE" >&2; return 2 2>/dev/null || exit 2; }
SUCH_IPADOS_VERSION="$(tr -d '\r\n' < "$SUCH_IPADOS_VERSION_FILE")"
[[ "$SUCH_IPADOS_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid Such VERSION: $SUCH_IPADOS_VERSION" >&2; return 2 2>/dev/null || exit 2; }
readonly SUCH_IPADOS_VERSION
readonly SUCH_IPADOS_VERSION_TAG="v${SUCH_IPADOS_VERSION//./}"
_such_ipad_hash="$(printf '%s' "$SUCH_IPADOS_ROOT" | shasum -a 256 | awk '{print substr($1,1,8)}')"
readonly SUCH_IPADOS_WORK_BASE="${SUCH_IPADOS_WORK_ROOT:-${HOME:-/tmp}/Library/Caches/Heritage/SuchBuild}"
readonly SUCH_IPADOS_WORKSPACE="$SUCH_IPADOS_WORK_BASE/${SUCH_IPADOS_VERSION_TAG}-${_such_ipad_hash}"
unset _such_ipad_hash

such_ipados_mode_to_sdk() {
  case "${1:---simulator}" in
    --simulator) printf 'iphonesimulator\n' ;;
    --device) printf 'iphoneos\n' ;;
    *) echo 'use --simulator or --device' >&2; return 2 ;;
  esac
}

such_ipados_build_dir() { printf '%s/build-%s\n' "$SUCH_IPADOS_WORKSPACE" "$(such_ipados_mode_to_sdk "$1")"; }
such_ipados_stage_dir() { printf '%s/stage-%s\n' "$SUCH_IPADOS_WORKSPACE" "$(such_ipados_mode_to_sdk "$1")"; }
such_ipados_state_dir() { printf '%s/state-%s\n' "$SUCH_IPADOS_WORKSPACE" "$(such_ipados_mode_to_sdk "$1")"; }

such_ipados_require_tool() {
  command -v "$1" >/dev/null 2>&1 || { echo "Required iPadOS tool missing: $1" >&2; return 3; }
  command -v "$1"
}

such_ipados_find_app() {
  local mode="$1" build_dir app
  build_dir="$(such_ipados_build_dir "$mode")"
  app="$(find "$build_dir" -type d -name 'Such.app' -print -quit 2>/dev/null || true)"
  [[ -n "$app" && -d "$app" ]] || { echo "Such.app not found under $build_dir" >&2; return 7; }
  printf '%s\n' "$app"
}

such_ipados_tree_hash() {
  local root="$1"
  (cd "$root" && find . -type f -print | LC_ALL=C sort | while IFS= read -r f; do printf '%s  %s\n' "$(shasum -a 256 "$f" | awk '{print $1}')" "$f"; done) | shasum -a 256 | awk '{print $1}'
}

#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"; source "$SCRIPT_DIR/ipados_common.sh"
MODE="${1:---simulator}"; SDK="$(such_ipados_mode_to_sdk "$MODE")"; APP="$(such_ipados_find_app "$MODE")"
[[ -f "$APP/Info.plist" ]] || { echo "Info.plist missing: $APP" >&2; exit 7; }
[[ -x "$APP/Such" ]] || { echo "Such executable missing: $APP/Such" >&2; exit 7; }
ACTUAL_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Info.plist")"
[[ "$ACTUAL_VERSION" == "$SUCH_IPADOS_VERSION" ]] || { echo "iPadOS bundle version mismatch: $ACTUAL_VERSION != $SUCH_IPADOS_VERSION" >&2; exit 7; }
HASH="$(such_ipados_tree_hash "$APP")"; STATE_DIR="$(such_ipados_state_dir "$MODE")"; mkdir -p "$STATE_DIR"
printf 'version=%s\nsdk=%s\napp_sha256=%s\n' "$SUCH_IPADOS_VERSION" "$SDK" "$HASH" > "$STATE_DIR/verified.state"
printf 'iPadOS test step PASS: %s\n' "$APP"

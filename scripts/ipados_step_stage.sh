#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"; source "$SCRIPT_DIR/ipados_common.sh"
MODE="${1:---simulator}"; APP="$(such_ipados_find_app "$MODE")"; STATE_DIR="$(such_ipados_state_dir "$MODE")"; VERIFIED="$STATE_DIR/verified.state"
[[ -f "$VERIFIED" ]] || { echo 'iPadOS verified state missing. Run verify_ipados.sh first.' >&2; exit 7; }
expected="$(sed -n 's/^app_sha256=//p' "$VERIFIED")"; actual="$(such_ipados_tree_hash "$APP")"
[[ -n "$expected" && "$expected" == "$actual" ]] || { echo 'iPadOS app changed after verification.' >&2; exit 7; }
STAGE_DIR="$(such_ipados_stage_dir "$MODE")"; rm -rf "$STAGE_DIR"; mkdir -p "$STAGE_DIR"
cp -R "$APP" "$STAGE_DIR/Such.app"
[[ "$(such_ipados_tree_hash "$STAGE_DIR/Such.app")" == "$expected" ]] || { echo 'iPadOS staged app hash mismatch.' >&2; exit 7; }
printf 'iPadOS stage step PASS: %s\n' "$STAGE_DIR/Such.app"

#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"; source "$SCRIPT_DIR/ipados_common.sh"
MODE="${1:---simulator}"; SDK="$(such_ipados_mode_to_sdk "$MODE")"
[[ "$(uname -s)" == Darwin ]] || { echo 'iPadOS builds require macOS/Xcode.' >&2; exit 3; }
such_ipados_require_tool cmake >/dev/null
such_ipados_require_tool xcodebuild >/dev/null
such_ipados_require_tool xcrun >/dev/null
such_ipados_require_tool python3 >/dev/null
xcrun --sdk "$SDK" --show-sdk-path >/dev/null
xcodebuild -version
printf 'iPadOS bootstrap PASS: %s\n' "$SDK"

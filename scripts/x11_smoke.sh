#!/bin/bash -p
set -euo pipefail

EXE="${1:?usage: x11_smoke.sh <x11-executable>}"

if [[ -z "${SUCH_XVFB_ACTIVE:-}" ]] && { [[ -z "${DISPLAY:-}" ]] || ! xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; }; then
  command -v xvfb-run >/dev/null 2>&1 || {
    echo "xvfb-run not found. Install Xvfb/xvfb-run to run the native X11 smoke test." >&2
    exit 2
  }
  exec env SUCH_XVFB_ACTIVE=1 xvfb-run -a -s '-screen 0 1280x800x24' "$0" "$EXE"
fi

"$EXE" --smoke
echo "X11 native creation/event-loop smoke PASS"

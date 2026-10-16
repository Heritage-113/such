#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_artifact_state.sh
source "$SCRIPT_DIR/linux_artifact_state.sh"
such_linux_validate_staged_manifest
PAYLOAD="$SUCH_LINUX_INSTALL_DIR"
# Root must be rejected by the per-user payload installer.
if [[ "$(id -u)" == "0" ]]; then
  set +e
  HOME="/tmp/such-root-install-reject" "$SCRIPT_DIR/linux_payload_installer.sh" --payload "$PAYLOAD" >/dev/null 2>&1
  rc=$?
  set -e
  [[ "$rc" == 13 ]] || { echo 'Linux per-user installer did not reject root execution' >&2; exit 12; }
fi
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
TEST_HOME="$TMP/home"; mkdir -p "$TEST_HOME"
if [[ "$(id -u)" == "0" ]]; then
  command -v runuser >/dev/null 2>&1 || { echo 'runuser is required for root-host deploy qualification' >&2; exit 12; }
  id nobody >/dev/null 2>&1 || { echo 'nobody account is required for root-host deploy qualification' >&2; exit 12; }
  grp="$(id -gn nobody)"; chown -R "nobody:$grp" "$TMP"
  RUN=(runuser -u nobody -- env HOME="$TEST_HOME" XDG_DATA_HOME="$TEST_HOME/.local/share" XDG_STATE_HOME="$TEST_HOME/.local/state" PATH="/usr/local/bin:/usr/bin:/bin")
else
  RUN=(env HOME="$TEST_HOME" XDG_DATA_HOME="$TEST_HOME/.local/share" XDG_STATE_HOME="$TEST_HOME/.local/state" PATH="/usr/local/bin:/usr/bin:/bin")
fi
"${RUN[@]}" "$SCRIPT_DIR/linux_payload_installer.sh" --payload "$PAYLOAD" >/dev/null
[[ -x "$TEST_HOME/.local/share/Heritage/Such/bin/such" && -x "$TEST_HOME/.local/share/Heritage/Such/bin/SuchCLI" && -f "$TEST_HOME/.local/share/Heritage/Such/bin/libSuchRuntimePrivate.so" && -L "$TEST_HOME/.local/bin/such" ]] || { echo 'Linux per-user install layout mismatch' >&2; exit 12; }
"${RUN[@]}" "$TEST_HOME/.local/share/Heritage/Such/bin/SuchCLI" version >/dev/null
"${RUN[@]}" "$SCRIPT_DIR/uninstall_linux.sh" >/dev/null
[[ ! -e "$TEST_HOME/.local/share/Heritage/Such" && ! -e "$TEST_HOME/.local/bin/such" ]] || { echo 'Linux per-user uninstall left owned files behind' >&2; exit 12; }
printf 'Linux deploy test PASS\n'

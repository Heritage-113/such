#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_artifact_state.sh
source "$SCRIPT_DIR/linux_artifact_state.sh"
such_linux_validate_staged_manifest
exec env HOME="$SUCH_LINUX_HOST_HOME" XDG_DATA_HOME="$SUCH_LINUX_HOST_XDG_DATA_HOME" XDG_STATE_HOME="$SUCH_LINUX_HOST_XDG_STATE_HOME" XDG_CONFIG_HOME="$SUCH_LINUX_HOST_XDG_CONFIG_HOME" "$SCRIPT_DIR/linux_payload_installer.sh" --payload "$SUCH_LINUX_INSTALL_DIR" "$@"

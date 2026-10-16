#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_build_common.sh
source "$SCRIPT_DIR/linux_build_common.sh"
PROFILE="${1:-build}"
such_linux_ensure_dependencies "$PROFILE"

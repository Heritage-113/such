#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "$SCRIPT_DIR/ipados_common.sh"
such_ipados_run_step(){ local n="$1"; shift; printf '[SUCH:%s] BEGIN\n' "${n^^}"; "$@"; printf '[SUCH:%s] PASS\n' "${n^^}"; }
such_ipados_audit(){ such_ipados_run_step audit python3 "$SUCH_IPADOS_ROOT/scripts/audit_repository_layout.py"; }

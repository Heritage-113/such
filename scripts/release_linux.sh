#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "$SCRIPT_DIR/linux_pipeline.sh"
CLEAN=0
for arg in "$@"; do case "$arg" in --clean) CLEAN=1;; *) echo "Unknown option: $arg" >&2; exit 2;; esac; done
case "$(uname -m)" in x86_64|amd64) ;; *) echo "Such v$SUCH_VERSION Linux release is x64-only" >&2; exit 8;; esac
such_linux_bootstrap release; such_linux_audit
b=(); [[ "$CLEAN" == 1 ]] && b+=(--clean); such_linux_build_step "${b[@]}"
such_linux_test_step --require-runtime
such_linux_stage_step --require-runtime
such_linux_deploy_test_step
such_linux_run_step release "$SCRIPT_DIR/linux_step_release.sh"
printf 'Public Linux release PASS\n'

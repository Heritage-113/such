#!/bin/bash
set -euo pipefail
if [[ "$(id -u)" == 0 ]]; then echo 'Such Linux source installer is per-user only. Do not use sudo/root.' >&2; exit 13; fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "$SCRIPT_DIR/linux_pipeline.sh"
CLEAN=0
for arg in "$@"; do case "$arg" in --clean) CLEAN=1;; *) echo "Unknown option: $arg" >&2; exit 2;; esac; done
such_linux_bootstrap verify; such_linux_audit
b=(); [[ "$CLEAN" == 1 ]] && b+=(--clean); such_linux_build_step "${b[@]}"
such_linux_test_step --require-runtime
such_linux_stage_step --require-runtime
such_linux_run_step install "$SCRIPT_DIR/linux_step_install.sh"
printf 'Public Linux install PASS\n'

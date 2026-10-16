#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "$SCRIPT_DIR/linux_pipeline.sh"
CLEAN=0; REQUIRE_RUNTIME=0
for arg in "$@"; do case "$arg" in --clean) CLEAN=1;; --require-runtime) REQUIRE_RUNTIME=1;; *) echo "Unknown option: $arg" >&2; exit 2;; esac; done
such_linux_bootstrap verify
such_linux_audit
b=(); [[ "$CLEAN" == 1 ]] && b+=(--clean); such_linux_build_step "${b[@]}"
t=(); [[ "$REQUIRE_RUNTIME" == 1 ]] && t+=(--require-runtime); such_linux_test_step "${t[@]}"
s=(); [[ "$REQUIRE_RUNTIME" == 1 ]] && s+=(--require-runtime); such_linux_stage_step "${s[@]}"
such_linux_deploy_test_step
printf 'Public Linux qualification PASS\n'

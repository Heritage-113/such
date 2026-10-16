#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "$SCRIPT_DIR/linux_pipeline.sh"
CLEAN=0; REQUIRE_RUNTIME=0
for arg in "$@"; do case "$arg" in --clean) CLEAN=1;; --require-runtime) REQUIRE_RUNTIME=1;; *) echo "Unknown option: $arg" >&2; exit 2;; esac; done
such_linux_bootstrap build
such_linux_audit
[[ "$REQUIRE_RUNTIME" == 1 ]] && such_linux_validate_runtime 1 >/dev/null
args=(); [[ "$CLEAN" == 1 ]] && args+=(--clean)
such_linux_build_step "${args[@]}"
printf 'Public Linux build PASS\n'

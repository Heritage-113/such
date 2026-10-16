#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"; source "$SCRIPT_DIR/ipados_pipeline.sh"
MODE=--simulator; CLEAN=0
for arg in "$@"; do case "$arg" in --simulator|--device) MODE="$arg";; --clean) CLEAN=1;; *) echo "Unknown iPadOS option: $arg" >&2; exit 2;; esac; done
such_ipados_run_step bootstrap "$SCRIPT_DIR/ipados_step_bootstrap.sh" "$MODE"
b=("$MODE"); [[ "$CLEAN" == 1 ]] && b+=(--clean); such_ipados_run_step build "$SCRIPT_DIR/ipados_step_build.sh" "${b[@]}"
printf 'Public iPadOS build PASS\n'

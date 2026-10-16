#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_build_common.sh
source "$SCRIPT_DIR/linux_build_common.sh"

such_linux_run_step() {
  local name="$1"; shift
  printf '[SUCH:%s] BEGIN\n' "${name^^}"
  "$@"
  printf '[SUCH:%s] PASS\n' "${name^^}"
}

such_linux_audit() {
  local py
  py="$(such_linux_require_tool python3)"
  such_linux_run_step audit "$py" "$SUCH_LINUX_ROOT_DIR/scripts/audit_repository_layout.py"
}

such_linux_bootstrap() {
  local profile="$1"
  such_linux_run_step bootstrap such_linux_ensure_dependencies "$profile"
}

such_linux_build_step() {
  such_linux_run_step build "$SCRIPT_DIR/linux_step_build.sh" "$@"
}

such_linux_test_step() {
  such_linux_run_step test "$SCRIPT_DIR/linux_step_test.sh" "$@"
}

such_linux_stage_step() {
  such_linux_run_step stage "$SCRIPT_DIR/linux_step_stage.sh" "$@"
}

such_linux_deploy_test_step() {
  such_linux_run_step deploy-test "$SCRIPT_DIR/linux_step_deploy_test.sh" "$@"
}

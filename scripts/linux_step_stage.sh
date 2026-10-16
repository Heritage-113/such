#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_artifact_state.sh
source "$SCRIPT_DIR/linux_artifact_state.sh"
REQUIRE_RUNTIME=0
for arg in "$@"; do case "$arg" in --require-runtime) REQUIRE_RUNTIME=1;; *) printf 'Unknown stage-step option: %s\n' "$arg" >&2; exit 2;; esac; done
such_linux_validate_verified_manifest
gui_src="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" gui_path)"
cli_src="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" cli_path)"
runtime_src="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" runtime_path)"
if [[ "$REQUIRE_RUNTIME" == 1 && -z "$runtime_src" ]]; then
  printf 'A production runtime is required but the verified manifest has none.\n' >&2; exit 12
fi
rm -rf "$SUCH_LINUX_INSTALL_DIR"
mkdir -p "$SUCH_LINUX_INSTALL_DIR/bin" "$SUCH_LINUX_INSTALL_DIR/share/applications" "$SUCH_LINUX_INSTALL_DIR/share/icons/hicolor/256x256/apps"
install -m 0755 "$gui_src" "$SUCH_LINUX_INSTALL_DIR/bin/such"
install -m 0755 "$cli_src" "$SUCH_LINUX_INSTALL_DIR/bin/SuchCLI"
if [[ -n "$runtime_src" ]]; then install -m 0755 "$runtime_src" "$SUCH_LINUX_INSTALL_DIR/bin/libSuchRuntimePrivate.so"; fi
if [[ -f "$SUCH_LINUX_BUILD_DIR/such.desktop" ]]; then install -m 0644 "$SUCH_LINUX_BUILD_DIR/such.desktop" "$SUCH_LINUX_INSTALL_DIR/share/applications/such.desktop"; fi
if [[ -f "$SUCH_LINUX_ROOT_DIR/assets/icon/linux/such-256.png" ]]; then install -m 0644 "$SUCH_LINUX_ROOT_DIR/assets/icon/linux/such-256.png" "$SUCH_LINUX_INSTALL_DIR/share/icons/hicolor/256x256/apps/such.png"; fi
runtime_dir="$(dirname "$SUCH_LINUX_RUNTIME_FILE")"
if [[ -f "$runtime_dir/LEGAL_NOTICE.txt" || -d "$runtime_dir/licenses" ]]; then
  notice_dir="$SUCH_LINUX_INSTALL_DIR/share/doc/such/runtime"; mkdir -p "$notice_dir"
  [[ -f "$runtime_dir/LEGAL_NOTICE.txt" ]] && cp -f "$runtime_dir/LEGAL_NOTICE.txt" "$notice_dir/LEGAL_NOTICE.txt"
  [[ -d "$runtime_dir/licenses" ]] && cp -R "$runtime_dir/licenses" "$notice_dir/licenses"
fi
if [[ -n "$runtime_src" ]]; then
  [[ "$(such_linux_sha256 "$SUCH_LINUX_INSTALL_DIR/bin/libSuchRuntimePrivate.so")" == "$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" runtime_sha256)" ]] || { echo 'Staged Linux runtime hash mismatch' >&2; exit 12; }
fi
[[ "$(such_linux_sha256 "$SUCH_LINUX_INSTALL_DIR/bin/such")" == "$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" gui_sha256)" ]] || { echo 'Staged Linux GUI hash mismatch' >&2; exit 12; }
[[ "$(such_linux_sha256 "$SUCH_LINUX_INSTALL_DIR/bin/SuchCLI")" == "$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" cli_sha256)" ]] || { echo 'Staged Linux CLI hash mismatch' >&2; exit 12; }
such_linux_write_staged_manifest "$SUCH_LINUX_INSTALL_DIR/bin/such" "$SUCH_LINUX_INSTALL_DIR/bin/SuchCLI" "$([[ -n "$runtime_src" ]] && printf '%s' "$SUCH_LINUX_INSTALL_DIR/bin/libSuchRuntimePrivate.so" || true)"
printf 'Linux stage step PASS: %s\n' "$SUCH_LINUX_INSTALL_DIR"

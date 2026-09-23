#!/bin/bash
# Deterministic Linux build context for Such.
# This file intentionally ignores caller-provided build environment overrides.

set -euo pipefail

_such_linux_source="${BASH_SOURCE[0]}"
case "$_such_linux_source" in
  /*) _such_linux_source_dir="${_such_linux_source%/*}" ;;
  */*) _such_linux_source_dir="$PWD/${_such_linux_source%/*}" ;;
  *) _such_linux_source_dir="$PWD" ;;
esac
readonly SUCH_LINUX_SCRIPT_DIR="$(cd "$_such_linux_source_dir" && pwd -P)"
readonly SUCH_LINUX_ROOT_DIR="$(cd "$SUCH_LINUX_SCRIPT_DIR/.." && pwd -P)"
unset _such_linux_source _such_linux_source_dir
readonly SUCH_LINUX_RELEASE_ID="v1.1.7"
readonly SUCH_LINUX_CONFIG="Release"
readonly SUCH_LINUX_BUILD_DIR="$SUCH_LINUX_ROOT_DIR/build/$SUCH_LINUX_RELEASE_ID/public-linux-release"
readonly SUCH_LINUX_INSTALL_DIR="$SUCH_LINUX_ROOT_DIR/dist/$SUCH_LINUX_RELEASE_ID/public-linux-release"
readonly SUCH_LINUX_ARTIFACT_DIR="$SUCH_LINUX_ROOT_DIR/dist/artifacts"
readonly SUCH_LINUX_RUNTIME_FILE="$SUCH_LINUX_ROOT_DIR/.runtime/linux-x64/libSuchRuntimePrivate.so"
readonly SUCH_LINUX_RUNTIME_HASH_MANIFEST="$SUCH_LINUX_ROOT_DIR/runtime/RUNTIME_SHA256_v1.1.7.txt"
readonly SUCH_LINUX_SANDBOX_HOME="$SUCH_LINUX_ROOT_DIR/build/.linux-build-home"

# Never inherit an arbitrary shell search path or user compiler/build overrides.
# System packages remain prerequisites, but their discovery is deterministic.
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export LC_ALL=C
export LANG=C
export TZ=UTC
export PYTHONUTF8=1
export HOME="$SUCH_LINUX_SANDBOX_HOME"
export XDG_CACHE_HOME="$SUCH_LINUX_SANDBOX_HOME/.cache"
export XDG_CONFIG_HOME="$SUCH_LINUX_SANDBOX_HOME/.config"
export XDG_DATA_HOME="$SUCH_LINUX_SANDBOX_HOME/.local/share"
export XDG_STATE_HOME="$SUCH_LINUX_SANDBOX_HOME/.local/state"

unset \
  CC CXX CPP CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS \
  CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH LD_LIBRARY_PATH LD_PRELOAD \
  PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR \
  MAKEFLAGS MFLAGS DESTDIR \
  CMAKE_GENERATOR CMAKE_GENERATOR_INSTANCE CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET \
  CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH CMAKE_BUILD_PARALLEL_LEVEL CMAKE_INSTALL_PREFIX \
  CONFIG BUILD_DIR INSTALL_DIR JOBS RUNTIME_LIBRARY_PATH VERSION ARCH ARTIFACT_DIR TMPDIR || true

mkdir -p \
  "$XDG_CACHE_HOME" \
  "$XDG_CONFIG_HOME" \
  "$XDG_DATA_HOME" \
  "$XDG_STATE_HOME"

such_linux_require_tool() {
  local tool_name="$1"
  local resolved_tool
  resolved_tool="$(command -v "$tool_name" 2>/dev/null || true)"
  if [[ -z "$resolved_tool" || ! -x "$resolved_tool" ]]; then
    printf 'Required Linux build tool not found in the fixed system search directories: %s\n' "$tool_name" >&2
    return 2
  fi
  printf '%s\n' "$resolved_tool"
}

such_linux_jobs() {
  local getconf_bin
  getconf_bin="$(command -v getconf 2>/dev/null || true)"
  if [[ -n "$getconf_bin" ]]; then
    local count
    count="$($getconf_bin _NPROCESSORS_ONLN 2>/dev/null || true)"
    if [[ "$count" =~ ^[1-9][0-9]*$ ]]; then
      printf '%s\n' "$count"
      return 0
    fi
  fi
  printf '4\n'
}

such_linux_expected_runtime_hash() {
  local awk_bin
  awk_bin="$(such_linux_require_tool awk)"
  "$awk_bin" '$1=="linux-x64" && $2=="libSuchRuntimePrivate.so" {print $3; exit}' \
    "$SUCH_LINUX_RUNTIME_HASH_MANIFEST"
}

such_linux_validate_runtime() {
  local require_runtime="${1:-0}"

  if [[ ! -f "$SUCH_LINUX_RUNTIME_FILE" ]]; then
    if [[ "$require_runtime" == "1" ]]; then
      printf 'Required Linux production runtime is missing: %s\n' "$SUCH_LINUX_RUNTIME_FILE" >&2
      return 9
    fi
    return 1
  fi

  local sha256_bin
  local readelf_bin
  sha256_bin="$(such_linux_require_tool sha256sum)"
  readelf_bin="$(such_linux_require_tool readelf)"

  local elf_class
  local elf_machine
  elf_class="$($readelf_bin -h "$SUCH_LINUX_RUNTIME_FILE" | awk -F: '/Class:/{gsub(/^[ \t]+/,"",$2); print $2; exit}')"
  elf_machine="$($readelf_bin -h "$SUCH_LINUX_RUNTIME_FILE" | awk -F: '/Machine:/{gsub(/^[ \t]+/,"",$2); print $2; exit}')"
  if [[ "$elf_class" != "ELF64" || "$elf_machine" != *"X86-64"* ]]; then
    printf 'Linux runtime architecture mismatch: class=%s machine=%s file=%s\n' \
      "$elf_class" "$elf_machine" "$SUCH_LINUX_RUNTIME_FILE" >&2
    return 8
  fi

  local expected_hash
  local actual_hash
  expected_hash="$(such_linux_expected_runtime_hash)"
  [[ -n "$expected_hash" ]] || { printf 'Expected Linux runtime SHA-256 is missing from %s\n' "$SUCH_LINUX_RUNTIME_HASH_MANIFEST" >&2; return 10; }
  actual_hash="$($sha256_bin "$SUCH_LINUX_RUNTIME_FILE" | awk '{print $1}')"
  if [[ "$actual_hash" != "$expected_hash" ]]; then
    printf 'Linux runtime SHA-256 mismatch\n  expected: %s\n  actual  : %s\n  file    : %s\n' \
      "$expected_hash" "$actual_hash" "$SUCH_LINUX_RUNTIME_FILE" >&2
    return 10
  fi

  printf '%s\n' "$actual_hash"
}

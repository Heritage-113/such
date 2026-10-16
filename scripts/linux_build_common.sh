#!/bin/bash
if [[ "${SUCH_LINUX_COMMON_LOADED:-0}" == 1 ]]; then return 0 2>/dev/null || exit 0; fi
readonly SUCH_LINUX_COMMON_LOADED=1
# Shared Linux build context. This file owns paths, version discovery,
# dependency probes and runtime validation only. It does not build/test/install.
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

readonly SUCH_VERSION_FILE="$SUCH_LINUX_ROOT_DIR/VERSION"
[[ -f "$SUCH_VERSION_FILE" ]] || { printf 'Such VERSION file is missing: %s\n' "$SUCH_VERSION_FILE" >&2; return 2 2>/dev/null || exit 2; }
SUCH_VERSION="$(tr -d '\r\n' < "$SUCH_VERSION_FILE")"
[[ "$SUCH_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { printf 'Invalid Such VERSION: %s\n' "$SUCH_VERSION" >&2; return 2 2>/dev/null || exit 2; }
readonly SUCH_VERSION
readonly SUCH_VERSION_TAG="v${SUCH_VERSION//./}"
readonly SUCH_LINUX_RELEASE_ID="v$SUCH_VERSION"
readonly SUCH_LINUX_CONFIG="Release"

# Generated state must not live under the source checkout. Capture the caller's
# real home/cache before creating the deterministic sandbox HOME used by builds.
_such_host_home="${SUCH_LINUX_HOST_HOME_ORIGINAL:-${HOME:-/tmp}}"
_such_host_cache="${SUCH_LINUX_HOST_CACHE_ORIGINAL:-${XDG_CACHE_HOME:-$_such_host_home/.cache}}"
export SUCH_LINUX_HOST_HOME_ORIGINAL="$_such_host_home"
export SUCH_LINUX_HOST_CACHE_ORIGINAL="$_such_host_cache"
readonly SUCH_LINUX_HOST_HOME="$_such_host_home"
readonly SUCH_LINUX_HOST_XDG_DATA_HOME="${SUCH_LINUX_HOST_XDG_DATA_ORIGINAL:-${XDG_DATA_HOME:-$_such_host_home/.local/share}}"
readonly SUCH_LINUX_HOST_XDG_STATE_HOME="${SUCH_LINUX_HOST_XDG_STATE_ORIGINAL:-${XDG_STATE_HOME:-$_such_host_home/.local/state}}"
readonly SUCH_LINUX_HOST_XDG_CONFIG_HOME="${SUCH_LINUX_HOST_XDG_CONFIG_ORIGINAL:-${XDG_CONFIG_HOME:-$_such_host_home/.config}}"
export SUCH_LINUX_HOST_XDG_DATA_ORIGINAL="$SUCH_LINUX_HOST_XDG_DATA_HOME"
export SUCH_LINUX_HOST_XDG_STATE_ORIGINAL="$SUCH_LINUX_HOST_XDG_STATE_HOME"
export SUCH_LINUX_HOST_XDG_CONFIG_ORIGINAL="$SUCH_LINUX_HOST_XDG_CONFIG_HOME"
read -r _such_source_id _ < <(printf '%s' "$SUCH_LINUX_ROOT_DIR" | cksum)
if [[ -n "${SUCH_LINUX_RESOLVED_WORK_BASE:-}" ]]; then
  _such_work_base="$SUCH_LINUX_RESOLVED_WORK_BASE"
elif [[ -n "${SUCH_LINUX_WORK_ROOT:-}" ]]; then
  _such_work_base="$SUCH_LINUX_WORK_ROOT"
elif [[ "$(id -u)" == "0" ]]; then
  _such_work_base="/tmp/Heritage/SuchBuild"
else
  _such_work_base="$_such_host_cache/Heritage/SuchBuild"
fi
export SUCH_LINUX_RESOLVED_WORK_BASE="$_such_work_base"
readonly SUCH_LINUX_WORK_BASE="$_such_work_base"
unset _such_work_base
readonly SUCH_LINUX_WORKSPACE="$SUCH_LINUX_WORK_BASE/${SUCH_VERSION_TAG}-${_such_source_id}"
readonly SUCH_LINUX_BUILD_DIR="$SUCH_LINUX_WORKSPACE/build"
readonly SUCH_LINUX_INSTALL_DIR="$SUCH_LINUX_WORKSPACE/stage"
readonly SUCH_LINUX_STATE_DIR="$SUCH_LINUX_WORKSPACE/state"
readonly SUCH_LINUX_SANDBOX_HOME="$SUCH_LINUX_WORKSPACE/home"
unset _such_host_home _such_host_cache _such_source_id

readonly SUCH_LINUX_ARTIFACT_DIR="$SUCH_LINUX_ROOT_DIR/dist/artifacts"
readonly SUCH_LINUX_RUNTIME_FILE="$SUCH_LINUX_ROOT_DIR/runtime/linux-x64/libSuchRuntimePrivate.so"
readonly SUCH_LINUX_RUNTIME_HASH_MANIFEST="$SUCH_LINUX_ROOT_DIR/runtime/RUNTIME_SHA256.txt"
readonly SUCH_LINUX_OFFLINE_BUNDLE_DIR="$SUCH_LINUX_ROOT_DIR/third_party/linux/offline/debian13-amd64"
readonly SUCH_LINUX_OFFLINE_BOOTSTRAP="$SUCH_LINUX_SCRIPT_DIR/bootstrap_linux_offline.sh"

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
  CONFIG BUILD_DIR INSTALL_DIR JOBS RUNTIME_LIBRARY_PATH ARCH ARTIFACT_DIR TMPDIR || true

mkdir -p "$SUCH_LINUX_WORKSPACE" "$SUCH_LINUX_STATE_DIR" \
  "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_STATE_HOME"

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

# Linux dependency bootstrap belongs to the build-tooling boundary. Product
# sources and CMake targets remain unaware of distribution package managers.
# The fast path is a pure local probe: package-manager/network/elevation work is
# attempted only when an actual required capability is missing.
such_linux_collect_missing_dependencies() {
  local profile="${1:-build}"
  SUCH_LINUX_MISSING_DEPENDENCIES=()

  local tool_name
  for tool_name in cmake ctest python3 make c++ readelf sha256sum pkg-config; do
    if ! command -v "$tool_name" >/dev/null 2>&1; then
      SUCH_LINUX_MISSING_DEPENDENCIES+=("tool:$tool_name")
    fi
  done

  if command -v pkg-config >/dev/null 2>&1; then
    pkg-config --exists x11 >/dev/null 2>&1 || SUCH_LINUX_MISSING_DEPENDENCIES+=("development:x11")
    pkg-config --exists libpng >/dev/null 2>&1 || SUCH_LINUX_MISSING_DEPENDENCIES+=("development:libpng")
  else
    SUCH_LINUX_MISSING_DEPENDENCIES+=("development:x11" "development:libpng")
  fi

  case "$profile" in
    build) ;;
    verify|release)
      for tool_name in xvfb-run Xvfb xauth timeout; do
        if ! command -v "$tool_name" >/dev/null 2>&1; then
          SUCH_LINUX_MISSING_DEPENDENCIES+=("verification:$tool_name")
        fi
      done
      ;;
    deb)
      if ! command -v dpkg-deb >/dev/null 2>&1; then
        SUCH_LINUX_MISSING_DEPENDENCIES+=("packaging:dpkg-deb")
      fi
      ;;
    *)
      printf 'Unknown Linux dependency profile: %s\n' "$profile" >&2
      return 2
      ;;
  esac
}

such_linux_print_missing_dependencies() {
  local item
  for item in "${SUCH_LINUX_MISSING_DEPENDENCIES[@]:-}"; do
    [[ -n "$item" ]] && printf '  - %s\n' "$item" >&2
  done
}

such_linux_detect_package_manager() {
  if command -v apt-get >/dev/null 2>&1; then
    printf 'apt\n'
  elif command -v dnf >/dev/null 2>&1; then
    printf 'dnf\n'
  elif command -v yum >/dev/null 2>&1; then
    printf 'yum\n'
  elif command -v zypper >/dev/null 2>&1; then
    printf 'zypper\n'
  elif command -v pacman >/dev/null 2>&1; then
    printf 'pacman\n'
  else
    return 1
  fi
}

such_linux_run_privileged() {
  if [[ "$(id -u)" == "0" ]]; then
    "$@"
    return
  fi
  if command -v sudo >/dev/null 2>&1; then
    sudo -- "$@"
    return
  fi
  if command -v doas >/dev/null 2>&1; then
    doas -- "$@"
    return
  fi
  printf 'Missing Linux build dependencies require package installation, but neither sudo nor doas is available.\n' >&2
  return 4
}

such_linux_offline_bundle_is_compatible() {
  [[ -x "$SUCH_LINUX_OFFLINE_BOOTSTRAP" ]] || return 1
  [[ -f "$SUCH_LINUX_OFFLINE_BUNDLE_DIR/SHA256SUMS" ]] || return 1
  [[ -d "$SUCH_LINUX_OFFLINE_BUNDLE_DIR/pool" ]] || return 1
  [[ -r /etc/os-release ]] || return 1
  command -v dpkg >/dev/null 2>&1 || return 1

  local distro_id distro_version distro_arch
  distro_id="$(. /etc/os-release; printf '%s' "${ID:-}")"
  distro_version="$(. /etc/os-release; printf '%s' "${VERSION_ID:-}")"
  distro_arch="$(dpkg --print-architecture 2>/dev/null || true)"
  [[ "$distro_id" == "debian" && "$distro_version" == "13" && "$distro_arch" == "amd64" ]]
}

such_linux_install_offline_bundle() {
  local profile="${1:-build}"
  if ! such_linux_offline_bundle_is_compatible; then
    return 1
  fi
  printf 'Installing missing Such Linux dependencies from bundled Debian 13 amd64 offline packages (%s)...\n' "$profile" >&2
  "$SUCH_LINUX_OFFLINE_BOOTSTRAP"
}

such_linux_install_dependency_packages() {
  local profile="${1:-build}"

  # On the qualified Debian 13 amd64 target the tracked offline bundle is the
  # authoritative first choice. This path performs no repository/network fetch.
  if such_linux_offline_bundle_is_compatible; then
    such_linux_install_offline_bundle "$profile"
    return 0
  fi

  local package_manager
  package_manager="$(such_linux_detect_package_manager || true)"
  if [[ -z "$package_manager" ]]; then
    printf 'Missing Linux build dependencies and no supported package manager was found.\n' >&2
    printf 'Supported automatic bootstrap managers: apt-get, dnf/yum, zypper, pacman.\n' >&2
    such_linux_print_missing_dependencies
    return 3
  fi

  local -a packages=()
  case "$package_manager" in
    apt)
      packages=(build-essential cmake python3 libx11-dev libpng-dev binutils pkg-config)
      [[ "$profile" == verify || "$profile" == release ]] && packages+=(xvfb xauth)
      [[ "$profile" == deb ]] && packages+=(dpkg-dev)
      printf 'Installing missing Such Linux build dependencies with apt-get...\n' >&2
      such_linux_run_privileged apt-get update
      such_linux_run_privileged env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"
      ;;
    dnf|yum)
      if [[ "$profile" == deb ]]; then
        printf 'build_deb.sh is Debian/Ubuntu packaging and requires dpkg-deb; automatic %s bootstrap is not applicable.\n' "$package_manager" >&2
        return 3
      fi
      packages=(gcc-c++ make cmake python3 libX11-devel libpng-devel binutils pkgconf-pkg-config)
      [[ "$profile" == verify || "$profile" == release ]] && packages+=(xorg-x11-server-Xvfb xorg-x11-xauth)
      printf 'Installing missing Such Linux build dependencies with %s...\n' "$package_manager" >&2
      such_linux_run_privileged "$package_manager" install -y "${packages[@]}"
      ;;
    zypper)
      if [[ "$profile" == deb ]]; then
        printf 'build_deb.sh is Debian/Ubuntu packaging and requires dpkg-deb; automatic zypper bootstrap is not applicable.\n' >&2
        return 3
      fi
      packages=(gcc-c++ make cmake python3 libX11-devel libpng16-devel binutils pkg-config)
      [[ "$profile" == verify || "$profile" == release ]] && packages+=(xorg-x11-server-Xvfb xauth)
      printf 'Installing missing Such Linux build dependencies with zypper...\n' >&2
      such_linux_run_privileged zypper --non-interactive install --no-recommends "${packages[@]}"
      ;;
    pacman)
      if [[ "$profile" == deb ]]; then
        printf 'build_deb.sh is Debian/Ubuntu packaging and requires dpkg-deb; automatic pacman bootstrap is not applicable.\n' >&2
        return 3
      fi
      packages=(base-devel cmake python libx11 libpng binutils pkgconf)
      [[ "$profile" == verify || "$profile" == release ]] && packages+=(xorg-server-xvfb xorg-xauth)
      printf 'Installing missing Such Linux build dependencies with pacman...\n' >&2
      such_linux_run_privileged pacman -S --needed --noconfirm "${packages[@]}"
      ;;
  esac
}

such_linux_ensure_dependencies() {
  local profile="${1:-build}"
  such_linux_collect_missing_dependencies "$profile"
  if (( ${#SUCH_LINUX_MISSING_DEPENDENCIES[@]} == 0 )); then
    printf 'Linux dependency check (%s): PASS\n' "$profile"
    return 0
  fi

  printf 'Linux dependency check (%s): missing capabilities detected:\n' "$profile" >&2
  such_linux_print_missing_dependencies
  such_linux_install_dependency_packages "$profile"

  # Re-probe the exact capabilities instead of assuming the package transaction
  # provided the commands/headers the build actually needs.
  hash -r
  such_linux_collect_missing_dependencies "$profile"
  if (( ${#SUCH_LINUX_MISSING_DEPENDENCIES[@]} != 0 )); then
    printf 'Linux dependency bootstrap completed, but required capabilities are still missing:\n' >&2
    such_linux_print_missing_dependencies
    return 3
  fi
  printf 'Linux dependency bootstrap (%s): PASS\n' "$profile"
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

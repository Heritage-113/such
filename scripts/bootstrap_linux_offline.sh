#!/bin/bash -p
set -euo pipefail

SCRIPT_SOURCE="${BASH_SOURCE[0]}"
case "$SCRIPT_SOURCE" in
  /*) SCRIPT_SOURCE_DIR="${SCRIPT_SOURCE%/*}" ;;
  */*) SCRIPT_SOURCE_DIR="$PWD/${SCRIPT_SOURCE%/*}" ;;
  *) SCRIPT_SOURCE_DIR="$PWD" ;;
esac
SCRIPT_DIR="$(cd "$SCRIPT_SOURCE_DIR" && pwd -P)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"
BUNDLE_DIR="$ROOT_DIR/third_party/linux/offline/debian13-amd64"
POOL_DIR="$BUNDLE_DIR/pool"
HASH_MANIFEST="$BUNDLE_DIR/SHA256SUMS"
PACKAGES_INDEX="$BUNDLE_DIR/Packages"

if [[ ! -r /etc/os-release ]]; then
  echo 'Cannot identify Linux distribution: /etc/os-release is missing.' >&2
  exit 20
fi
# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != "debian" || "${VERSION_ID:-}" != "13" ]]; then
  printf 'Bundled offline dependencies are qualified only for Debian 13; found ID=%s VERSION_ID=%s\n' "${ID:-?}" "${VERSION_ID:-?}" >&2
  exit 20
fi
if ! command -v dpkg >/dev/null 2>&1; then
  echo 'Debian dpkg is required to install the bundled offline dependencies.' >&2
  exit 20
fi
if [[ "$(dpkg --print-architecture)" != "amd64" ]]; then
  printf 'Bundled offline dependencies require amd64; found %s\n' "$(dpkg --print-architecture)" >&2
  exit 20
fi
if [[ ! -d "$POOL_DIR" || ! -f "$HASH_MANIFEST" || ! -f "$PACKAGES_INDEX" ]]; then
  printf 'Offline dependency bundle is incomplete: %s\n' "$BUNDLE_DIR" >&2
  exit 21
fi
if ! command -v sha256sum >/dev/null 2>&1; then
  echo 'sha256sum is required to verify the bundled dependency payload.' >&2
  exit 21
fi

(
  cd "$BUNDLE_DIR"
  sha256sum -c SHA256SUMS
)

run_privileged() {
  if [[ "$(id -u)" == "0" ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo -- "$@"
  elif command -v doas >/dev/null 2>&1; then
    doas -- "$@"
  else
    echo 'Installing bundled build dependencies requires root, sudo, or doas.' >&2
    return 22
  fi
}

ROOT_PACKAGES=(
  build-essential
  cmake
  python3
  libx11-dev
  libpng-dev
  binutils
  pkgconf
  xvfb
  xauth
  dpkg-dev
)

if command -v apt-get >/dev/null 2>&1; then
  # Build an isolated APT view that contains exactly one source: this local
  # file:// repository. No configured internet/intranet mirror is loaded.
  APT_TMP="$(mktemp -d /tmp/such-offline-apt.XXXXXX)"
  trap 'rm -rf "$APT_TMP"' EXIT
  mkdir -p "$APT_TMP/sourceparts" "$APT_TMP/lists/partial" "$APT_TMP/archives/partial"
  printf 'deb [trusted=yes] file:%s ./\n' "$BUNDLE_DIR" > "$APT_TMP/sources.list"

  APT_OPTIONS=(
    -o "Dir::Etc::sourcelist=$APT_TMP/sources.list"
    -o "Dir::Etc::sourceparts=$APT_TMP/sourceparts"
    -o "Dir::State::lists=$APT_TMP/lists"
    -o "Dir::Cache::archives=$APT_TMP/archives"
    -o "Acquire::Languages=none"
    -o "APT::Sandbox::User=root"
    -o "Acquire::CompressionTypes::Order::=gz"
    -o "Acquire::AllowInsecureRepositories=true"
  )

  run_privileged apt-get "${APT_OPTIONS[@]}" update
  run_privileged env DEBIAN_FRONTEND=noninteractive \
    apt-get "${APT_OPTIONS[@]}" install -y --no-install-recommends "${ROOT_PACKAGES[@]}"
else
  # Minimal Debian fallback. This path still uses only the verified local .deb
  # closure. Package order is resolved by unpacking all payloads first.
  mapfile -t DEB_FILES < <(find "$POOL_DIR" -maxdepth 1 -type f -name '*.deb' -print | LC_ALL=C sort)
  if (( ${#DEB_FILES[@]} == 0 )); then
    echo 'Offline dependency bundle contains no .deb packages.' >&2
    exit 21
  fi
  run_privileged dpkg --unpack "${DEB_FILES[@]}"
  run_privileged dpkg --configure -a
fi

echo 'Such bundled Debian 13 amd64 build dependencies: PASS'

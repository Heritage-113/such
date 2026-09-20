#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${CONFIG:-Release}"
CONFIG_LOWER="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/v1.0.0/public-linux-$CONFIG_LOWER}"
INSTALL_DIR="${INSTALL_DIR:-$ROOT/dist/v1.0.0/public-linux-$CONFIG_LOWER}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
CLEAN=0
REQUIRE_RUNTIME=0
RUNTIME_LIBRARY_PATH="${RUNTIME_LIBRARY_PATH:-}"
for arg in "$@"; do
  case "$arg" in
    --clean) CLEAN=1 ;;
    --require-runtime) REQUIRE_RUNTIME=1 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done
command -v cmake >/dev/null || { echo 'cmake not found' >&2; exit 2; }
command -v ctest >/dev/null || { echo 'ctest not found' >&2; exit 2; }
command -v python3 >/dev/null || { echo 'python3 not found' >&2; exit 2; }
python3 "$ROOT/scripts/audit_repository_layout.py"
if [[ "$CLEAN" == 1 ]]; then rm -rf "$BUILD_DIR" "$INSTALL_DIR"; fi
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DSUCH_BUILD_GUI=ON -DSUCH_BUILD_CLI=ON -DSUCH_BUILD_TESTS=ON \
  -DSUCH_STRICT_WARNINGS=ON
cmake --build "$BUILD_DIR" --parallel "$JOBS"
ctest --test-dir "$BUILD_DIR" --output-on-failure
cmake --install "$BUILD_DIR" --prefix "$INSTALL_DIR"

# Production search is not tracked in the public source repository. Release
# builders inject the runtime from .runtime/ (or RUNTIME_LIBRARY_PATH) and this
# script stages it only into build/install outputs.
if [[ -n "$RUNTIME_LIBRARY_PATH" ]]; then
  RUNTIME_PATH="$RUNTIME_LIBRARY_PATH"
else
  RUNTIME_PATH="$ROOT/.runtime/linux-x64/libSuchRuntimePrivate.so"
fi

expected_runtime_hash() {
  awk '$1=="linux-x64" && $2=="libSuchRuntimePrivate.so" {print $3; exit}' \
    "$ROOT/runtime/RUNTIME_SHA256_v1.0.0.txt"
}

if [[ -f "$RUNTIME_PATH" ]]; then
  if command -v readelf >/dev/null 2>&1; then
    machine="$(readelf -h "$RUNTIME_PATH" 2>/dev/null | awk -F: '/Machine:/{gsub(/^[ \t]+/,"",$2); print $2; exit}')"
    class="$(readelf -h "$RUNTIME_PATH" 2>/dev/null | awk -F: '/Class:/{gsub(/^[ \t]+/,"",$2); print $2; exit}')"
    if [[ "$class" != "ELF64" || "$machine" != *"X86-64"* ]]; then
      echo "Linux runtime architecture mismatch: class=$class machine=$machine path=$RUNTIME_PATH" >&2
      exit 8
    fi
  fi
  if [[ "$REQUIRE_RUNTIME" == 1 ]]; then
    command -v sha256sum >/dev/null || { echo 'sha256sum is required for release runtime verification' >&2; exit 10; }
    expected="$(expected_runtime_hash)"
    [[ -n "$expected" ]] || { echo 'Expected Linux runtime SHA-256 is missing from runtime manifest' >&2; exit 10; }
    actual="$(sha256sum "$RUNTIME_PATH" | awk '{print $1}')"
    if [[ "$actual" != "$expected" ]]; then
      echo "Linux release runtime SHA-256 mismatch" >&2
      echo "  expected: $expected" >&2
      echo "  actual  : $actual" >&2
      echo "  path    : $RUNTIME_PATH" >&2
      exit 10
    fi
    printf 'Release runtime SHA-256 PASS: %s\n' "$actual"
  fi
  mkdir -p "$BUILD_DIR" "$INSTALL_DIR/bin"
  cp -f "$RUNTIME_PATH" "$BUILD_DIR/libSuchRuntimePrivate.so"
  cp -f "$RUNTIME_PATH" "$INSTALL_DIR/bin/libSuchRuntimePrivate.so"
  runtime_dir="$(cd "$(dirname "$RUNTIME_PATH")" && pwd)"
  notice_dir="$INSTALL_DIR/share/doc/such/runtime"
  if [[ -f "$runtime_dir/LEGAL_NOTICE.txt" || -d "$runtime_dir/licenses" ]]; then
    mkdir -p "$notice_dir"
    [[ -f "$runtime_dir/LEGAL_NOTICE.txt" ]] && cp -f "$runtime_dir/LEGAL_NOTICE.txt" "$notice_dir/LEGAL_NOTICE.txt"
    if [[ -d "$runtime_dir/licenses" ]]; then
      rm -rf "$notice_dir/licenses"
      cp -R "$runtime_dir/licenses" "$notice_dir/licenses"
    fi
  fi
  printf 'Production runtime staged: %s\n' "$RUNTIME_PATH"
else
  if [[ "$REQUIRE_RUNTIME" == 1 ]]; then
    echo "Required Linux production runtime is missing: $RUNTIME_PATH" >&2
    exit 9
  fi
  printf 'Production runtime: not present (frontend-only build)\n'
fi

printf 'Public Linux build complete: %s\n' "$BUILD_DIR"
printf 'Public install staging: %s\n' "$INSTALL_DIR"

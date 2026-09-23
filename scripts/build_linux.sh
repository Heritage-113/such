#!/bin/bash -p
set -euo pipefail

SCRIPT_SOURCE="${BASH_SOURCE[0]}"
case "$SCRIPT_SOURCE" in
  /*) SCRIPT_SOURCE_DIR="${SCRIPT_SOURCE%/*}" ;;
  */*) SCRIPT_SOURCE_DIR="$PWD/${SCRIPT_SOURCE%/*}" ;;
  *) SCRIPT_SOURCE_DIR="$PWD" ;;
esac
SCRIPT_DIR="$(cd "$SCRIPT_SOURCE_DIR" && pwd -P)"
unset SCRIPT_SOURCE SCRIPT_SOURCE_DIR
# shellcheck source=linux_build_common.sh
source "$SCRIPT_DIR/linux_build_common.sh"

CLEAN=0
REQUIRE_RUNTIME=0
for arg in "$@"; do
  case "$arg" in
    --clean) CLEAN=1 ;;
    --require-runtime) REQUIRE_RUNTIME=1 ;;
    *) printf 'Unknown option: %s\n' "$arg" >&2; exit 2 ;;
  esac
done

CMAKE_BIN="$(such_linux_require_tool cmake)"
CTEST_BIN="$(such_linux_require_tool ctest)"
PYTHON_BIN="$(such_linux_require_tool python3)"
MAKE_BIN="$(such_linux_require_tool make)"
CXX_BIN="$(such_linux_require_tool c++)"
JOBS_COUNT="$(such_linux_jobs)"

"$PYTHON_BIN" "$SUCH_LINUX_ROOT_DIR/scripts/audit_repository_layout.py"

if [[ "$CLEAN" == 1 ]]; then
  rm -rf "$SUCH_LINUX_BUILD_DIR" "$SUCH_LINUX_INSTALL_DIR"
fi

mkdir -p "$SUCH_LINUX_BUILD_DIR" "$SUCH_LINUX_INSTALL_DIR"

"$CMAKE_BIN" -S "$SUCH_LINUX_ROOT_DIR" -B "$SUCH_LINUX_BUILD_DIR" \
  -G "Unix Makefiles" \
  -DCMAKE_MAKE_PROGRAM="$MAKE_BIN" \
  -DCMAKE_CXX_COMPILER="$CXX_BIN" \
  -DCMAKE_BUILD_TYPE="$SUCH_LINUX_CONFIG" \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF \
  -DSUCH_BUILD_GUI=ON \
  -DSUCH_BUILD_CLI=ON \
  -DSUCH_BUILD_TESTS=ON \
  -DSUCH_STRICT_WARNINGS=ON

"$CMAKE_BIN" --build "$SUCH_LINUX_BUILD_DIR" --parallel "$JOBS_COUNT"
"$CTEST_BIN" --test-dir "$SUCH_LINUX_BUILD_DIR" --output-on-failure
"$CMAKE_BIN" --install "$SUCH_LINUX_BUILD_DIR" --prefix "$SUCH_LINUX_INSTALL_DIR"

RUNTIME_HASH=""
if RUNTIME_HASH="$(such_linux_validate_runtime "$REQUIRE_RUNTIME")"; then
  mkdir -p "$SUCH_LINUX_INSTALL_DIR/bin"
  cp -f "$SUCH_LINUX_RUNTIME_FILE" "$SUCH_LINUX_BUILD_DIR/libSuchRuntimePrivate.so"
  cp -f "$SUCH_LINUX_RUNTIME_FILE" "$SUCH_LINUX_INSTALL_DIR/bin/libSuchRuntimePrivate.so"

  RUNTIME_DIR="$(cd "$(dirname "$SUCH_LINUX_RUNTIME_FILE")" && pwd -P)"
  NOTICE_DIR="$SUCH_LINUX_INSTALL_DIR/share/doc/such/runtime"
  if [[ -f "$RUNTIME_DIR/LEGAL_NOTICE.txt" || -d "$RUNTIME_DIR/licenses" ]]; then
    mkdir -p "$NOTICE_DIR"
    [[ -f "$RUNTIME_DIR/LEGAL_NOTICE.txt" ]] && cp -f "$RUNTIME_DIR/LEGAL_NOTICE.txt" "$NOTICE_DIR/LEGAL_NOTICE.txt"
    if [[ -d "$RUNTIME_DIR/licenses" ]]; then
      rm -rf "$NOTICE_DIR/licenses"
      cp -R "$RUNTIME_DIR/licenses" "$NOTICE_DIR/licenses"
    fi
  fi
  printf 'Production runtime staged and verified: %s\n' "$RUNTIME_HASH"
else
  runtime_status=$?
  if [[ "$runtime_status" != 1 ]]; then
    exit "$runtime_status"
  fi
  printf 'Production runtime: not present; frontend-only build completed\n'
fi

printf 'Public Linux build complete: %s\n' "$SUCH_LINUX_BUILD_DIR"
printf 'Public install staging: %s\n' "$SUCH_LINUX_INSTALL_DIR"

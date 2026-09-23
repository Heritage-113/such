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

readonly RELEASE_ARTIFACT="$SUCH_LINUX_ARTIFACT_DIR/Such_v1.1.7_Linux_x64_Public.zip"

case "$(uname -m)" in
  x86_64|amd64) ;;
  *) printf 'v1.1.7 release runtime is x64-only; host architecture is %s\n' "$(uname -m)" >&2; exit 8 ;;
esac

PYTHON_BIN="$(such_linux_require_tool python3)"
"$SUCH_LINUX_ROOT_DIR/scripts/verify_linux.sh" --clean --require-runtime

if find "$SUCH_LINUX_INSTALL_DIR" -type f \( -name '*.service' -o -path '*/autostart/*' -o -path '*/systemd/system/*' -o -path '*/systemd/user/*' \) -print -quit | grep -q .; then
  printf 'Release staging contains a forbidden resident-service/autostart payload\n' >&2
  exit 13
fi

mkdir -p "$SUCH_LINUX_ARTIFACT_DIR"
rm -f "$RELEASE_ARTIFACT"
"$PYTHON_BIN" - "$SUCH_LINUX_INSTALL_DIR" "$RELEASE_ARTIFACT" <<'PY'
from pathlib import Path
import sys, zipfile
src = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
if not (src / 'bin' / 'libSuchRuntimePrivate.so').is_file():
    raise SystemExit('release runtime missing from install staging')
with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for file_name in sorted(src.rglob('*')):
        if file_name.is_file():
            archive.write(file_name, Path('Such-v1.1.7-Linux-x64') / file_name.relative_to(src))
print(out)
PY
printf 'Public Linux release artifact: %s\n' "$RELEASE_ARTIFACT"

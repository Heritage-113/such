#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${CONFIG:-Release}"
CONFIG_LOWER="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
INSTALL_DIR="${INSTALL_DIR:-$ROOT/dist/v1.0.0/public-linux-$CONFIG_LOWER}"
ARTIFACT_DIR="${ARTIFACT_DIR:-$ROOT/dist/artifacts}"
ARTIFACT="$ARTIFACT_DIR/Such_v1.0.0_Linux_x64_Public.zip"
case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "v1.0.0 release runtime is x64-only; host architecture is $(uname -m)" >&2; exit 8 ;;
esac
"$ROOT/scripts/verify_linux.sh" --clean --require-runtime
mkdir -p "$ARTIFACT_DIR"
rm -f "$ARTIFACT"
python3 - "$INSTALL_DIR" "$ARTIFACT" <<'PY'
from pathlib import Path
import sys, zipfile
src = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
if not (src/'bin'/'libSuchRuntimePrivate.so').is_file():
    raise SystemExit('release runtime missing from install staging')
with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(src.rglob('*')):
        if p.is_file():
            z.write(p, Path('Such-v1.0.0-Linux-x64') / p.relative_to(src))
print(out)
PY
printf 'Public Linux release artifact: %s\n' "$ARTIFACT"

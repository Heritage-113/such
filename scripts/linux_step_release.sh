#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_artifact_state.sh
source "$SCRIPT_DIR/linux_artifact_state.sh"
such_linux_validate_staged_manifest
runtime="$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" runtime_path)"
[[ -n "$runtime" ]] || { echo 'Linux release requires a verified production runtime.' >&2; exit 12; }
if find "$SUCH_LINUX_INSTALL_DIR" -type f \( -name '*.service' -o -path '*/autostart/*' -o -path '*/systemd/system/*' -o -path '*/systemd/user/*' \) -print -quit | grep -q .; then
  echo 'Release staging contains a forbidden resident-service/autostart payload' >&2; exit 13
fi
artifact="$SUCH_LINUX_ARTIFACT_DIR/Such_v${SUCH_VERSION}_Linux_x64_PerUser.zip"
mkdir -p "$SUCH_LINUX_ARTIFACT_DIR"; rm -f "$artifact"
python3 - "$SUCH_LINUX_INSTALL_DIR" "$SCRIPT_DIR/linux_payload_installer.sh" "$SCRIPT_DIR/uninstall_linux.sh" "$artifact" "$SUCH_VERSION" <<'PY'
from pathlib import Path
import sys,zipfile
src=Path(sys.argv[1]); installer=Path(sys.argv[2]); uninstaller=Path(sys.argv[3]); out=Path(sys.argv[4]); version=sys.argv[5]
root=Path(f'Such-v{version}-Linux-x64-PerUser')
with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for f in sorted(src.rglob('*')):
        if f.is_file(): z.write(f,root/'payload'/f.relative_to(src))
    for source,name in ((installer,'install_linux.sh'),(uninstaller,'uninstall_linux.sh')):
        info=zipfile.ZipInfo(str(root/name)); info.external_attr=0o100755<<16
        z.writestr(info,source.read_bytes())
    info=zipfile.ZipInfo(str(root/'INSTALL.txt')); info.external_attr=0o100644<<16
    z.writestr(info,'Such for Linux installs for the current user only.\nRun: ./install_linux.sh\nDo not use sudo.\n')
PY
printf 'Linux release step PASS: %s\n' "$artifact"

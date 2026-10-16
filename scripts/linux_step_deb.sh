#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_artifact_state.sh
source "$SCRIPT_DIR/linux_artifact_state.sh"
such_linux_validate_staged_manifest
runtime="$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" runtime_path)"
[[ -n "$runtime" ]] || { echo 'DEB packaging requires a verified production runtime.' >&2; exit 12; }
case "$(uname -m)" in x86_64|amd64) ;; *) echo "Such $SUCH_VERSION DEB is amd64-only" >&2; exit 8;; esac
DPKG_DEB_BIN="$(such_linux_require_tool dpkg-deb)"
PACKAGE_ARCH=amd64
TMP="$(mktemp -d /tmp/such-deb.XXXXXX)"; trap 'rm -rf "$TMP"' EXIT
ROOT="$TMP/such_${SUCH_VERSION}_${PACKAGE_ARCH}"
DEB="$SUCH_LINUX_ARTIFACT_DIR/such_${SUCH_VERSION}_${PACKAGE_ARCH}.deb"
mkdir -p "$ROOT/DEBIAN" "$ROOT/opt/such/bin" "$ROOT/usr/bin" "$ROOT/usr/share/applications" "$ROOT/usr/share/icons/hicolor/256x256/apps" "$ROOT/usr/share/doc/such/runtime"
install -m 0755 "$SUCH_LINUX_INSTALL_DIR/bin/such" "$ROOT/opt/such/bin/such"
install -m 0755 "$SUCH_LINUX_INSTALL_DIR/bin/SuchCLI" "$ROOT/opt/such/bin/SuchCLI"
install -m 0755 "$SUCH_LINUX_INSTALL_DIR/bin/libSuchRuntimePrivate.so" "$ROOT/opt/such/bin/libSuchRuntimePrivate.so"
printf '#!/bin/sh\nexec /opt/such/bin/such "$@"\n' > "$ROOT/usr/bin/such"
printf '#!/bin/sh\nexec /opt/such/bin/SuchCLI "$@"\n' > "$ROOT/usr/bin/SuchCLI"
chmod 0755 "$ROOT/usr/bin/such" "$ROOT/usr/bin/SuchCLI"
[[ -f "$SUCH_LINUX_INSTALL_DIR/share/applications/such.desktop" ]] && install -m 0644 "$SUCH_LINUX_INSTALL_DIR/share/applications/such.desktop" "$ROOT/usr/share/applications/such.desktop"
[[ -f "$SUCH_LINUX_INSTALL_DIR/share/icons/hicolor/256x256/apps/such.png" ]] && install -m 0644 "$SUCH_LINUX_INSTALL_DIR/share/icons/hicolor/256x256/apps/such.png" "$ROOT/usr/share/icons/hicolor/256x256/apps/such.png"
[[ -d "$SUCH_LINUX_INSTALL_DIR/share/doc/such/runtime" ]] && cp -a "$SUCH_LINUX_INSTALL_DIR/share/doc/such/runtime/." "$ROOT/usr/share/doc/such/runtime/"
cat > "$ROOT/DEBIAN/control" <<CONTROL
Package: such
Version: $SUCH_VERSION
Section: utils
Priority: optional
Architecture: $PACKAGE_ARCH
Maintainer: Heritage Inc. <jimin@heritage-labs.net>
Depends: libc6, libstdc++6, libx11-6, libpng16-16 | libpng16-16t64
Description: Such local file search
 Native C++ local file search for Windows and Linux.
CONTROL
for maint in postinst postrm; do
cat > "$ROOT/DEBIAN/$maint" <<'MAINT'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true
exit 0
MAINT
chmod 0755 "$ROOT/DEBIAN/$maint"
done
if find "$ROOT" -type f \( -name '*.service' -o -path '*/etc/xdg/autostart/*' -o -path '*/systemd/system/*' -o -path '*/systemd/user/*' \) -print -quit | grep -q .; then echo 'DEB contains forbidden resident service/autostart payload' >&2; exit 13; fi
find "$ROOT" -type d -exec chmod 0755 {} +
mkdir -p "$SUCH_LINUX_ARTIFACT_DIR"; rm -f "$DEB"
"$DPKG_DEB_BIN" --build --root-owner-group "$ROOT" "$DEB"
"$DPKG_DEB_BIN" --info "$DEB" >/dev/null
printf 'Linux DEB step PASS: %s\n' "$DEB"

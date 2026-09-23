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

readonly PACKAGE_VERSION="1.1.7"
readonly PACKAGE_ARCH="amd64"
readonly PACKAGE_TMP="$(mktemp -d /tmp/such-deb.XXXXXX)"
trap 'rm -rf "$PACKAGE_TMP"' EXIT
readonly PACKAGE_ROOT="$PACKAGE_TMP/such_${PACKAGE_VERSION}_${PACKAGE_ARCH}"
readonly DEB_FILE="$SUCH_LINUX_ARTIFACT_DIR/such_${PACKAGE_VERSION}_${PACKAGE_ARCH}.deb"

case "$(uname -m)" in
  x86_64|amd64) ;;
  *) printf 'Such v1.1 Linux package is x64-only; host architecture is %s\n' "$(uname -m)" >&2; exit 8 ;;
esac

DPKG_DEB_BIN="$(such_linux_require_tool dpkg-deb)"

"$SUCH_LINUX_ROOT_DIR/scripts/build_linux.sh" --clean --require-runtime

GUI_FILE="$SUCH_LINUX_INSTALL_DIR/bin/such"
CLI_FILE="$SUCH_LINUX_INSTALL_DIR/bin/SuchCLI"
RUNTIME_FILE="$SUCH_LINUX_INSTALL_DIR/bin/libSuchRuntimePrivate.so"
for required_file in "$GUI_FILE" "$CLI_FILE" "$RUNTIME_FILE"; do
  [[ -f "$required_file" ]] || { printf 'Missing package input: %s\n' "$required_file" >&2; exit 7; }
done

mkdir -p \
  "$PACKAGE_ROOT/DEBIAN" \
  "$PACKAGE_ROOT/opt/such/bin" \
  "$PACKAGE_ROOT/usr/bin" \
  "$PACKAGE_ROOT/usr/share/applications" \
  "$PACKAGE_ROOT/usr/share/icons/hicolor/256x256/apps" \
  "$PACKAGE_ROOT/usr/share/doc/such/runtime"

install -m 0755 "$GUI_FILE" "$PACKAGE_ROOT/opt/such/bin/such"
install -m 0755 "$CLI_FILE" "$PACKAGE_ROOT/opt/such/bin/SuchCLI"
install -m 0755 "$RUNTIME_FILE" "$PACKAGE_ROOT/opt/such/bin/libSuchRuntimePrivate.so"

cat > "$PACKAGE_ROOT/usr/bin/such" <<'EOF_WRAPPER'
#!/bin/sh
exec /opt/such/bin/such "$@"
EOF_WRAPPER
cat > "$PACKAGE_ROOT/usr/bin/SuchCLI" <<'EOF_WRAPPER'
#!/bin/sh
exec /opt/such/bin/SuchCLI "$@"
EOF_WRAPPER
chmod 0755 "$PACKAGE_ROOT/usr/bin/such" "$PACKAGE_ROOT/usr/bin/SuchCLI"

if [[ -f "$SUCH_LINUX_INSTALL_DIR/share/applications/such.desktop" ]]; then
  install -m 0644 "$SUCH_LINUX_INSTALL_DIR/share/applications/such.desktop" \
    "$PACKAGE_ROOT/usr/share/applications/such.desktop"
fi
if [[ -f "$SUCH_LINUX_INSTALL_DIR/share/icons/hicolor/256x256/apps/such.png" ]]; then
  install -m 0644 "$SUCH_LINUX_INSTALL_DIR/share/icons/hicolor/256x256/apps/such.png" \
    "$PACKAGE_ROOT/usr/share/icons/hicolor/256x256/apps/such.png"
fi
if [[ -d "$SUCH_LINUX_INSTALL_DIR/share/doc/such/runtime" ]]; then
  cp -a "$SUCH_LINUX_INSTALL_DIR/share/doc/such/runtime/." "$PACKAGE_ROOT/usr/share/doc/such/runtime/"
fi

cat > "$PACKAGE_ROOT/DEBIAN/control" <<EOF_CONTROL
Package: such
Version: $PACKAGE_VERSION
Section: utils
Priority: optional
Architecture: $PACKAGE_ARCH
Maintainer: Heritage Inc. <jimin@heritage-labs.net>
Depends: libc6, libstdc++6, libx11-6, libpng16-16 | libpng16-16t64
Description: Such local file search
 Native C++ local file search for Windows and Linux.
EOF_CONTROL

cat > "$PACKAGE_ROOT/DEBIAN/postinst" <<'EOF_MAINT'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF_MAINT
chmod 0755 "$PACKAGE_ROOT/DEBIAN/postinst"

cat > "$PACKAGE_ROOT/DEBIAN/postrm" <<'EOF_MAINT'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF_MAINT
chmod 0755 "$PACKAGE_ROOT/DEBIAN/postrm"

if find "$PACKAGE_ROOT" -type f \( -name '*.service' -o -path '*/etc/xdg/autostart/*' -o -path '*/systemd/system/*' -o -path '*/systemd/user/*' \) -print -quit | grep -q .; then
  printf 'Debian package contains a forbidden resident-service/autostart payload\n' >&2
  exit 13
fi

find "$PACKAGE_ROOT" -type d -exec chmod 0755 {} +
mkdir -p "$SUCH_LINUX_ARTIFACT_DIR"
rm -f "$DEB_FILE"
"$DPKG_DEB_BIN" --build --root-owner-group "$PACKAGE_ROOT" "$DEB_FILE"

printf 'Debian package: %s\n' "$DEB_FILE"
"$DPKG_DEB_BIN" --info "$DEB_FILE"

#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${VERSION:-1.0.0}"
ARCH="${ARCH:-amd64}"
CONFIG="${CONFIG:-Release}"
CONFIG_LOWER="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
INSTALL_DIR="${INSTALL_DIR:-$ROOT/dist/v1.0.0/public-linux-$CONFIG_LOWER}"
ARTIFACT_DIR="${ARTIFACT_DIR:-$ROOT/dist/artifacts}"
PACKAGE_ROOT="$ROOT/build/deb/such_${VERSION}_${ARCH}"
DEB="$ARTIFACT_DIR/such_${VERSION}_${ARCH}.deb"

case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "Such v1.0 Linux package is x64-only; host architecture is $(uname -m)" >&2; exit 8 ;;
esac

command -v dpkg-deb >/dev/null || { echo "dpkg-deb not found. Install dpkg-dev/dpkg." >&2; exit 2; }

"$ROOT/scripts/build_linux.sh" --clean --require-runtime

GUI="$INSTALL_DIR/bin/such"
CLI="$INSTALL_DIR/bin/SuchCLI"
RUNTIME="$INSTALL_DIR/bin/libSuchRuntimePrivate.so"

for p in "$GUI" "$CLI" "$RUNTIME"; do
  [[ -f "$p" ]] || { echo "Missing package input: $p" >&2; exit 7; }
done

rm -rf "$PACKAGE_ROOT"
mkdir -p \
  "$PACKAGE_ROOT/DEBIAN" \
  "$PACKAGE_ROOT/opt/such/bin" \
  "$PACKAGE_ROOT/usr/bin" \
  "$PACKAGE_ROOT/usr/share/applications" \
  "$PACKAGE_ROOT/usr/share/icons/hicolor/256x256/apps" \
  "$PACKAGE_ROOT/usr/share/doc/such/runtime"

install -m 0755 "$GUI" "$PACKAGE_ROOT/opt/such/bin/such"
install -m 0755 "$CLI" "$PACKAGE_ROOT/opt/such/bin/SuchCLI"
install -m 0755 "$RUNTIME" "$PACKAGE_ROOT/opt/such/bin/libSuchRuntimePrivate.so"

cat > "$PACKAGE_ROOT/usr/bin/such" <<'EOF'
#!/usr/bin/env bash
exec /opt/such/bin/such "$@"
EOF
cat > "$PACKAGE_ROOT/usr/bin/SuchCLI" <<'EOF'
#!/usr/bin/env bash
exec /opt/such/bin/SuchCLI "$@"
EOF
chmod 0755 "$PACKAGE_ROOT/usr/bin/such" "$PACKAGE_ROOT/usr/bin/SuchCLI"

if [[ -f "$INSTALL_DIR/share/applications/such.desktop" ]]; then
  install -m 0644 "$INSTALL_DIR/share/applications/such.desktop" \
    "$PACKAGE_ROOT/usr/share/applications/such.desktop"
fi
if [[ -f "$INSTALL_DIR/share/icons/hicolor/256x256/apps/such.png" ]]; then
  install -m 0644 "$INSTALL_DIR/share/icons/hicolor/256x256/apps/such.png" \
    "$PACKAGE_ROOT/usr/share/icons/hicolor/256x256/apps/such.png"
fi

# Copy the complete Such documentation/legal payload from CMake install staging.
# This includes the public Apache-2.0 LICENSE and NOTICE plus the separate
# production-runtime notice and any runtime third-party legal files.
if [[ -d "$INSTALL_DIR/share/doc/such" ]]; then
  cp -a "$INSTALL_DIR/share/doc/such/." "$PACKAGE_ROOT/usr/share/doc/such/"
fi

cat > "$PACKAGE_ROOT/DEBIAN/control" <<EOF
Package: such
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Heritage Inc. <jimin@heritage-labs.net>
Depends: libc6, libstdc++6, libx11-6, libpng16-16 | libpng16-16t64
Description: Such local file search
 Native C++ local file search for Windows and Linux.
EOF

cat > "$PACKAGE_ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$PACKAGE_ROOT/DEBIAN/postinst"

cat > "$PACKAGE_ROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$PACKAGE_ROOT/DEBIAN/postrm"

find "$PACKAGE_ROOT" -type d -exec chmod 0755 {} +
mkdir -p "$ARTIFACT_DIR"
rm -f "$DEB"
dpkg-deb --build --root-owner-group "$PACKAGE_ROOT" "$DEB"

printf 'Debian package: %s\n' "$DEB"
dpkg-deb --info "$DEB"

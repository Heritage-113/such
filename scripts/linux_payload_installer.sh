#!/bin/bash -p
set -euo pipefail

if [[ "$(id -u)" == "0" ]]; then
  printf 'Such Linux is per-user only. Do not run this installer as root or through sudo.\n' >&2
  exit 13
fi

SCRIPT_SOURCE="${BASH_SOURCE[0]}"
case "$SCRIPT_SOURCE" in
  /*) SCRIPT_SOURCE_DIR="${SCRIPT_SOURCE%/*}" ;;
  */*) SCRIPT_SOURCE_DIR="$PWD/${SCRIPT_SOURCE%/*}" ;;
  *) SCRIPT_SOURCE_DIR="$PWD" ;;
esac
SCRIPT_DIR="$(cd "$SCRIPT_SOURCE_DIR" && pwd -P)"
unset SCRIPT_SOURCE SCRIPT_SOURCE_DIR

PAYLOAD_DIR=""
while (($#)); do
  case "$1" in
    --payload)
      shift
      [[ $# -gt 0 ]] || { printf 'Missing value for --payload\n' >&2; exit 2; }
      PAYLOAD_DIR="$1"
      ;;
    *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
  esac
  shift
done

if [[ -z "$PAYLOAD_DIR" ]]; then
  if [[ -x "$SCRIPT_DIR/payload/bin/such" ]]; then
    PAYLOAD_DIR="$SCRIPT_DIR/payload"
  else
    printf 'Could not locate the staged Such Linux payload. Use --payload <directory>.\n' >&2
    exit 7
  fi
fi
PAYLOAD_DIR="$(cd "$PAYLOAD_DIR" && pwd -P)"

for required in bin/such bin/SuchCLI bin/libSuchRuntimePrivate.so; do
  [[ -f "$PAYLOAD_DIR/$required" ]] || { printf 'Missing Linux install payload: %s\n' "$PAYLOAD_DIR/$required" >&2; exit 7; }
done

USER_HOME="${HOME:-}"
[[ -n "$USER_HOME" && "$USER_HOME" != "/" ]] || { printf 'A non-root HOME is required for per-user installation.\n' >&2; exit 13; }
USER_HOME="$(cd "$USER_HOME" && pwd -P)"
DATA_HOME="${XDG_DATA_HOME:-$USER_HOME/.local/share}"
USER_BIN="$USER_HOME/.local/bin"
APP_ROOT="$DATA_HOME/Heritage/Such"
APP_BIN="$APP_ROOT/bin"
APPLICATIONS_DIR="$DATA_HOME/applications"
ICONS_DIR="$DATA_HOME/icons/hicolor/256x256/apps"

case "$APP_ROOT" in
  "$USER_HOME"/*) ;;
  *)
    # XDG_DATA_HOME may intentionally live elsewhere, but a per-user installer
    # must never target a system prefix.
    case "$APP_ROOT" in
      /usr/*|/opt/*|/etc/*|/bin/*|/sbin/*|/lib/*|/lib64/*|/var/*)
        printf 'Refusing system-wide XDG_DATA_HOME for per-user Such install: %s\n' "$APP_ROOT" >&2
        exit 13
        ;;
    esac
    ;;
esac

mkdir -p "$APP_BIN" "$USER_BIN" "$APPLICATIONS_DIR" "$ICONS_DIR"
install -m 0755 "$PAYLOAD_DIR/bin/such" "$APP_BIN/such"
install -m 0755 "$PAYLOAD_DIR/bin/SuchCLI" "$APP_BIN/SuchCLI"
install -m 0755 "$PAYLOAD_DIR/bin/libSuchRuntimePrivate.so" "$APP_BIN/libSuchRuntimePrivate.so"

if [[ -d "$PAYLOAD_DIR/share/doc/such" ]]; then
  rm -rf "$APP_ROOT/doc"
  mkdir -p "$APP_ROOT/doc"
  cp -a "$PAYLOAD_DIR/share/doc/such/." "$APP_ROOT/doc/"
fi

link_owned_binary() {
  local link_path="$1"
  local target_path="$2"
  if [[ -e "$link_path" || -L "$link_path" ]]; then
    if [[ -L "$link_path" ]]; then
      local current
      current="$(readlink "$link_path" || true)"
      if [[ "$current" != "$APP_BIN/such" && "$current" != "$APP_BIN/SuchCLI" ]]; then
        printf 'Refusing to replace an unrelated user command: %s -> %s\n' "$link_path" "$current" >&2
        exit 13
      fi
    else
      printf 'Refusing to replace an existing user file: %s\n' "$link_path" >&2
      exit 13
    fi
  fi
  ln -sfn "$target_path" "$link_path"
}

link_owned_binary "$USER_BIN/such" "$APP_BIN/such"
link_owned_binary "$USER_BIN/SuchCLI" "$APP_BIN/SuchCLI"

cat > "$APPLICATIONS_DIR/such.desktop" <<EOF_DESKTOP
[Desktop Entry]
Type=Application
Name=Such
Comment=Local file search
Exec=$APP_BIN/such
Icon=such
Terminal=false
Categories=Utility;FileTools;
StartupNotify=true
EOF_DESKTOP
chmod 0644 "$APPLICATIONS_DIR/such.desktop"

if [[ -f "$PAYLOAD_DIR/share/icons/hicolor/256x256/apps/such.png" ]]; then
  install -m 0644 "$PAYLOAD_DIR/share/icons/hicolor/256x256/apps/such.png" "$ICONS_DIR/such.png"
fi

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true
fi

printf 'Such installed for current user only.\n'
printf '  app: %s\n' "$APP_ROOT"
printf '  cli: %s\n' "$USER_BIN/such"
case ":${PATH:-}:" in
  *":$USER_BIN:"*) ;;
  *) printf 'Note: add %s to PATH to launch Such from a shell.\n' "$USER_BIN" ;;
esac

#!/bin/bash -p
set -euo pipefail

if [[ "$(id -u)" == "0" ]]; then
  printf 'Such Linux is per-user only. Do not run this uninstaller as root or through sudo.\n' >&2
  exit 13
fi

USER_HOME="${HOME:-}"
[[ -n "$USER_HOME" && "$USER_HOME" != "/" ]] || { printf 'A non-root HOME is required for per-user uninstall.\n' >&2; exit 13; }
USER_HOME="$(cd "$USER_HOME" && pwd -P)"
DATA_HOME="${XDG_DATA_HOME:-$USER_HOME/.local/share}"
USER_BIN="$USER_HOME/.local/bin"
APP_ROOT="$DATA_HOME/Heritage/Such"
APP_BIN="$APP_ROOT/bin"
APPLICATIONS_DIR="$DATA_HOME/applications"
ICONS_DIR="$DATA_HOME/icons/hicolor/256x256/apps"

remove_owned_link() {
  local link_path="$1"
  local expected="$2"
  if [[ -L "$link_path" && "$(readlink "$link_path" || true)" == "$expected" ]]; then
    rm -f "$link_path"
  fi
}

remove_owned_link "$USER_BIN/such" "$APP_BIN/such"
remove_owned_link "$USER_BIN/SuchCLI" "$APP_BIN/SuchCLI"
rm -f "$APPLICATIONS_DIR/such.desktop"
rm -f "$ICONS_DIR/such.png"
rm -rf "$APP_ROOT"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true
fi

printf 'Such per-user application files removed. Search/index state was preserved.\n'

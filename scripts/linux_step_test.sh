#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=linux_build_common.sh
source "$SCRIPT_DIR/linux_build_common.sh"
# shellcheck source=linux_artifact_state.sh
source "$SCRIPT_DIR/linux_artifact_state.sh"
ROOT="$SUCH_LINUX_ROOT_DIR"
REQUIRE_RUNTIME=0
for arg in "$@"; do
  case "$arg" in
    --require-runtime) REQUIRE_RUNTIME=1 ;;
    *) printf 'Unknown test-step option: %s\n' "$arg" >&2; exit 2 ;;
  esac
done
BUILD_DIR="$SUCH_LINUX_BUILD_DIR"
STUB="$BUILD_DIR/libSuchRuntimeStub.so"
GUI="$BUILD_DIR/such"
CLI="$BUILD_DIR/SuchCLI"
PROD="$SUCH_LINUX_RUNTIME_FILE"
X11_INPUT_DRIVER="$BUILD_DIR/such_x11_input_driver"
LINUX_ROOTS_CONTRACT="$BUILD_DIR/such_linux_index_roots_contract"
SUCH_VERSION_DISPLAY="v$SUCH_VERSION"
export SUCH_VERSION_DISPLAY
for p in "$STUB" "$GUI" "$CLI"; do
  [[ -f "$p" ]] || { echo "Missing build output: $p" >&2; exit 7; }
done
RUNTIME_PATH_FOR_MANIFEST=""
if [[ -f "$PROD" ]]; then
  such_linux_validate_runtime "$REQUIRE_RUNTIME" >/dev/null
  RUNTIME_PATH_FOR_MANIFEST="$PROD"
elif [[ "$REQUIRE_RUNTIME" == 1 ]]; then
  echo 'Required Linux production runtime is missing' >&2
  exit 12
fi

assert_no_resident_exe() {
  local exe="$1"
  local label="$2"
  local expected
  expected="$(readlink -f "$exe")"
  sleep 0.25
  local proc target
  for proc in /proc/[0-9]*/exe; do
    [[ -e "$proc" ]] || continue
    target="$(readlink -f "$proc" 2>/dev/null || true)"
    if [[ -n "$target" && "$target" == "$expected" ]]; then
      local pid="${proc#/proc/}"; pid="${pid%/exe}"
      kill -TERM "$pid" 2>/dev/null || true
      sleep 0.1
      kill -KILL "$pid" 2>/dev/null || true
      echo "$label left a resident Such process after GUI smoke (pid=$pid)" >&2
      return 1
    fi
  done
}
# Public ABI contract smoke remains independent of the proprietary engine.
SUCH_RUNTIME_LIBRARY="$STUB" "$CLI" report >/dev/null
if command -v xvfb-run >/dev/null 2>&1; then
  SUCH_RUNTIME_LIBRARY="$STUB" xvfb-run -a "$GUI" --smoke >/dev/null 2>&1 || {
    echo 'X11 ABI stub smoke failed' >&2; exit 7;
  }

  # Drive an actual UTF-8 Hangul code point through the X11/XIM key boundary
  # and assert that the GUI sends the exact bytes to RuntimeClient. This is not
  # a substitute for a native IBus/Fcitx composition test, but it catches ANSI
  # conversion, Xutf8LookupString, XFilterEvent, and query-buffer regressions.
  [[ -x "$X11_INPUT_DRIVER" ]] || { echo "Missing X11 input driver: $X11_INPUT_DRIVER" >&2; exit 7; }
  X11_IME_TMP="$(mktemp -d)"
  X11_IME_ROOT="$X11_IME_TMP/root"
  X11_IME_LOG="$X11_IME_TMP/query.log"
  mkdir -p "$X11_IME_ROOT"
  set +e
  xvfb-run -a -s '-screen 0 1280x800x24' sh -c '
    export LANG=C.utf8 LC_ALL=C.utf8 XMODIFIERS=@im=local
    export SUCH_RUNTIME_LIBRARY="'"$STUB"'"
    export SUCH_FAKE_RUNTIME_INITIAL_ROOT="'"$X11_IME_ROOT"'"
    export SUCH_FAKE_RUNTIME_QUERY_LOG="'"$X11_IME_LOG"'"
    "'"$GUI"'" >/dev/null 2>"'"$X11_IME_TMP"'/gui.err" &
    app=$!
    sleep 0.5
    "'"$X11_INPUT_DRIVER"'" "Such ${SUCH_VERSION_DISPLAY}" U+AC00
    driver_rc=$?
    sleep 0.3
    kill "$app" 2>/dev/null || true
    wait "$app" 2>/dev/null || true
    exit "$driver_rc"
  '
  X11_IME_RC=$?
  set -e
  if [[ "$X11_IME_RC" != 0 ]]; then
    cat "$X11_IME_TMP/gui.err" >&2 || true
    rm -rf "$X11_IME_TMP"
    echo 'Linux XIM UTF-8 input smoke failed' >&2
    exit 7
  fi
  X11_IME_EXPECTED="$(printf '\352\260\200')"
  if ! grep -Fqx "$X11_IME_EXPECTED" "$X11_IME_LOG"; then
    printf 'Observed GUI query log:\n' >&2
    cat "$X11_IME_LOG" >&2 || true
    rm -rf "$X11_IME_TMP"
    echo 'Linux XIM did not deliver Korean UTF-8 to the runtime query boundary' >&2
    exit 7
  fi
  rm -rf "$X11_IME_TMP"
  printf 'Linux XIM Korean UTF-8 query-boundary smoke PASS\n'
fi

# A release/product verification must exercise the real runtime, not the stub.
if [[ -f "$PROD" ]]; then
  SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" report >/dev/null || {
    echo 'Linux production-runtime CLI smoke failed' >&2; exit 12;
  }
  INSIDE_TMP="$(mktemp -d)"
  INSIDE_HOME="$INSIDE_TMP/home"
  INSIDE_ROOT="$INSIDE_TMP/root"
  INSIDE_RUN="$INSIDE_TMP/run"
  INSIDE_CONFIG="$INSIDE_TMP/config"
  INSIDE_DATA="$INSIDE_TMP/data"
  mkdir -p "$INSIDE_HOME" "$INSIDE_ROOT" "$INSIDE_RUN" "$INSIDE_CONFIG" "$INSIDE_DATA"
  chmod 700 "$INSIDE_RUN"
  printf 'alpha SUCH_INSIDE_SENTINEL_918273 warranty omega\n' > "$INSIDE_ROOT/content_probe.txt"
  INSIDE_ENV=(
    "HOME=$INSIDE_HOME"
    "XDG_RUNTIME_DIR=$INSIDE_RUN"
    "XDG_CONFIG_HOME=$INSIDE_CONFIG"
    "XDG_DATA_HOME=$INSIDE_DATA"
    "SUCH_RUNTIME_LIBRARY=$PROD"
  )
  env "${INSIDE_ENV[@]}" "$CLI" "//drive $INSIDE_ROOT" >/dev/null || {
    rm -rf "$INSIDE_TMP"; echo 'Linux /inside fixture indexing failed' >&2; exit 12;
  }
  INSIDE_OUTPUT="$(env "${INSIDE_ENV[@]}" "$CLI" '/inside SUCH_INSIDE_SENTINEL_918273')" || {
    rm -rf "$INSIDE_TMP"; echo 'Linux /inside real-runtime query failed' >&2; exit 12;
  }
  if [[ "$INSIDE_OUTPUT" != *content_probe.txt* || "$INSIDE_OUTPUT" != *SUCH_INSIDE_SENTINEL_918273* ]]; then
    rm -rf "$INSIDE_TMP"; echo 'Linux /inside real-runtime result mismatch' >&2; exit 12
  fi
  rm -rf "$INSIDE_TMP"
  printf 'Linux /inside production-runtime search PASS\n'
  if command -v xvfb-run >/dev/null 2>&1; then
    SUCH_RUNTIME_LIBRARY="$PROD" xvfb-run -a "$GUI" --smoke >/dev/null 2>&1 || {
      echo 'Linux production-runtime GUI smoke failed' >&2; exit 12;
    }
    SUCH_UI_SCALE=2 SUCH_RUNTIME_LIBRARY="$PROD" xvfb-run -a "$GUI" --smoke >/dev/null 2>&1 || {
      echo 'Linux HiDPI (2x override) production-runtime GUI smoke failed' >&2; exit 12;
    }
    SUCH_RUNTIME_LIBRARY="$PROD" xvfb-run -a -s '-screen 0 3840x2160x24' "$GUI" --smoke >/dev/null 2>&1 || {
      echo 'Linux automatic 4K scaling production-runtime GUI smoke failed' >&2; exit 12;
    }
    printf 'Linux 2x override + automatic 4K vector/scaled GUI smoke PASS\n'

    # Fresh-install product contract: Linux must not silently index HOME.
    # A normal first launch opens the built-in //drive picker and leaves the
    # persistent root set empty until the user makes an explicit choice.
    BOOTSTRAP_TMP="$(mktemp -d)"
    BOOTSTRAP_HOME="$BOOTSTRAP_TMP/home"
    BOOTSTRAP_STATE="$BOOTSTRAP_TMP/state"
    BOOTSTRAP_FRONTEND_STATE="$BOOTSTRAP_TMP/frontend-state"
    BOOTSTRAP_RUN="$BOOTSTRAP_TMP/run"
    BOOTSTRAP_DATA="$BOOTSTRAP_TMP/data"
    mkdir -p "$BOOTSTRAP_HOME/Documents" "$BOOTSTRAP_STATE" "$BOOTSTRAP_FRONTEND_STATE" "$BOOTSTRAP_RUN" "$BOOTSTRAP_DATA"
    chmod 700 "$BOOTSTRAP_RUN"
    set +e
    timeout --kill-after=1s 2s xvfb-run -a env \
      HOME="$BOOTSTRAP_HOME" \
      XDG_DATA_HOME="$BOOTSTRAP_DATA" \
      XDG_STATE_HOME="$BOOTSTRAP_FRONTEND_STATE" \
      XDG_RUNTIME_DIR="$BOOTSTRAP_RUN" \
      SUCH_STATE_DIR="$BOOTSTRAP_STATE" \
      SUCH_RUNTIME_LIBRARY="$PROD" \
      "$GUI" >/dev/null 2>"$BOOTSTRAP_TMP/gui.err"
    BOOTSTRAP_GUI_RC=$?
    set -e
    if [[ "$BOOTSTRAP_GUI_RC" != 0 && "$BOOTSTRAP_GUI_RC" != 124 ]]; then
      cat "$BOOTSTRAP_TMP/gui.err" >&2 || true
      rm -rf "$BOOTSTRAP_TMP"
      echo 'Linux fresh-install GUI picker launch failed' >&2
      exit 12
    fi
    assert_no_resident_exe "$GUI" 'Linux fresh-install GUI' || { rm -rf "$BOOTSTRAP_TMP"; exit 12; }
    grep -Fq 'choose the first Linux search folder in the //drive picker' "$BOOTSTRAP_TMP/gui.err" || {
      cat "$BOOTSTRAP_TMP/gui.err" >&2 || true
      rm -rf "$BOOTSTRAP_TMP"
      echo 'Linux fresh-install GUI did not enter initial search-folder picker mode' >&2
      exit 12
    }
    BOOTSTRAP_ROOTS="$(env HOME="$BOOTSTRAP_HOME" XDG_DATA_HOME="$BOOTSTRAP_DATA" XDG_STATE_HOME="$BOOTSTRAP_FRONTEND_STATE" XDG_RUNTIME_DIR="$BOOTSTRAP_RUN" SUCH_STATE_DIR="$BOOTSTRAP_STATE" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" '//roots')"
    if [[ -n "$BOOTSTRAP_ROOTS" ]]; then
      printf 'Unexpected fresh-install roots:\n%s\n' "$BOOTSTRAP_ROOTS" >&2
      rm -rf "$BOOTSTRAP_TMP"
      echo 'Linux fresh-install GUI silently persisted a search root' >&2
      exit 12
    fi

    # Reopen the still-unconfigured profile and emulate an explicit user choice
    # in the built-in picker. Home is the first candidate, so Down + Return
    # selects it. This verifies that the first-run UI is not merely decorative.
    printf 'Such Linux bootstrap verification\n' > "$BOOTSTRAP_HOME/Documents/SuchBootstrapSentinel_74291.txt"
    set +e
    xvfb-run -a -s '-screen 0 1280x800x24' sh -c '
      export LANG=C.utf8 LC_ALL=C.utf8
      export HOME="'"$BOOTSTRAP_HOME"'" XDG_DATA_HOME="'"$BOOTSTRAP_DATA"'" XDG_STATE_HOME="'"$BOOTSTRAP_FRONTEND_STATE"'"
      export XDG_RUNTIME_DIR="'"$BOOTSTRAP_RUN"'" SUCH_STATE_DIR="'"$BOOTSTRAP_STATE"'" SUCH_RUNTIME_LIBRARY="'"$PROD"'"
      "'"$GUI"'" >/dev/null 2>"'"$BOOTSTRAP_TMP"'/picker.err" &
      app=$!
      sleep 0.6
      "'"$X11_INPUT_DRIVER"'" "Such ${SUCH_VERSION_DISPLAY}" Down Return
      driver_rc=$?
      sleep 3
      kill "$app" 2>/dev/null || true
      wait "$app" 2>/dev/null || true
      exit "$driver_rc"
    '
    BOOTSTRAP_PICK_RC=$?
    set -e
    if [[ "$BOOTSTRAP_PICK_RC" != 0 ]]; then
      cat "$BOOTSTRAP_TMP/picker.err" >&2 || true
      rm -rf "$BOOTSTRAP_TMP"
      echo 'Linux fresh-install search-folder picker interaction failed' >&2
      exit 12
    fi
    assert_no_resident_exe "$GUI" 'Linux fresh-install picker GUI' || { rm -rf "$BOOTSTRAP_TMP"; exit 12; }
    BOOTSTRAP_ROOTS="$(env HOME="$BOOTSTRAP_HOME" XDG_DATA_HOME="$BOOTSTRAP_DATA" XDG_STATE_HOME="$BOOTSTRAP_FRONTEND_STATE" XDG_RUNTIME_DIR="$BOOTSTRAP_RUN" SUCH_STATE_DIR="$BOOTSTRAP_STATE" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" '//roots')"
    if [[ "$BOOTSTRAP_ROOTS" != *"$BOOTSTRAP_HOME"* ]]; then
      cat "$BOOTSTRAP_TMP/picker.err" >&2 || true
      rm -rf "$BOOTSTRAP_TMP"
      echo 'Linux fresh-install picker did not persist the explicitly selected root' >&2
      exit 12
    fi
    BOOTSTRAP_HIT="$(env HOME="$BOOTSTRAP_HOME" XDG_DATA_HOME="$BOOTSTRAP_DATA" XDG_STATE_HOME="$BOOTSTRAP_FRONTEND_STATE" XDG_RUNTIME_DIR="$BOOTSTRAP_RUN" SUCH_STATE_DIR="$BOOTSTRAP_STATE" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" 'SuchBootstrapSentinel_74291')"
    if [[ "$BOOTSTRAP_HIT" != *"SuchBootstrapSentinel_74291.txt"* ]]; then
      cat "$BOOTSTRAP_TMP/picker.err" >&2 || true
      rm -rf "$BOOTSTRAP_TMP"
      echo 'Linux fresh-install selected root was persisted but search cannot return a real hit' >&2
      exit 12
    fi
    rm -rf "$BOOTSTRAP_TMP"
    printf 'Linux fresh-install explicit search-folder picker/selection/search PASS\n'

    # Upgrade-state regression: if every persisted root disappeared, clear the
    # stale set through the tracked worker and return to the same explicit picker.
    # This exercises the exact RuntimeClient unload -> worker -> reload boundary
    # that previously failed with "could not detach the Such index worker".
    STALE_TMP="$(mktemp -d)"
    STALE_HOME="$STALE_TMP/home"
    STALE_DATA="$STALE_HOME/.local/share"
    STALE_FRONTEND_STATE="$STALE_TMP/frontend-state"
    STALE_RUN="$STALE_TMP/run"
    STALE_ROOT="$STALE_TMP/removed-root"
    mkdir -p "$STALE_HOME" "$STALE_DATA" "$STALE_FRONTEND_STATE" "$STALE_RUN" "$STALE_ROOT"
    chmod 700 "$STALE_RUN"
    printf 'old\n' > "$STALE_ROOT/OldSentinel.txt"
    env HOME="$STALE_HOME" XDG_DATA_HOME="$STALE_DATA" XDG_STATE_HOME="$STALE_FRONTEND_STATE" XDG_RUNTIME_DIR="$STALE_RUN" SUCH_RUNTIME_LIBRARY="$PROD" \
      "$CLI" "//drive $STALE_ROOT" >/dev/null
    rm -rf "$STALE_ROOT"
    set +e
    timeout --kill-after=1s 3s xvfb-run -a env \
      HOME="$STALE_HOME" XDG_DATA_HOME="$STALE_DATA" XDG_STATE_HOME="$STALE_FRONTEND_STATE" XDG_RUNTIME_DIR="$STALE_RUN" \
      SUCH_RUNTIME_LIBRARY="$PROD" "$GUI" >/dev/null 2>"$STALE_TMP/gui.err"
    STALE_GUI_RC=$?
    set -e
    if [[ "$STALE_GUI_RC" != 0 && "$STALE_GUI_RC" != 124 ]]; then
      cat "$STALE_TMP/gui.err" >&2 || true
      rm -rf "$STALE_TMP"
      echo 'Linux stale-root recovery GUI launch failed' >&2
      exit 12
    fi
    assert_no_resident_exe "$GUI" 'Linux stale-root recovery GUI' || { rm -rf "$STALE_TMP"; exit 12; }
    if grep -Fq 'could not detach the Such index worker' "$STALE_TMP/gui.err"; then
      cat "$STALE_TMP/gui.err" >&2 || true
      rm -rf "$STALE_TMP"
      echo 'Linux tracked worker regressed to the old detach failure' >&2
      exit 12
    fi
    STALE_ROOTS="$(env HOME="$STALE_HOME" XDG_DATA_HOME="$STALE_DATA" XDG_STATE_HOME="$STALE_FRONTEND_STATE" XDG_RUNTIME_DIR="$STALE_RUN" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" '//roots')"
    if [[ -n "$STALE_ROOTS" ]]; then
      printf 'Recovered roots were:\n%s\n' "$STALE_ROOTS" >&2
      rm -rf "$STALE_TMP"
      echo 'Linux stale-root recovery did not clear the invalid persisted root set' >&2
      exit 12
    fi
    grep -Fq 'choose the first Linux search folder in the //drive picker' "$STALE_TMP/gui.err" || {
      cat "$STALE_TMP/gui.err" >&2 || true
      rm -rf "$STALE_TMP"
      echo 'Linux stale-root recovery did not return to search-folder picker mode' >&2
      exit 12
    }
    rm -rf "$STALE_TMP"
    printf 'Linux stale-root tracked-worker recovery/picker PASS\n'
    # Explicit-root regression: a valid //drive choice is authoritative across
    # GUI restarts. Startup recovery must not silently add HOME beside it.
    EXPLICIT_TMP="$(mktemp -d)"
    EXPLICIT_HOME="$EXPLICIT_TMP/home"
    EXPLICIT_DATA="$EXPLICIT_HOME/.local/share"
    EXPLICIT_FRONTEND_STATE="$EXPLICIT_TMP/frontend-state"
    EXPLICIT_RUN="$EXPLICIT_TMP/run"
    EXPLICIT_ROOT="$EXPLICIT_TMP/selected-drive"
    mkdir -p "$EXPLICIT_HOME/Documents" "$EXPLICIT_DATA" "$EXPLICIT_FRONTEND_STATE" "$EXPLICIT_RUN" "$EXPLICIT_ROOT"
    chmod 700 "$EXPLICIT_RUN"
    printf 'home must remain outside explicit drive
' > "$EXPLICIT_HOME/Documents/HomeMustNotBeIndexed.txt"
    printf 'selected drive corpus
' > "$EXPLICIT_ROOT/SuchExplicitDriveSentinel_38124.txt"
    env HOME="$EXPLICIT_HOME" XDG_DATA_HOME="$EXPLICIT_DATA" XDG_STATE_HOME="$EXPLICIT_FRONTEND_STATE" XDG_RUNTIME_DIR="$EXPLICIT_RUN" SUCH_RUNTIME_LIBRARY="$PROD" \
      "$CLI" "//drive $EXPLICIT_ROOT" >/dev/null
    set +e
    timeout --kill-after=1s 3s xvfb-run -a env \
      HOME="$EXPLICIT_HOME" XDG_DATA_HOME="$EXPLICIT_DATA" XDG_STATE_HOME="$EXPLICIT_FRONTEND_STATE" XDG_RUNTIME_DIR="$EXPLICIT_RUN" \
      SUCH_RUNTIME_LIBRARY="$PROD" "$GUI" >/dev/null 2>"$EXPLICIT_TMP/gui.err"
    EXPLICIT_GUI_RC=$?
    set -e
    if [[ "$EXPLICIT_GUI_RC" != 0 && "$EXPLICIT_GUI_RC" != 124 ]]; then
      cat "$EXPLICIT_TMP/gui.err" >&2 || true
      rm -rf "$EXPLICIT_TMP"
      echo 'Linux explicit-drive preservation GUI launch failed' >&2
      exit 12
    fi
    assert_no_resident_exe "$GUI" 'Linux explicit-drive GUI' || { rm -rf "$EXPLICIT_TMP"; exit 12; }
    EXPLICIT_ROOTS="$(env HOME="$EXPLICIT_HOME" XDG_DATA_HOME="$EXPLICIT_DATA" XDG_STATE_HOME="$EXPLICIT_FRONTEND_STATE" XDG_RUNTIME_DIR="$EXPLICIT_RUN" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" '//roots')"
    if [[ "$EXPLICIT_ROOTS" != "$EXPLICIT_ROOT" ]]; then
      printf 'Roots after explicit //drive and GUI restart were:
%s
' "$EXPLICIT_ROOTS" >&2
      rm -rf "$EXPLICIT_TMP"
      echo 'Linux startup recovery overrode an explicit //drive choice' >&2
      exit 12
    fi
    EXPLICIT_HIT="$(env HOME="$EXPLICIT_HOME" XDG_DATA_HOME="$EXPLICIT_DATA" XDG_STATE_HOME="$EXPLICIT_FRONTEND_STATE" XDG_RUNTIME_DIR="$EXPLICIT_RUN" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" 'SuchExplicitDriveSentinel_38124')"
    if [[ "$EXPLICIT_HIT" != *"SuchExplicitDriveSentinel_38124.txt"* ]]; then
      rm -rf "$EXPLICIT_TMP"
      echo 'Linux explicit drive remained selected but cannot return a search hit' >&2
      exit 12
    fi
    HOME_LEAK="$(env HOME="$EXPLICIT_HOME" XDG_DATA_HOME="$EXPLICIT_DATA" XDG_STATE_HOME="$EXPLICIT_FRONTEND_STATE" XDG_RUNTIME_DIR="$EXPLICIT_RUN" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" 'HomeMustNotBeIndexed')"
    if [[ -n "$HOME_LEAK" ]]; then
      rm -rf "$EXPLICIT_TMP"
      echo 'Linux explicit //drive was polluted by an automatic HOME root' >&2
      exit 12
    fi
    rm -rf "$EXPLICIT_TMP"
    printf 'Linux explicit-drive preservation/search PASS
'
  elif [[ "$REQUIRE_RUNTIME" == 1 ]]; then
    echo 'xvfb-run is required for strict Linux release GUI smoke' >&2
    exit 12
  fi

  # Samba/CIFS permission contract. Such never stores SMB credentials or
  # elevates around ACLs: it indexes an already-authenticated mount with the
  # current process credentials. Exercise both the public root-access gate and
  # the real private runtime under an unprivileged identity. An inaccessible
  # subtree must be skipped without preventing readable share content from
  # being indexed and searched.
  [[ -x "$LINUX_ROOTS_CONTRACT" ]] || { echo "Missing Linux root contract: $LINUX_ROOTS_CONTRACT" >&2; exit 12; }
  SAMBA_TMP="$(mktemp -d)"
  SAMBA_HOME="$SAMBA_TMP/home"
  SAMBA_STATE="$SAMBA_TMP/state"
  SAMBA_RUN="$SAMBA_TMP/run"
  SAMBA_SHARE="$SAMBA_TMP/share"
  SAMBA_PRIVATE="$SAMBA_SHARE/Private"
  mkdir -p "$SAMBA_HOME" "$SAMBA_STATE" "$SAMBA_RUN" "$SAMBA_SHARE/Public" "$SAMBA_PRIVATE"
  printf 'samba-visible\n' > "$SAMBA_SHARE/Public/SambaVisible_92173.txt"
  printf 'private\n' > "$SAMBA_PRIVATE/SecretShouldSkip.txt"
  chmod 700 "$SAMBA_RUN"

  if [[ "$(id -u)" == "0" ]]; then
    command -v runuser >/dev/null 2>&1 || { rm -rf "$SAMBA_TMP"; echo 'runuser is required for Samba permission qualification from a root host' >&2; exit 12; }
    id nobody >/dev/null 2>&1 || { rm -rf "$SAMBA_TMP"; echo 'a nobody account is required for Samba permission qualification from a root host' >&2; exit 12; }
    SAMBA_GROUP="$(id -gn nobody)"
    chown nobody:"$SAMBA_GROUP" "$SAMBA_TMP"
    chmod 755 "$SAMBA_TMP"
    chown -R nobody:"$SAMBA_GROUP" "$SAMBA_HOME" "$SAMBA_STATE" "$SAMBA_RUN" "$SAMBA_SHARE"
    chown root:root "$SAMBA_PRIVATE" "$SAMBA_PRIVATE/SecretShouldSkip.txt"
    chmod 700 "$SAMBA_PRIVATE"
    SAMBA_RUN_AS=(runuser -u nobody --)
  else
    chmod 000 "$SAMBA_PRIVATE"
    SAMBA_RUN_AS=()
  fi

  "${SAMBA_RUN_AS[@]}" "$LINUX_ROOTS_CONTRACT" --expect-accessible "$SAMBA_SHARE" || {
    rm -rf "$SAMBA_TMP"; echo 'Linux Samba/CIFS current-user access gate rejected a readable share root' >&2; exit 12;
  }
  "${SAMBA_RUN_AS[@]}" "$LINUX_ROOTS_CONTRACT" --expect-inaccessible "$SAMBA_PRIVATE" || {
    rm -rf "$SAMBA_TMP"; echo 'Linux Samba/CIFS current-user access gate accepted a denied share directory' >&2; exit 12;
  }

  SAMBA_ENV=(env HOME="$SAMBA_HOME" XDG_DATA_HOME="$SAMBA_HOME/.local/share" XDG_STATE_HOME="$SAMBA_HOME/.local/state" XDG_RUNTIME_DIR="$SAMBA_RUN" SUCH_STATE_DIR="$SAMBA_STATE" SUCH_RUNTIME_LIBRARY="$PROD" PATH="/usr/local/bin:/usr/bin:/bin")
  "${SAMBA_RUN_AS[@]}" "${SAMBA_ENV[@]}" "$CLI" "//drive $SAMBA_SHARE" >/dev/null || {
    rm -rf "$SAMBA_TMP"; echo 'Linux Samba/CIFS permission fixture indexing failed' >&2; exit 12;
  }
  SAMBA_HIT="$("${SAMBA_RUN_AS[@]}" "${SAMBA_ENV[@]}" "$CLI" 'SambaVisible_92173')" || {
    rm -rf "$SAMBA_TMP"; echo 'Linux Samba/CIFS readable-share search failed' >&2; exit 12;
  }
  if [[ "$SAMBA_HIT" != *'SambaVisible_92173.txt'* ]]; then
    rm -rf "$SAMBA_TMP"; echo 'Linux Samba/CIFS readable-share search returned no real hit' >&2; exit 12
  fi
  SAMBA_PRIVATE_HIT="$("${SAMBA_RUN_AS[@]}" "${SAMBA_ENV[@]}" "$CLI" 'SecretShouldSkip')" || true
  if [[ "$SAMBA_PRIVATE_HIT" == *'SecretShouldSkip.txt'* ]]; then
    rm -rf "$SAMBA_TMP"; echo 'Linux Samba/CIFS crawler indexed a current-user-inaccessible subtree' >&2; exit 12
  fi
  chmod 700 "$SAMBA_TMP" 2>/dev/null || true
  rm -rf "$SAMBA_TMP"
  printf 'Linux Samba/CIFS current-user permission/index/search PASS\n'

  # Rich-query regression: exercise the real runtime with the new public
  # compatibility layer. This catches the exact class of bug where syntax tests
  # pass but a production runtime never returns DWG/date/detail results.
  QUERY_TMP="$(mktemp -d)"
  QUERY_HOME="$QUERY_TMP/home"
  QUERY_DATA="$QUERY_TMP/data"
  QUERY_STATE="$QUERY_TMP/state"
  QUERY_RUN="$QUERY_TMP/run"
  QUERY_ROOT="$QUERY_TMP/corpus"
  mkdir -p "$QUERY_HOME" "$QUERY_DATA" "$QUERY_STATE" "$QUERY_RUN" "$QUERY_ROOT"
  chmod 700 "$QUERY_RUN"
  printf 'dwg sentinel\n' > "$QUERY_ROOT/Project_Structure_2026.dwg"
  printf 'pdf sentinel\n' > "$QUERY_ROOT/Project_Other_2026.pdf"
  printf 'old sentinel\n' > "$QUERY_ROOT/Project_Structure_2025.dwg"
  python3 - "$QUERY_ROOT" <<'EOF_PY'
import os, sys, datetime
root=sys.argv[1]
def stamp(name, y,m,d):
    dt=datetime.datetime(y,m,d,12,0,0).astimezone()
    t=dt.timestamp(); os.utime(os.path.join(root,name),(t,t))
stamp('Project_Structure_2026.dwg', 2026,9,10)
stamp('Project_Other_2026.pdf', 2026,9,12)
stamp('Project_Structure_2025.dwg', 2025,9,10)
EOF_PY
  qenv=(env HOME="$QUERY_HOME" XDG_DATA_HOME="$QUERY_DATA" XDG_STATE_HOME="$QUERY_STATE" XDG_RUNTIME_DIR="$QUERY_RUN" SUCH_STATE_DIR="$QUERY_STATE" SUCH_RUNTIME_LIBRARY="$PROD")
  "${qenv[@]}" "$CLI" "//drive $QUERY_ROOT" >/dev/null
  DWG_HIT="$("${qenv[@]}" "$CLI" '//dwg Project_Structure_2026')"
  [[ "$DWG_HIT" == *'Project_Structure_2026.dwg'* ]] || { rm -rf "$QUERY_TMP"; echo 'Production runtime DWG search regression failed' >&2; exit 12; }
  DETAIL_HIT="$("${qenv[@]}" "$CLI" 'Project /; Structure /; 2026')"
  [[ "$DETAIL_HIT" == *'Project_Structure_2026.dwg'* ]] || { rm -rf "$QUERY_TMP"; echo 'Production runtime detail-search regression failed' >&2; exit 12; }
  DATE_HIT="$("${qenv[@]}" "$CLI" '/20260901-20260919 Project')"
  [[ "$DATE_HIT" == *'Project_Structure_2026.dwg'* && "$DATE_HIT" == *'Project_Other_2026.pdf'* && "$DATE_HIT" != *'Project_Structure_2025.dwg'* ]] || { rm -rf "$QUERY_TMP"; echo 'Production runtime compact-date regression failed' >&2; exit 12; }
  DATE_TILDE="$("${qenv[@]}" "$CLI" '/20260901~20260919 Project')"
  [[ "$DATE_TILDE" == *'Project_Structure_2026.dwg'* ]] || { rm -rf "$QUERY_TMP"; echo 'Production runtime tilde-date regression failed' >&2; exit 12; }
  DATE_MDY="$("${qenv[@]}" "$CLI" '/09012026-09192026 Project')"
  [[ "$DATE_MDY" == *'Project_Structure_2026.dwg'* ]] || { rm -rf "$QUERY_TMP"; echo 'Production runtime MMDDYYYY-date regression failed' >&2; exit 12; }
  rm -rf "$QUERY_TMP"
  printf 'Linux DWG/date/detail production-runtime search PASS\n'

  # Real production-runtime Korean filename regression. This is intentionally
  # separate from the XIM stub test above: one gate proves GUI UTF-8 input, the
  # other proves those bytes actually match Korean/mixed filenames in the engine.
  KOREAN_TMP="$(mktemp -d)"
  KOREAN_HOME="$KOREAN_TMP/home"
  KOREAN_DATA="$KOREAN_TMP/data"
  KOREAN_STATE="$KOREAN_TMP/state"
  KOREAN_RUN="$KOREAN_TMP/run"
  KOREAN_ROOT="$KOREAN_TMP/corpus"
  mkdir -p "$KOREAN_HOME" "$KOREAN_DATA" "$KOREAN_STATE" "$KOREAN_RUN" "$KOREAN_ROOT/테스트폴더"
  chmod 700 "$KOREAN_RUN"
  : > "$KOREAN_ROOT/한글검색테스트.txt"
  : > "$KOREAN_ROOT/건축도면_구조.dwg"
  : > "$KOREAN_ROOT/아주대학교_설계.pdf"
  : > "$KOREAN_ROOT/테스트폴더/문서.txt"
  : > "$KOREAN_ROOT/project_설계.txt"
  : > "$KOREAN_ROOT/A동_구조.txt"
  : > "$KOREAN_ROOT/2026_건축.txt"
  kenv=(env HOME="$KOREAN_HOME" XDG_DATA_HOME="$KOREAN_DATA" XDG_STATE_HOME="$KOREAN_STATE" XDG_RUNTIME_DIR="$KOREAN_RUN" SUCH_STATE_DIR="$KOREAN_STATE" SUCH_RUNTIME_LIBRARY="$PROD")
  "${kenv[@]}" "$CLI" "//drive $KOREAN_ROOT" >/dev/null
  check_korean_hit() {
    local query="$1" expected="$2" output
    output="$("${kenv[@]}" "$CLI" "$query")"
    [[ "$output" == *"$expected"* ]] || {
      printf 'Korean query failed: %s -> expected %s\n' "$query" "$expected" >&2
      rm -rf "$KOREAN_TMP"
      exit 12
    }
  }
  check_korean_hit '한글' '한글검색테스트.txt'
  check_korean_hit '한글검색' '한글검색테스트.txt'
  check_korean_hit '건축' '건축도면_구조.dwg'
  check_korean_hit '구조' '건축도면_구조.dwg'
  check_korean_hit '아주대학교' '아주대학교_설계.pdf'
  check_korean_hit '설계' '아주대학교_설계.pdf'
  check_korean_hit '문서' '문서.txt'
  check_korean_hit 'project_설계' 'project_설계.txt'
  check_korean_hit 'A동_구조' 'A동_구조.txt'
  check_korean_hit '2026_건축' '2026_건축.txt'
  rm -rf "$KOREAN_TMP"
  printf 'Linux Korean/mixed filename production-runtime search PASS\n'

  # Index Snapshot: carry the opaque runtime state to a fresh state
  # directory and prove the imported database answers immediately without a
  # replacement-root/reindex operation. The original absolute root path must
  # still exist; root remapping is deliberately deferred until a private ABI
  # contract exists for it.
  SNAP_TMP="$(mktemp -d)"
  SNAP_HOME="$SNAP_TMP/home"
  SNAP_RUN="$SNAP_TMP/run"
  SNAP_ROOT="$SNAP_TMP/large-disk"
  SNAP_STATE_A="$SNAP_TMP/state-a"
  SNAP_STATE_B="$SNAP_TMP/state-b"
  SNAP_FILE="$SNAP_TMP/portable.sidx"
  mkdir -p "$SNAP_HOME" "$SNAP_RUN" "$SNAP_ROOT/archive"
  chmod 700 "$SNAP_RUN"
  printf 'portable-index-fixture\n' > "$SNAP_ROOT/archive/SuchSnapshotSentinel_125.dwg"
  snapenv=(env HOME="$SNAP_HOME" XDG_DATA_HOME="$SNAP_HOME/.local/share" XDG_RUNTIME_DIR="$SNAP_RUN" SUCH_RUNTIME_LIBRARY="$PROD")
  "${snapenv[@]}" SUCH_STATE_DIR="$SNAP_STATE_A" "$CLI" root "$SNAP_ROOT" >/dev/null || { rm -rf "$SNAP_TMP"; echo 'Linux snapshot source indexing failed' >&2; exit 12; }
  SNAP_A_STATUS="$("${snapenv[@]}" SUCH_STATE_DIR="$SNAP_STATE_A" "$CLI" status)"
  "${snapenv[@]}" SUCH_STATE_DIR="$SNAP_STATE_A" "$CLI" snapshot export "$SNAP_FILE" >/dev/null || { rm -rf "$SNAP_TMP"; echo 'Linux snapshot export failed' >&2; exit 12; }
  SNAP_INFO="$("${snapenv[@]}" SUCH_STATE_DIR="$SNAP_STATE_A" "$CLI" snapshot info "$SNAP_FILE")"
  [[ "$SNAP_INFO" == *'format=1'* && "$SNAP_INFO" == *"root=$SNAP_ROOT"* ]] || { rm -rf "$SNAP_TMP"; echo 'Linux snapshot metadata mismatch' >&2; exit 12; }
  "${snapenv[@]}" SUCH_STATE_DIR="$SNAP_STATE_B" "$CLI" snapshot import "$SNAP_FILE" >/dev/null || { rm -rf "$SNAP_TMP"; echo 'Linux snapshot import failed' >&2; exit 12; }
  SNAP_B_STATUS="$("${snapenv[@]}" SUCH_STATE_DIR="$SNAP_STATE_B" "$CLI" status)"
  SNAP_HIT="$("${snapenv[@]}" SUCH_STATE_DIR="$SNAP_STATE_B" "$CLI" 'SuchSnapshotSentinel_125')"
  [[ "$SNAP_HIT" == *'SuchSnapshotSentinel_125.dwg'* ]] || { rm -rf "$SNAP_TMP"; echo 'Imported Linux snapshot did not answer from transported index' >&2; exit 12; }
  SNAP_A_GEN="$(printf '%s\n' "$SNAP_A_STATUS" | sed -n 's/^generation=//p')"
  SNAP_B_GEN="$(printf '%s\n' "$SNAP_B_STATUS" | sed -n 's/^generation=//p')"
  [[ -n "$SNAP_A_GEN" && "$SNAP_A_GEN" == "$SNAP_B_GEN" && "$SNAP_B_STATUS" == *'indexing=no'* ]] || { rm -rf "$SNAP_TMP"; echo 'Linux snapshot import unexpectedly changed generation or started indexing' >&2; exit 12; }
  rm -rf "$SNAP_TMP"
  printf 'Linux opaque Index Snapshot export/import/no-reindex PASS\n'

  printf 'Linux production-runtime smoke PASS\n'
elif [[ "$REQUIRE_RUNTIME" == 1 ]]; then
  echo 'Required Linux production runtime was not staged' >&2
  exit 12
fi
such_linux_write_verified_manifest "$GUI" "$CLI" "$STUB" "$RUNTIME_PATH_FOR_MANIFEST" "$REQUIRE_RUNTIME"
printf 'Linux test step PASS\n'

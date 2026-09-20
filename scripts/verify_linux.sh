#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REQUIRE_RUNTIME=0
for arg in "$@"; do
  [[ "$arg" == "--require-runtime" ]] && REQUIRE_RUNTIME=1
done
"$ROOT/scripts/build_linux.sh" "$@"
CONFIG="${CONFIG:-Release}"
CONFIG_LOWER="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/v1.0.0/public-linux-$CONFIG_LOWER}"
INSTALL_DIR="${INSTALL_DIR:-$ROOT/dist/v1.0.0/public-linux-$CONFIG_LOWER}"
STUB="$BUILD_DIR/libSuchRuntimeStub.so"
GUI="$BUILD_DIR/such"
CLI="$BUILD_DIR/SuchCLI"
PROD="$BUILD_DIR/libSuchRuntimePrivate.so"
for p in "$STUB" "$GUI" "$CLI"; do
  [[ -f "$p" ]] || { echo "Missing build output: $p" >&2; exit 7; }
done

# Public ABI contract smoke remains independent of the proprietary engine.
SUCH_RUNTIME_LIBRARY="$STUB" "$CLI" report >/dev/null
if command -v xvfb-run >/dev/null 2>&1; then
  SUCH_RUNTIME_LIBRARY="$STUB" xvfb-run -a "$GUI" --smoke >/dev/null 2>&1 || {
    echo 'X11 ABI stub smoke failed' >&2; exit 7;
  }
fi

# A release/product verification must exercise the real runtime, not the stub.
if [[ -f "$PROD" ]]; then
  SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" report >/dev/null || {
    echo 'Linux production-runtime CLI smoke failed' >&2; exit 12;
  }
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

    # Fresh-install product contract: a normal Linux GUI launch must bootstrap
    # the current user's HOME as the first search root. Without this test the
    # frontend can paint successfully while every real query returns nothing.
    BOOTSTRAP_TMP="$(mktemp -d)"
    BOOTSTRAP_HOME="$BOOTSTRAP_TMP/home"
    BOOTSTRAP_STATE="$BOOTSTRAP_TMP/state"
    BOOTSTRAP_FRONTEND_STATE="$BOOTSTRAP_TMP/frontend-state"
    BOOTSTRAP_RUN="$BOOTSTRAP_TMP/run"
    BOOTSTRAP_DATA="$BOOTSTRAP_TMP/data"
    mkdir -p "$BOOTSTRAP_HOME/Documents" "$BOOTSTRAP_STATE" "$BOOTSTRAP_FRONTEND_STATE" "$BOOTSTRAP_RUN" "$BOOTSTRAP_DATA"
    chmod 700 "$BOOTSTRAP_RUN"
    printf 'Such Linux bootstrap verification\n' > "$BOOTSTRAP_HOME/Documents/SuchBootstrapSentinel_74291.txt"
    set +e
    timeout --kill-after=1s 3s xvfb-run -a env \
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
      echo 'Linux fresh-install GUI bootstrap failed' >&2
      exit 12
    fi
    BOOTSTRAP_ROOTS="$(env HOME="$BOOTSTRAP_HOME" XDG_DATA_HOME="$BOOTSTRAP_DATA" XDG_STATE_HOME="$BOOTSTRAP_FRONTEND_STATE" XDG_RUNTIME_DIR="$BOOTSTRAP_RUN" SUCH_STATE_DIR="$BOOTSTRAP_STATE" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" '//roots')"
    if [[ "$BOOTSTRAP_ROOTS" != *"$BOOTSTRAP_HOME"* ]]; then
      cat "$BOOTSTRAP_TMP/gui.err" >&2 || true
      rm -rf "$BOOTSTRAP_TMP"
      echo 'Linux fresh-install GUI did not persist HOME as a search root' >&2
      exit 12
    fi
    BOOTSTRAP_HIT="$(env HOME="$BOOTSTRAP_HOME" XDG_DATA_HOME="$BOOTSTRAP_DATA" XDG_STATE_HOME="$BOOTSTRAP_FRONTEND_STATE" XDG_RUNTIME_DIR="$BOOTSTRAP_RUN" SUCH_STATE_DIR="$BOOTSTRAP_STATE" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" 'SuchBootstrapSentinel_74291')"
    if [[ "$BOOTSTRAP_HIT" != *"SuchBootstrapSentinel_74291.txt"* ]]; then
      cat "$BOOTSTRAP_TMP/gui.err" >&2 || true
      rm -rf "$BOOTSTRAP_TMP"
      echo 'Linux fresh-install index exists but cannot return a real search hit' >&2
      exit 12
    fi
    rm -rf "$BOOTSTRAP_TMP"
    printf 'Linux fresh-install HOME bootstrap/search PASS\n'

    # Upgrade-state regression: older builds may have persisted a root that no
    # longer exists. A non-empty roots.txt must not suppress HOME bootstrap.
    STALE_TMP="$(mktemp -d)"
    STALE_HOME="$STALE_TMP/home"
    STALE_DATA="$STALE_HOME/.local/share"
    STALE_FRONTEND_STATE="$STALE_TMP/frontend-state"
    STALE_RUN="$STALE_TMP/run"
    STALE_ROOT="$STALE_TMP/removed-root"
    mkdir -p "$STALE_HOME/Documents" "$STALE_DATA" "$STALE_FRONTEND_STATE" "$STALE_RUN" "$STALE_ROOT"
    chmod 700 "$STALE_RUN"
    printf 'Such Linux stale-root recovery verification\n' > "$STALE_HOME/Documents/SuchStaleRootSentinel_91537.txt"
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
    STALE_ROOTS="$(env HOME="$STALE_HOME" XDG_DATA_HOME="$STALE_DATA" XDG_STATE_HOME="$STALE_FRONTEND_STATE" XDG_RUNTIME_DIR="$STALE_RUN" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" '//roots')"
    if [[ "$STALE_ROOTS" != *"$STALE_HOME"* || "$STALE_ROOTS" == *"removed-root"* ]]; then
      cat "$STALE_TMP/gui.err" >&2 || true
      printf 'Recovered roots were:\n%s\n' "$STALE_ROOTS" >&2
      rm -rf "$STALE_TMP"
      echo 'Linux stale-root recovery did not replace invalid persisted roots with HOME' >&2
      exit 12
    fi
    STALE_HIT="$(env HOME="$STALE_HOME" XDG_DATA_HOME="$STALE_DATA" XDG_STATE_HOME="$STALE_FRONTEND_STATE" XDG_RUNTIME_DIR="$STALE_RUN" SUCH_RUNTIME_LIBRARY="$PROD" "$CLI" 'SuchStaleRootSentinel_91537')"
    if [[ "$STALE_HIT" != *"SuchStaleRootSentinel_91537.txt"* ]]; then
      cat "$STALE_TMP/gui.err" >&2 || true
      rm -rf "$STALE_TMP"
      echo 'Linux stale-root recovery completed but search still cannot return a hit' >&2
      exit 12
    fi
    rm -rf "$STALE_TMP"
    printf 'Linux stale-root recovery/search PASS\n'
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

  staged="$INSTALL_DIR/bin/libSuchRuntimePrivate.so"
  [[ -f "$staged" ]] || { echo "Missing production runtime in install artifact: $staged" >&2; exit 12; }
  cmp -s "$PROD" "$staged" || { echo 'Staged Linux runtime differs from verified build runtime' >&2; exit 12; }
  printf 'Linux production-runtime smoke PASS\n'
elif [[ "$REQUIRE_RUNTIME" == 1 ]]; then
  echo 'Required Linux production runtime was not staged' >&2
  exit 12
fi
printf 'Public Linux verification PASS\n'

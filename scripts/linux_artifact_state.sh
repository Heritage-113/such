#!/bin/bash
if [[ "${SUCH_LINUX_ARTIFACT_STATE_LOADED:-0}" == 1 ]]; then return 0 2>/dev/null || exit 0; fi
readonly SUCH_LINUX_ARTIFACT_STATE_LOADED=1
set -euo pipefail
# shellcheck source=linux_build_common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/linux_build_common.sh"

readonly SUCH_LINUX_VERIFIED_MANIFEST="$SUCH_LINUX_STATE_DIR/verified.json"
readonly SUCH_LINUX_STAGED_MANIFEST="$SUCH_LINUX_STATE_DIR/staged.json"

such_linux_clear_artifact_state() {
  rm -f "$SUCH_LINUX_VERIFIED_MANIFEST" "$SUCH_LINUX_STAGED_MANIFEST"
}

such_linux_sha256() {
  local path="$1"
  [[ -f "$path" ]] || { printf 'Artifact is missing: %s\n' "$path" >&2; return 7; }
  sha256sum "$path" | awk '{print $1}'
}

such_linux_json_field() {
  local file="$1" field="$2"
  python3 - "$file" "$field" <<'PY'
import json,sys
with open(sys.argv[1],encoding='utf-8') as f: obj=json.load(f)
v=obj.get(sys.argv[2], '')
if isinstance(v,bool): print('1' if v else '0')
else: print(v)
PY
}

such_linux_write_verified_manifest() {
  local gui="$1" cli="$2" stub="$3" runtime_path="${4:-}" runtime_required="${5:-0}"
  mkdir -p "$SUCH_LINUX_STATE_DIR"
  python3 - "$SUCH_LINUX_VERIFIED_MANIFEST" "$SUCH_VERSION" "$SUCH_LINUX_ROOT_DIR" "$SUCH_LINUX_BUILD_DIR" \
    "$gui" "$(such_linux_sha256 "$gui")" "$cli" "$(such_linux_sha256 "$cli")" \
    "$stub" "$(such_linux_sha256 "$stub")" "$runtime_path" \
    "$([[ -n "$runtime_path" ]] && such_linux_sha256 "$runtime_path" || true)" "$runtime_required" <<'PY'
import json,sys,datetime
(out,ver,root,build,gui,gsha,cli,csha,stub,ssha,runtime,rsha,required)=sys.argv[1:]
obj={
 'schema':1,'version':ver,'source_root':root,'build_dir':build,
 'gui_path':gui,'gui_sha256':gsha,'cli_path':cli,'cli_sha256':csha,
 'stub_path':stub,'stub_sha256':ssha,'runtime_path':runtime,'runtime_sha256':rsha,
 'runtime_required':required=='1','verified_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()
}
with open(out,'w',encoding='utf-8') as f: json.dump(obj,f,indent=2,sort_keys=True)
PY
  rm -f "$SUCH_LINUX_STAGED_MANIFEST"
  printf 'Linux verification manifest: %s\n' "$SUCH_LINUX_VERIFIED_MANIFEST"
}

such_linux_validate_verified_manifest() {
  [[ -f "$SUCH_LINUX_VERIFIED_MANIFEST" ]] || { printf 'Verified Linux artifact manifest is missing. Run verify_linux.sh first.\n' >&2; return 7; }
  local version root gui cli stub runtime
  version="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" version)"
  root="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" source_root)"
  gui="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" gui_path)"
  cli="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" cli_path)"
  stub="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" stub_path)"
  runtime="$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" runtime_path)"
  [[ "$version" == "$SUCH_VERSION" ]] || { printf 'Verified Linux manifest version mismatch: %s != %s\n' "$version" "$SUCH_VERSION" >&2; return 7; }
  [[ "$root" == "$SUCH_LINUX_ROOT_DIR" ]] || { printf 'Verified Linux manifest source-root mismatch.\n' >&2; return 7; }
  [[ "$(such_linux_sha256 "$gui")" == "$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" gui_sha256)" ]] || { printf 'Verified Linux GUI changed after test.\n' >&2; return 7; }
  [[ "$(such_linux_sha256 "$cli")" == "$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" cli_sha256)" ]] || { printf 'Verified Linux CLI changed after test.\n' >&2; return 7; }
  [[ "$(such_linux_sha256 "$stub")" == "$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" stub_sha256)" ]] || { printf 'Verified Linux runtime stub changed after test.\n' >&2; return 7; }
  if [[ -n "$runtime" ]]; then
    [[ "$(such_linux_sha256 "$runtime")" == "$(such_linux_json_field "$SUCH_LINUX_VERIFIED_MANIFEST" runtime_sha256)" ]] || { printf 'Verified Linux production runtime changed after test.\n' >&2; return 7; }
  fi
}

such_linux_write_staged_manifest() {
  local gui="$1" cli="$2" runtime="${3:-}"
  mkdir -p "$SUCH_LINUX_STATE_DIR"
  local verified_sha
  verified_sha="$(such_linux_sha256 "$SUCH_LINUX_VERIFIED_MANIFEST")"
  python3 - "$SUCH_LINUX_STAGED_MANIFEST" "$SUCH_VERSION" "$SUCH_LINUX_ROOT_DIR" "$SUCH_LINUX_INSTALL_DIR" \
    "$verified_sha" "$gui" "$(such_linux_sha256 "$gui")" "$cli" "$(such_linux_sha256 "$cli")" \
    "$runtime" "$([[ -n "$runtime" ]] && such_linux_sha256 "$runtime" || true)" <<'PY'
import json,sys,datetime
(out,ver,root,stage,vsha,gui,gsha,cli,csha,runtime,rsha)=sys.argv[1:]
obj={'schema':1,'version':ver,'source_root':root,'stage_dir':stage,'verification_manifest_sha256':vsha,
     'gui_path':gui,'gui_sha256':gsha,'cli_path':cli,'cli_sha256':csha,
     'runtime_path':runtime,'runtime_sha256':rsha,'staged_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
with open(out,'w',encoding='utf-8') as f: json.dump(obj,f,indent=2,sort_keys=True)
PY
  printf 'Linux stage manifest: %s\n' "$SUCH_LINUX_STAGED_MANIFEST"
}

such_linux_validate_staged_manifest() {
  such_linux_validate_verified_manifest
  [[ -f "$SUCH_LINUX_STAGED_MANIFEST" ]] || { printf 'Staged Linux artifact manifest is missing.\n' >&2; return 7; }
  local version root vsha gui cli runtime
  version="$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" version)"
  root="$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" source_root)"
  vsha="$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" verification_manifest_sha256)"
  gui="$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" gui_path)"
  cli="$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" cli_path)"
  runtime="$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" runtime_path)"
  [[ "$version" == "$SUCH_VERSION" && "$root" == "$SUCH_LINUX_ROOT_DIR" ]] || { printf 'Staged Linux manifest identity mismatch.\n' >&2; return 7; }
  [[ "$(such_linux_sha256 "$SUCH_LINUX_VERIFIED_MANIFEST")" == "$vsha" ]] || { printf 'Linux stage belongs to a different verification manifest.\n' >&2; return 7; }
  [[ "$(such_linux_sha256 "$gui")" == "$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" gui_sha256)" ]] || { printf 'Staged Linux GUI changed after staging.\n' >&2; return 7; }
  [[ "$(such_linux_sha256 "$cli")" == "$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" cli_sha256)" ]] || { printf 'Staged Linux CLI changed after staging.\n' >&2; return 7; }
  if [[ -n "$runtime" ]]; then
    [[ "$(such_linux_sha256 "$runtime")" == "$(such_linux_json_field "$SUCH_LINUX_STAGED_MANIFEST" runtime_sha256)" ]] || { printf 'Staged Linux runtime changed after staging.\n' >&2; return 7; }
  fi
}

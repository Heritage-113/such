# Such Script Architecture

Status: Authoritative for the v1.3.0 script line and later maintenance unless explicitly replaced.

## 1. Purpose

The script layer is a build/release control plane. It must remain simpler than the product it builds.

The previous script line repeatedly failed because responsibilities drifted across entry scripts: install rebuilt the product, stage invoked CMake, verify implicitly depended on staging, generated state lived under long source paths, version strings were duplicated, PowerShell hosts differed between wrappers, and platform audits interpreted another platform's dependencies as source leaks.

This document fixes ownership so future patches change one layer instead of adding another local workaround.

## 2. Single version authority

`VERSION` at repository root is the script and product release authority.

- CMake reads `VERSION` before `project()`.
- Windows workspace, artifact manifests, installer metadata and release names read `VERSION`.
- Linux workspace, runtime-manifest name, DEB version and release names read `VERSION`.
- iPadOS workspace and bundle verification read `VERSION`.

A script must not introduce a literal active release number when the value can be read from `VERSION`.

## 3. Layer model

Every platform uses the same four script layers.

### Layer A — public workflow entry

User-facing commands such as `build_windows.cmd`, `verify_linux.sh`, or `qualify_ipados.sh`.

Responsibilities:

- parse public options;
- call orchestration functions in a documented order;
- convert the final failure into one process exit code;
- print the workflow result.

Public entries do not contain compiler commands, packaging implementation, filesystem crawling, registry implementation, or artifact hashing logic.

### Layer B — platform pipeline/orchestration library

- `windows_pipeline.ps1`
- `linux_pipeline.sh`
- `ipados_pipeline.sh`

Responsibilities:

- compose atomic steps;
- provide consistent BEGIN/PASS markers;
- centralize the canonical verified-build sequence;
- avoid duplicating step order across public entries.

An orchestration library may call atomic steps. An atomic step may never call an orchestration library or sibling step.

### Layer C — atomic step

Examples:

- `windows_step_build.ps1`
- `linux_step_test.sh`
- `ipados_step_stage.sh`

An atomic step has exactly one owner responsibility and assumes its prerequisite has already been satisfied.

Atomic steps fail upward. They do not launch another public workflow and do not translate errors into nested generic FATAL messages.

### Layer D — common/platform utility

Examples:

- path/version discovery;
- native-process invocation;
- dependency probes;
- artifact-state serialization;
- runtime SHA validation.

Utilities do not orchestrate workflows.

## 4. Canonical responsibilities

| Step | Owns | Must not own |
|---|---|---|
| audit | repository/source contract | dependency installation, build |
| bootstrap | dependency preparation only | configure, compile, test |
| build | configure + compile only | CTest, staging, install, release |
| test | test/smoke + verified manifest | compile, user install, package |
| stage | copy exact verified artifacts + staged manifest | compile, test, package manager |
| install | mutate the user installation from staged artifacts | build/test/toolchain setup |
| release | package staged artifacts | build/test/user installation |
| deploy-test | test deployment behavior using staged artifacts | rebuild product |

## 5. Artifact state and trust

Build output is not trusted merely because a file exists.

The pipeline uses attestations:

1. `build` invalidates previous verification/stage state.
2. `test` hashes exact output files and writes a verified manifest.
3. `stage` reads that manifest, copies only those files, re-hashes them, and writes a staged manifest.
4. `install` and `release` consume only a valid staged manifest.

Changing a verified binary invalidates the downstream handoff.

Windows stores `verified.json` / `staged.json` in its repository-local `build/` workspace. Linux uses the same manifest model in its platform build workspace. iPadOS records a verified app-tree hash before staging.

## 6. Generated-state policy

Generated build/stage/test state must not be placed under the source checkout.

### Windows

Default generated roots:

- work: `%USERPROFILE%\SW`
- toolchain: `%USERPROFILE%\STC`
- VS offline layout: `%USERPROFILE%\STC\offline\VS17`

The source may be reached through a short junction when required by MSBuild/FileTracker.

Windows script-host state is also external. The pinned PowerShell 7.6.6 LTS portable host is materialized under `%USERPROFILE%\STC\powershell` (or `SUCH_TOOLCHAIN_ROOT`) after SHA-256 verification. A valid local/bundled/offline archive is reused before network access.

### Linux

Default work root:

`$XDG_CACHE_HOME/Heritage/SuchBuild` or `$HOME/.cache/Heritage/SuchBuild`

Each checkout gets a version + source-hash workspace.

### iPadOS

Default work root:

`~/Library/Caches/Heritage/SuchBuild`

Xcode generated data therefore does not expand the source archive path.

`dist/artifacts` is an intentional publish output and is not build state.

## 7. Windows-specific invariants

- PowerShell 7.6.6 LTS is the pinned default host for Windows Such scripts.
- `.cmd` launchers call `windows_powershell_host.cmd`; host provisioning is a Layer-D utility and is not duplicated per workflow.
- The in-box Windows PowerShell 5.1 process is only a first-run bootstrap bridge used to SHA-verify/materialize the pinned portable PowerShell 7 host. It does not execute the Such build/install/release workflow.
- `windows_powershell_host.ps1` alone must remain Windows PowerShell 5.1-compatible so a clean Windows machine can provision the default host without an administrator install.
- Do not infer success from a stale `$LASTEXITCODE` after invoking a PowerShell script.
- Native commands must be checked at the point they are executed.
- MSVC `/W4 /WX` remains authoritative.
- Build/stage paths must remain short and outside the source checkout.
- install is current-user only except the explicitly isolated legacy-machine uninstall cleanup path.
- stage is toolchain-free.
- install and release consume staged attestations; neither may invoke CMake/MSBuild/CTest.

## 8. Linux-specific invariants

- Dependency installation belongs only to bootstrap.
- `linux_step_build.sh` performs configure/build only.
- runtime smoke and product regression belong to test.
- staging manually copies the exact verified GUI/CLI/runtime instead of running `cmake --install` after verification.
- per-user payload installation is isolated in `linux_payload_installer.sh`.
- source `install_linux.sh` is an orchestration workflow; release archives rename the atomic payload installer to `install_linux.sh` for end users.
- release and DEB packaging consume staged manifests and never rebuild.
- Samba/CIFS and permission regression remain test responsibilities, not installer logic.

## 9. iPadOS-specific invariants

- iPadOS requires macOS/Xcode and fails closed elsewhere.
- bootstrap checks Xcode/CMake/SDK availability only.
- build configures/builds `such_ipados` only.
- test checks `Such.app`, executable presence, bundle version and a deterministic app-tree hash.
- stage copies only the verified app tree and rechecks its hash.
- no fake desktop-style install step is introduced; App Store/device deployment is outside this public build-script boundary.

## 10. Error model

One error should identify one owner.

Public workflows print step markers:

```text
[SUCH:BUILD] BEGIN
...
[SUCH:BUILD] PASS
[SUCH:TEST] BEGIN
...
[SUCH:FAIL][verify] ...
```

Atomic steps throw/return non-zero and do not wrap the failure in another generic FATAL layer.

A failure message should contain the failing prerequisite or artifact path whenever possible.

## 11. Prohibited patterns

The repository audit must reject these regressions:

- atomic step invoking a sibling atomic step;
- install/release invoking compiler, CMake, MSBuild or CTest;
- build invoking tests or installation;
- duplicate active version literals instead of reading `VERSION`;
- generated build state under the source tree;
- public workflow order duplicated independently when a platform pipeline helper exists;
- platform bootstrap logic embedded in product CMake targets;
- workflow-specific PowerShell host selection that bypasses `windows_powershell_host.cmd`;
- Linux release/DEB scripts rebuilding the product;
- iPadOS verify being only an alias for build.

## 12. Canonical workflows

### Windows

```text
build   = audit -> bootstrap -> build
verify  = audit -> bootstrap -> build -> test
install = audit -> bootstrap -> build -> test -> stage -> install
release = audit -> bootstrap -> build -> test -> stage -> release
qualify = audit -> bootstrap -> build -> test
```

The ordering is implemented through `windows_pipeline.ps1`, not duplicated as direct script calls in every entry.

### Linux

```text
build   = bootstrap(build) -> audit -> build
verify  = bootstrap(verify) -> audit -> build -> test
qualify = bootstrap(verify) -> audit -> build -> test -> stage -> deploy-test
install = bootstrap(verify) -> audit -> build -> test -> stage -> install
release = bootstrap(release) -> audit -> build -> test -> stage -> deploy-test -> release
DEB     = bootstrap(deb) -> audit -> build -> test -> stage -> deb
```

### iPadOS

```text
build   = bootstrap -> build
verify  = bootstrap -> audit -> build -> test
qualify = bootstrap -> audit -> build -> test -> stage
```

## 13. Qualification labels

A script change is not complete merely because it parses.

- **Verified**: executed on the native platform and passed.
- **Cross-platform structural verified**: repository/audit/syntax contract passed elsewhere.
- **Platform-unverified**: native platform execution was unavailable.

Linux results must not be reported as Windows/iPadOS native PASS.


## Windows-specific orchestration rule

The stage names are shared across platforms; the orchestration policy is not required to be identical.

Windows has a heavyweight MSVC/CMake bootstrap and per-user installation path. Therefore `install_windows` and `release_windows` are artifact-first entry points:

1. Reuse a valid staged manifest without invoking CMake/MSVC.
2. If only a valid verification manifest exists, run the toolchain-free stage step.
3. Only when neither attestation is reusable (or `-Clean` / a new runtime path explicitly invalidates it) run audit -> bootstrap -> build -> test.
4. Then install or release.

Do not replace this with an unconditional `build -> test` chain merely to make the Windows script graph look like Linux or iPadOS. Cross-platform consistency applies to responsibility boundaries, not to platform-specific orchestration cost.

Windows PowerShell 5.1 native-process exit status must be captured at the call boundary. Shared Windows scripts must not make correctness depend on a later read of process-global `$LASTEXITCODE`.

# Contributing to Such

Thank you for your interest in Such.

This repository is the public source tree for the Such frontend, CLI, public build tooling, and stable runtime client boundary.

## License for contributions

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in this repository is provided under the Apache License, Version 2.0, consistent with Section 5 of that license.

By submitting a contribution, you represent that you have the right to submit it and that it does not knowingly include material you are not permitted to contribute.

## In scope

Contributions are welcome for public-tree components such as:

- Windows and Linux frontend behavior
- CLI behavior and documentation
- accessibility and native-platform integration
- public build and packaging scripts
- public runtime-client boundary fixes
- tests for public contracts
- documentation

## Out of scope / private components

The following are maintained separately by Heritage Inc. and are not part of the public Apache-2.0 source grant:

- production search/storage runtime implementation
- private backend source and private dependencies
- Such SDK implementation and SDK distribution packages
- internal benchmark infrastructure and private validation datasets
- signing material, credentials, private symbols, deployment secrets, and private build machinery

Do not submit reverse-engineered copies of private components or material obtained under separate confidential, commercial, or evaluation terms.

## Pull requests

Keep changes narrowly scoped and explain the user-visible or architectural reason for the change. Runtime-facing changes must preserve the documented ABI unless an explicit ABI-version migration is part of the change.

Before opening a pull request, run the relevant platform validation where possible.

Windows:

```bat
scripts\doctor_windows.cmd
scripts\build_windows.cmd -Clean
scripts\verify_windows.cmd -Clean
```

Linux:

```bash
./scripts/build_linux.sh --clean
./scripts/verify_linux.sh
```

Strict builds treat warnings as errors. Fix source-level warnings rather than globally suppressing them.

## Architecture rule

Fix behavior at the architectural layer that owns it. Avoid platform-local or call-site workarounds that duplicate state, bypass the runtime boundary, or create divergent behavior between frontends.

## Generated and binary material

Do not commit local build directories, caches, debug symbols, credentials, or generated temporary files.

Approved production runtime binaries present in `.runtime/` are release inputs maintained by Heritage Inc. They are not ordinary contribution targets and are not licensed under Apache-2.0 merely because they are stored in this repository.

## Conduct

Keep technical discussion focused on reproducible behavior, code, and documented requirements. Harassment, discrimination, threats, and disclosure of private information are not acceptable in project spaces.

## SDK and commercial integration

The Such SDK is private and is not distributed through this repository. Developers or organizations that need SDK access, native embedding support, private runtime integration, or commercial deployment terms should contact Heritage Inc. via https://heritage-labs.net.

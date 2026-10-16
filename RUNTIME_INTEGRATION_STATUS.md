# Such v1.3.5 Windows runtime integration

The public tree includes hash-pinned Windows and Linux runtime binaries and their legal notices. Private Runtime and TopoDB implementation sources are excluded.

Windows validation: 13 public tests and 6 private runtime tests passed. Production-runtime smoke and Inno Setup 7 installer compilation passed. A 2,400-document cross-process test verified filename search before content indexing finished, writer exclusion, and persisted 8-shard reopening.

Windows supports filename checkpoints during enumeration, document extraction failure isolation, and automatic or manual 1/2/4/8/16-shard indexing. The GUI permits search and file opening while indexing and reports worker diagnostics on failure. Linux runtime behavior is unchanged.

The original full-system-drive worker exit 8 has not been conclusively reproduced. End-user installation and full-drive qualification remain pending. Build directories, caches, installers, and user index data are excluded from Git.

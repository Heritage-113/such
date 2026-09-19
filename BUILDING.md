# Building Such public frontend

The public frontend builds without the proprietary runtime source.

## Linux

```bash
scripts/verify_linux.sh --clean
```

## Windows

```powershell
scripts\verify_windows.cmd -Config Release -Arch x64 -Clean
```

## Runtime for real searches

Place the platform runtime shared library beside the executable, or set `SUCH_RUNTIME_LIBRARY` to its absolute path. Public tests use a purpose-built ABI stub and do not contain the production search implementation.

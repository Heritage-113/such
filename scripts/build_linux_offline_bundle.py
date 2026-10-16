#!/usr/bin/env python3
"""Build the pinned Debian 13 amd64 offline dependency bundle for Such.

This maintainer tool repacks the exact installed Debian package payloads that
satisfy Such's Linux build/verification/package profiles. It does not contact a
network or package repository. Run only on the qualified Debian 13 amd64
reference environment after the required packages are installed.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Dict, Iterable, List, Sequence, Set, Tuple

QUALIFIED_ID = "debian"
QUALIFIED_VERSION_ID = "13"
QUALIFIED_ARCH = "amd64"
ROOT_PACKAGES = (
    "build-essential",
    "cmake",
    "python3",
    "libx11-dev",
    "libpng-dev",
    "binutils",
    "pkgconf",
    "xvfb",
    "xauth",
    "dpkg-dev",
)
CONTROL_SIDE_FILES = (
    "conffiles",
    "config",
    "preinst",
    "postinst",
    "prerm",
    "postrm",
    "templates",
    "triggers",
    "shlibs",
    "symbols",
)


def run(args: Sequence[str], *, text: bool = True) -> str:
    return subprocess.check_output(args, text=text).strip()  # type: ignore[return-value]


def parse_os_release() -> Dict[str, str]:
    values: Dict[str, str] = {}
    for raw in Path("/etc/os-release").read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#") or "=" not in raw:
            continue
        key, value = raw.split("=", 1)
        values[key] = value.strip().strip('"')
    return values


def require_reference_host() -> None:
    release = parse_os_release()
    arch = run(["dpkg", "--print-architecture"])
    if release.get("ID") != QUALIFIED_ID or release.get("VERSION_ID") != QUALIFIED_VERSION_ID:
        raise SystemExit(
            f"offline bundle generation requires Debian {QUALIFIED_VERSION_ID}; "
            f"found ID={release.get('ID', '?')} VERSION_ID={release.get('VERSION_ID', '?')}"
        )
    if arch != QUALIFIED_ARCH:
        raise SystemExit(f"offline bundle generation requires {QUALIFIED_ARCH}; found {arch}")


def installed_packages() -> Dict[str, Tuple[str, str]]:
    """Return package/virtual names -> (installed binary package name, version)."""
    result: Dict[str, Tuple[str, str]] = {}
    raw = run(["dpkg-query", "-W", "-f=${binary:Package}\t${Package}\t${Version}\t${Provides}\t${db:Status-Abbrev}\n"])
    for line in raw.splitlines():
        binary_name, package_name, version, provides, status = (line.split("\t", 4) + [""] * 5)[:5]
        if not status.startswith("ii"):
            continue
        canonical = package_name
        result[canonical] = (binary_name, version)
        result[binary_name] = (binary_name, version)
        for provided in provides.split(","):
            virtual = re.sub(r"\s*\(.*?\)\s*", "", provided).strip()
            if virtual:
                result.setdefault(virtual, (binary_name, version))
    return result


def dependency_alternatives(raw_group: str) -> List[str]:
    names: List[str] = []
    for raw_alt in raw_group.split("|"):
        name = re.sub(r"\s*\([^)]*\)", "", raw_alt).strip()
        name = re.sub(r":(?:any|native)$", "", name)
        if name:
            names.append(name)
    return names


def package_dependencies(package: str) -> List[List[str]]:
    raw = run(["dpkg-query", "-W", "-f=${Pre-Depends}\n${Depends}\n", package])
    groups: List[List[str]] = []
    for line in raw.splitlines():
        if not line.strip():
            continue
        for raw_group in line.split(","):
            alts = dependency_alternatives(raw_group)
            if alts:
                groups.append(alts)
    return groups


def resolve_closure(roots: Iterable[str]) -> List[str]:
    installed = installed_packages()
    resolved: Set[str] = set()
    stack = list(roots)
    while stack:
        requested = stack.pop()
        provider = installed.get(requested)
        if provider is None:
            raise SystemExit(f"required dependency is not installed on reference host: {requested}")
        binary_name = provider[0]
        package_name = run(["dpkg-query", "-W", "-f=${Package}", binary_name])
        if package_name in resolved:
            continue
        resolved.add(package_name)
        for alternatives in package_dependencies(binary_name):
            chosen_binary = None
            for alternative in alternatives:
                provider = installed.get(alternative)
                if provider is not None:
                    chosen_binary = provider[0]
                    break
            if chosen_binary is None:
                raise SystemExit(
                    f"no installed provider satisfies dependency of {package_name}: " + " | ".join(alternatives)
                )
            chosen_package = run(["dpkg-query", "-W", "-f=${Package}", chosen_binary])
            if chosen_package not in resolved:
                stack.append(chosen_package)
    return sorted(resolved)


def control_text(package: str) -> str:
    raw = run(["dpkg-query", "-s", package]) + "\n"
    output: List[str] = []
    skip_continuation = False
    for line in raw.splitlines():
        if line.startswith("Status:"):
            continue
        if line.startswith("Conffiles:"):
            skip_continuation = True
            continue
        if skip_continuation and line.startswith(" "):
            continue
        skip_continuation = False
        if line.startswith("Config-Version:"):
            continue
        output.append(line)
    return "\n".join(output).rstrip() + "\n"


def package_file_list(package: str, output_path: Path) -> None:
    paths = run(["dpkg-query", "-L", package]).splitlines()
    with output_path.open("wb") as handle:
        for path in paths:
            if path in ("/", "/.") or not os.path.lexists(path):
                continue
            encoded = ("." + path).encode("utf-8", "surrogateescape")
            handle.write(encoded + b"\0")


def copy_control_side_files(binary_name: str, debian_dir: Path) -> None:
    info_prefix = Path("/var/lib/dpkg/info") / binary_name
    for suffix in CONTROL_SIDE_FILES:
        source = Path(str(info_prefix) + "." + suffix)
        if source.exists():
            shutil.copy2(source, debian_dir / suffix)


def generate_md5sums(package_root: Path) -> None:
    rows: List[str] = []
    for path in sorted(package_root.rglob("*")):
        if not path.is_file() or "DEBIAN" in path.relative_to(package_root).parts:
            continue
        digest = hashlib.md5(path.read_bytes()).hexdigest()  # Debian md5sums format.
        rows.append(f"{digest}  {path.relative_to(package_root).as_posix()}")
    (package_root / "DEBIAN" / "md5sums").write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")


def safe_filename_component(value: str) -> str:
    return value.replace(":", "%3a").replace("/", "_")


def repack(package: str, output_dir: Path, work_root: Path) -> Path:
    binary_name = run(["dpkg-query", "-W", "-f=${binary:Package}", package])
    version = run(["dpkg-query", "-W", "-f=${Version}", binary_name])
    arch = run(["dpkg-query", "-W", "-f=${Architecture}", binary_name])
    package_dir = work_root / safe_filename_component(binary_name)
    shutil.rmtree(package_dir, ignore_errors=True)
    debian_dir = package_dir / "DEBIAN"
    debian_dir.mkdir(parents=True)
    os.chmod(debian_dir, 0o755)
    (debian_dir / "control").write_text(control_text(binary_name), encoding="utf-8")

    list_path = work_root / (safe_filename_component(binary_name) + ".files")
    package_file_list(binary_name, list_path)
    producer = subprocess.Popen(
        ["tar", "--null", "--no-recursion", "-cpf", "-", "-T", str(list_path)],
        cwd="/",
        stdout=subprocess.PIPE,
    )
    assert producer.stdout is not None
    consumer = subprocess.run(["tar", "-xpf", "-"], cwd=package_dir, stdin=producer.stdout, check=False)
    producer.stdout.close()
    producer_rc = producer.wait()
    if producer_rc != 0 or consumer.returncode != 0:
        raise SystemExit(f"failed to copy installed payload for {binary_name}")

    copy_control_side_files(binary_name, debian_dir)
    os.chmod(debian_dir, 0o755)
    generate_md5sums(package_dir)

    filename = f"{safe_filename_component(package)}_{safe_filename_component(version)}_{arch}.deb"
    output = output_dir / filename
    temporary_output = output.with_suffix(output.suffix + ".tmp")
    temporary_output.unlink(missing_ok=True)
    subprocess.run(
        ["dpkg-deb", "--build", "--root-owner-group", "-Zzstd", "-z3", str(package_dir), str(temporary_output)],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    temporary_output.replace(output)
    return output


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_manifest(bundle_dir: Path, packages: Sequence[str], outputs: Sequence[Path]) -> None:
    package_rows = []
    for package, output in zip(packages, outputs):
        version = run(["dpkg-query", "-W", "-f=${Version}", package])
        arch = run(["dpkg-query", "-W", "-f=${Architecture}", package])
        package_rows.append(f"{package}\t{version}\t{arch}\t{output.name}")
    (bundle_dir / "packages.tsv").write_text(
        "package\tversion\tarchitecture\tfile\n" + "\n".join(package_rows) + "\n",
        encoding="utf-8",
    )
    scan = subprocess.run(
        ["dpkg-scanpackages", "pool", "/dev/null"],
        cwd=bundle_dir,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    packages_index = bundle_dir / "Packages"
    packages_index.write_text(scan.stdout, encoding="utf-8")
    (bundle_dir / "Packages.gz").write_bytes(gzip.compress(scan.stdout.encode("utf-8"), compresslevel=9, mtime=0))

    hashes = [f"{sha256(path)}  pool/{path.name}" for path in outputs]
    extras = [
        bundle_dir / "packages.tsv",
        bundle_dir / "BUNDLE_INFO.txt",
        bundle_dir / "Packages",
        bundle_dir / "Packages.gz",
    ]
    readme = bundle_dir / "OFFLINE_DEBIAN13_AMD64_README.md"
    if readme.is_file():
        extras.append(readme)
    for extra in extras:
        hashes.append(f"{sha256(extra)}  {extra.name}")
    (bundle_dir / "SHA256SUMS").write_text("\n".join(hashes) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()

    require_reference_host()
    output = args.output.resolve()
    if args.clean:
        shutil.rmtree(output, ignore_errors=True)
    pool_dir = output / "pool"
    pool_dir.mkdir(parents=True, exist_ok=True)

    packages = resolve_closure(ROOT_PACKAGES)
    info = (
        "Such Linux offline build dependency bundle\n"
        "qualified_distribution=Debian GNU/Linux 13 (trixie)\n"
        "qualified_architecture=amd64\n"
        "profiles=build,verify,release,deb\n"
        f"root_packages={','.join(ROOT_PACKAGES)}\n"
        f"package_count={len(packages)}\n"
        "generation_mode=repacked from installed reference-host package payloads; no network access\n"
    )
    (output / "BUNDLE_INFO.txt").write_text(info, encoding="utf-8")

    outputs: List[Path] = []
    with tempfile.TemporaryDirectory(prefix="such-offline-repack.") as temp:
        work_root = Path(temp)
        total = len(packages)
        for index, package in enumerate(packages, 1):
            version = run(["dpkg-query", "-W", "-f=${Version}", package])
            arch = run(["dpkg-query", "-W", "-f=${Architecture}", package])
            expected = pool_dir / f"{safe_filename_component(package)}_{safe_filename_component(version)}_{arch}.deb"
            if expected.is_file() and expected.stat().st_size > 0:
                print(f"[{index:03d}/{total:03d}] keep   {package}", flush=True)
                outputs.append(expected)
                continue
            print(f"[{index:03d}/{total:03d}] repack {package}", flush=True)
            outputs.append(repack(package, pool_dir, work_root))
    write_manifest(output, packages, outputs)
    print(f"offline bundle ready: {output}")
    print(f"package count: {len(outputs)}")
    print(f"bundle bytes: {sum(p.stat().st_size for p in outputs)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

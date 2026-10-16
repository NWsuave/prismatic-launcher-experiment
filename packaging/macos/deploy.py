#!/usr/bin/env python3
"""Bundle non-system Mach-O dependencies after macdeployqt, then audit paths.

Only packaging tools are executed; application/mod binaries are never executed here.
"""

import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys


MACHO_MAGIC = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
}


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def system_path(path):
    return os.path.normpath(path).startswith(("/System/Library/", "/usr/lib/"))


def dependencies(binary):
    # A fat binary prints one header per architecture. Deduplicate slice imports.
    return list(dict.fromkeys(
        line.strip().split(" (compatibility version", 1)[0]
        for line in run("otool", "-L", str(binary)).splitlines()
        if " (compatibility version" in line
    ))


def rpaths(binary):
    lines = run("otool", "-l", str(binary)).splitlines()
    result = []
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            for entry in lines[index + 1:index + 4]:
                if entry.strip().startswith("path "):
                    result.append(entry.strip()[5:].rsplit(" (offset", 1)[0])
    return list(dict.fromkeys(result))


def macho_files(bundle):
    for path in sorted(bundle.rglob("*")):
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as stream:
                if stream.read(4) in MACHO_MAGIC:
                    yield path


def expand(value, loader, executable):
    if value.startswith("@loader_path/"):
        return loader.parent / value[len("@loader_path/"):]
    if value.startswith("@executable_path/"):
        return executable.parent / value[len("@executable_path/"):]
    return Path(value)


def resolve_dependency(value, origin, executable, framework_dir, executable_rpaths):
    if value.startswith("@rpath/"):
        suffix = value[len("@rpath/"):]
        candidates = [framework_dir / suffix]
        candidates.extend(expand(path, origin, executable) / suffix
                          for path in rpaths(origin) + executable_rpaths)
    else:
        candidates = [expand(value, origin, executable)]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(f"Cannot resolve {value!r} imported by {origin}")


def validate_symlinks(bundle):
    for path in bundle.rglob("*"):
        if path.is_symlink() and not path.resolve().is_relative_to(bundle):
            raise RuntimeError(f"Bundle symlink escapes app: {path}")


def copy_dependency(source, framework_dir):
    framework = next((parent for parent in source.parents
                      if parent.name.endswith(".framework")), None)
    if framework:
        target_root = framework_dir / framework.name
        if target_root.exists():
            target = target_root / source.relative_to(framework)
            if not target.is_file():
                raise RuntimeError(f"Conflicting incomplete framework: {target_root}")
            return target.resolve()
        shutil.copytree(framework, target_root, symlinks=True)
        return (target_root / source.relative_to(framework)).resolve()
    target = framework_dir / source.name
    if target.exists():
        if hashlib.sha256(target.read_bytes()).digest() != hashlib.sha256(source.read_bytes()).digest():
            raise RuntimeError(f"Dependency filename collision: {source} and {target}")
    else:
        shutil.copy2(source, target)
    return target


def deploy(bundle):
    # Reject pre-existing escapes before any mkdir, chmod, copy, or path rewrite.
    validate_symlinks(bundle)
    executable = bundle / "Contents/MacOS/prismatic-launch"
    if not executable.is_file():
        raise RuntimeError(f"Missing canonical executable: {executable}")
    framework_dir = bundle / "Contents/Frameworks"
    framework_dir.mkdir(parents=True, exist_ok=True)
    executable_rpaths = rpaths(executable)
    pending = [(path, path) for path in macho_files(bundle)]
    processed = set()
    copied = {}
    while pending:
        binary, origin = pending.pop(0)
        binary = binary.resolve()
        if binary in processed:
            continue
        processed.add(binary)
        binary.chmod(binary.stat().st_mode | 0o200)
        imports = dependencies(binary)
        # dylibs list their own LC_ID_DYLIB first; do not treat it as an import.
        identifiers = run("otool", "-D", str(binary)).splitlines()[1:]
        identifiers = {line.strip() for line in identifiers if not line.endswith(":")}
        for dependency in imports:
            if dependency in identifiers or system_path(dependency):
                continue
            source = resolve_dependency(dependency, origin, executable, framework_dir, executable_rpaths)
            if source.is_relative_to(bundle):
                target = source
            elif source in copied:
                target = copied[source]
            else:
                target = copy_dependency(source, framework_dir)
                # A newly copied framework may itself contain SDK symlinks.
                validate_symlinks(bundle)
                copied[source] = target
                pending.append((target, source))
                # A copied framework can include helpers with their own dependencies.
                pending.extend((path, path) for path in macho_files(bundle) if path not in processed)
            relative = os.path.relpath(target, binary.parent)
            run("install_name_tool", "-change", dependency, f"@loader_path/{relative}", str(binary))
        if identifiers:
            relative = os.path.relpath(binary, framework_dir)
            run("install_name_tool", "-id", f"@rpath/{relative}", str(binary))
        # Every import is now relative; remove build-machine paths from LC_RPATH.
        for path in rpaths(binary):
            if path.startswith("/"):
                run("install_name_tool", "-delete_rpath", path, str(binary))
    validate_symlinks(bundle)
    audit(bundle)
    print(f"Validated {len(processed)} Mach-O files; bundled {len(copied)} additional dependencies.")


def audit(bundle):
    validate_symlinks(bundle)
    executable = bundle / "Contents/MacOS/prismatic-launch"
    if not executable.is_file():
        raise RuntimeError(f"Missing canonical executable: {executable}")
    for binary in macho_files(bundle):
        identifiers = {line.strip() for line in run("otool", "-D", str(binary)).splitlines()[1:]
                       if not line.endswith(":")}
        for dependency in dependencies(binary):
            if dependency in identifiers or system_path(dependency):
                continue
            if not dependency.startswith(("@loader_path/", "@executable_path/")):
                raise RuntimeError(f"External/unresolved import {dependency!r} in {binary}")
            target = expand(dependency, binary, executable).resolve()
            if not target.is_relative_to(bundle) or not target.is_file():
                raise RuntimeError(f"Missing or escaping import {dependency!r} in {binary}")
        for path in rpaths(binary):
            if path.startswith("/"):
                raise RuntimeError(f"Absolute runtime search path {path!r} in {binary}")


def sign(bundle, mode):
    binaries = list(macho_files(bundle))
    for binary in binaries:
        signed = subprocess.run(["codesign", "-dv", str(binary)], capture_output=True).returncode == 0
        if signed:
            run("codesign", "--remove-signature", str(binary))
    if mode == "unsigned":
        # Remove retained resource seals from both frameworks and the outer app.
        for seal in sorted(bundle.rglob("_CodeSignature"), reverse=True):
            shutil.rmtree(seal)
        return
    for binary in binaries:
        run("codesign", "--force", "--sign", "-", "--timestamp=none", str(binary))
    frameworks = sorted(bundle.rglob("*.framework"), key=lambda path: len(path.parts), reverse=True)
    for framework in frameworks:
        run("codesign", "--force", "--sign", "-", "--timestamp=none", str(framework))
    run("codesign", "--force", "--sign", "-", "--timestamp=none", str(bundle))
    run("codesign", "--verify", "--deep", "--strict", str(bundle))


def minimum_macos_version(binary):
    versions = []
    field = None
    for raw_line in run("otool", "-l", str(binary)).splitlines():
        line = raw_line.strip()
        if line.startswith("cmd "):
            field = {"cmd LC_BUILD_VERSION": "minos ",
                     "cmd LC_VERSION_MIN_MACOSX": "version "}.get(line)
        elif field and line.startswith(field):
            version = tuple(int(part) for part in line[len(field):].split()[0].split("."))
            versions.append(version + (0,) * (3 - len(version)))
    return max(versions, default=(0, 0, 0))


def update_minimum_os(bundle):
    # Homebrew dependencies can require a newer OS than the app's compiler flag.
    # Advertise the maximum of every bundled slice's actual Mach-O requirement.
    minimum = max((minimum_macos_version(binary) for binary in macho_files(bundle)),
                  default=(0, 0, 0))
    if minimum == (0, 0, 0):
        raise RuntimeError("Cannot determine the bundled macOS minimum version")
    version = ".".join(str(part) for part in minimum)
    info_path = bundle / "Contents/Info.plist"
    with info_path.open("rb") as stream:
        info = plistlib.load(stream)
    info["LSMinimumSystemVersion"] = version
    with info_path.open("wb") as stream:
        plistlib.dump(info, stream)
    print(f"Bundled Mach-O minimum macOS version: {version}; clean-host compatibility still requires testing.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--audit-only", action="store_true")
    parser.add_argument("--sign", choices=("adhoc", "unsigned"))
    args = parser.parse_args()
    try:
        bundle = args.bundle.resolve(strict=True)
        (audit if args.audit_only else deploy)(bundle)
        if not args.audit_only:
            update_minimum_os(bundle)
        if args.sign:
            sign(bundle, args.sign)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"macOS deployment failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

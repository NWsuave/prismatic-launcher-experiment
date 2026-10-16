#!/usr/bin/env python3
"""Deploy the trusted, locally built application and SDK libraries into an AppDir."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


# glibc and host graphics drivers must match the target system. Everything else,
# including Qt, libarchive, libstdc++ and their transitive dependencies, is bundled.
HOST_LIBRARIES = re.compile(
    r"^(?:ld-linux-x86-64\.so\.2|lib(?:c|m|pthread|dl|rt|resolv|util|anl)\.so\.[0-9]+"
    r"|libnss_[^.]+\.so\.[0-9]+|lib(?:GL|GLX|GLdispatch|EGL|OpenGL)\.so\.[0-9]+)$"
)
PLUGIN_GROUPS = (
    "platforms", "imageformats", "iconengines", "styles", "platformthemes",
    "xcbglintegrations", "wayland-decoration-client", "wayland-graphics-integration-client",
    "wayland-shell-integration", "tls", "networkinformation",
)


def run(*args, env=None):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT, env=env, timeout=30)


def dependencies(binary, library_dir=None):
    env = os.environ.copy()
    if library_dir:
        env["LD_LIBRARY_PATH"] = str(library_dir)
    output = run("ldd", str(binary), env=env)
    result = []
    for line in output.splitlines():
        if "=> not found" in line:
            raise RuntimeError(f"Unresolved dependency in {binary}: {line.strip()}")
        match = re.match(r"\s*(\S+)\s+=>\s+(.+?)\s+\(0x[0-9a-f]+\)", line)
        if match:
            result.append((match[1], Path(match[2])))
    return result


def verify(appdir):
    library_dir = appdir / "usr/lib"
    binaries = [appdir / "usr/bin/prismatic-launch"]
    binaries += sorted((appdir / "usr/plugins").rglob("*.so"))
    binaries += sorted(library_dir.glob("*.so*"))
    host = set()
    for binary in binaries:
        for name, path in dependencies(binary, library_dir):
            if HOST_LIBRARIES.fullmatch(name):
                host.add(name)
            elif not path.resolve().is_relative_to(appdir.resolve()):
                raise RuntimeError(f"Unbundled dependency in {binary}: {name} at {path}")
    for required in ("libQt6Core.so.6", "libQt6Gui.so.6", "libQt6Widgets.so.6", "libarchive.so.13"):
        if not (library_dir / required).is_file():
            raise RuntimeError(f"Required runtime was not deployed: {required}")
    for required in ("libqxcb.so", "libqoffscreen.so"):
        if not (appdir / "usr/plugins/platforms" / required).is_file():
            raise RuntimeError(f"Required Qt platform plugin was not deployed: {required}")
    return sorted(host)


def deploy(appdir, plugins, resources, source_binary=None, qt_libraries=None):
    binary = appdir / "usr/bin/prismatic-launch"
    if "Advanced Micro Devices X86-64" not in run("readelf", "-h", str(binary)):
        raise RuntimeError("Only an x86_64 ELF application can be packaged by this script")
    library_dir = appdir / "usr/lib"
    library_dir.mkdir(parents=True, exist_ok=True)
    for group in PLUGIN_GROUPS:
        source = plugins / group
        if source.is_dir():
            shutil.copytree(source, appdir / "usr/plugins" / group, symlinks=False)
    shutil.copy2(resources / "AppRun", appdir / "AppRun")
    (appdir / "AppRun").chmod(0o755)
    shutil.copy2(resources / "qt.conf", appdir / "usr/bin/qt.conf")
    shutil.copy2(resources / "org.prismatic.launcher.desktop", appdir / "org.prismatic.launcher.desktop")
    shutil.copy2(resources / "org.prismatic.launcher.svg", appdir / "org.prismatic.launcher.svg")
    (appdir / ".DirIcon").symlink_to("org.prismatic.launcher.svg")
    queue = [source_binary or binary] + sorted((appdir / "usr/plugins").rglob("*.so"))
    records = {}
    while queue:
        current = queue.pop()
        for name, source in dependencies(current, qt_libraries):
            if HOST_LIBRARIES.fullmatch(name):
                continue
            resolved = source.resolve(strict=True)
            if name in records:
                if str(resolved) != records[name]["source"]:
                    digest = hashlib.sha256(resolved.read_bytes()).hexdigest()
                    if digest != records[name]["sha256"]:
                        raise RuntimeError(f"Conflicting SDK libraries have the same SONAME: {name}")
                continue
            destination = library_dir / name
            shutil.copy2(resolved, destination)
            records[name] = {
                "source": str(resolved),
                "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            }
            queue.append(resolved)
    host = verify(appdir)
    metadata = appdir / "usr/share/prismatic-launcher"
    metadata.mkdir(parents=True, exist_ok=True)
    project_license = resources.parents[1] / "LICENSE"
    if project_license.is_file():
        shutil.copy2(project_license, metadata / "LICENSE")
    # Preserve installed SDK copyright/license notices on Debian/Ubuntu builders.
    license_dir = metadata / "licenses"
    license_dir.mkdir(exist_ok=True)
    if shutil.which("dpkg-query"):
        sources = {entry["source"] for entry in records.values()}
        sources.update(str(path) for path in plugins.rglob("*.so"))
        packages = subprocess.run(
            ["dpkg-query", "-S", *sorted(sources)], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False, timeout=60,
        ).stdout.splitlines()
        for package in packages:
            name = package.split(": ", 1)[0].split(":", 1)[0]
            notice = Path("/usr/share/doc") / name / "copyright"
            if notice.is_file():
                shutil.copy2(notice, license_dir / f"{name}.copyright")
        common_licenses = Path("/usr/share/common-licenses")
        if common_licenses.is_dir():
            shutil.copytree(common_licenses, license_dir / "common", symlinks=False)
    (metadata / "runtime-dependencies.json").write_text(
        json.dumps({"bundled": records, "required_from_host": host}, indent=2) + "\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appdir", type=Path)
    parser.add_argument("--plugins", type=Path)
    parser.add_argument("--qt-libraries", type=Path)
    parser.add_argument("--source-binary", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    if args.verify:
        print(json.dumps({"required_from_host": verify(args.appdir)}, indent=2))
    elif args.plugins:
        deploy(args.appdir, args.plugins, Path(__file__).resolve().parent,
               args.source_binary, args.qt_libraries)
    else:
        parser.error("--plugins is required for deployment")


if __name__ == "__main__":
    main()

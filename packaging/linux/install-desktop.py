#!/usr/bin/env python3
"""Explicitly register an existing AppImage in the current user's application menu."""

import argparse
import os
from pathlib import Path
import shutil


def install(appimage, data_home):
    appimage = appimage.resolve(strict=True)
    if not appimage.is_file() or not os.access(appimage, os.X_OK):
        raise ValueError("Select an existing executable AppImage")
    if any(character in str(appimage) for character in "\n\r\0"):
        raise ValueError("AppImage paths cannot contain line breaks")
    escaped = str(appimage).replace("%", "%%")
    for character in ("\\", '"', "`", "$"):
        escaped = escaped.replace(character, "\\" + character)
    # Desktop Entry strings escape backslashes in addition to Exec argument quoting.
    escaped = escaped.replace("\\", "\\\\")
    desktop_dir = data_home / "applications"
    icon_dir = data_home / "icons/hicolor/scalable/apps"
    desktop_dir.mkdir(parents=True, exist_ok=True)
    icon_dir.mkdir(parents=True, exist_ok=True)
    resources = Path(__file__).resolve().parent
    contents = (resources / "org.prismatic.launcher.desktop").read_text()
    contents = contents.replace("Exec=prismatic-launch", f'Exec="{escaped}"')
    destination = desktop_dir / "org.prismatic.launcher.desktop"
    destination.write_text(contents)
    shutil.copy2(resources / "org.prismatic.launcher.svg", icon_dir / "org.prismatic.launcher.svg")
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appimage", type=Path)
    parser.add_argument("--data-home", type=Path, default=Path(
        os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))
    ))
    args = parser.parse_args()
    print(install(args.appimage, args.data_home))


if __name__ == "__main__":
    main()

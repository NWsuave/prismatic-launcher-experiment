#!/usr/bin/env python3
"""Offline packaging policy tests; all paths and loader output are disposable fixtures."""

from pathlib import Path
import importlib.util
import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import deploy


class LinuxDeploymentTests(unittest.TestCase):
    def test_missing_appimage_tool_fails_before_build(self):
        env = os.environ.copy()
        env.pop("APPIMAGETOOL", None)
        env.pop("APPIMAGETOOL_SHA256", None)
        script = Path(__file__).resolve().parents[2] / "scripts/package-linux.sh"
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "unused-build"
            result = subprocess.run(
                [str(script), "--build-dir", str(build)], text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Set APPIMAGETOOL", result.stdout)
            self.assertFalse(build.exists())

    def test_tool_checksum_mismatch_fails_before_build(self):
        env = os.environ.copy()
        env["APPIMAGETOOL"] = shutil.which("true")
        env["APPIMAGETOOL_SHA256"] = "0" * 64
        script = Path(__file__).resolve().parents[2] / "scripts/package-linux.sh"
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "unused-build"
            result = subprocess.run(
                [str(script), "--build-dir", str(build)], text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("SHA-256 mismatch", result.stdout)
            self.assertFalse(build.exists())

    def test_appimage_runtime_is_explicitly_required(self):
        env = os.environ.copy()
        tool = Path(shutil.which("true"))
        env["APPIMAGETOOL"] = str(tool)
        env["APPIMAGETOOL_SHA256"] = hashlib.sha256(tool.read_bytes()).hexdigest()
        env.pop("APPIMAGE_RUNTIME", None)
        script = Path(__file__).resolve().parents[2] / "scripts/package-linux.sh"
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "unused-build"
            result = subprocess.run(
                [str(script), "--build-dir", str(build)], text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Set APPIMAGE_RUNTIME", result.stdout)
            self.assertFalse(build.exists())

    def test_conflicting_library_sonames_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            appdir = root / "AppDir"
            (appdir / "usr/bin").mkdir(parents=True)
            (appdir / "usr/bin/prismatic-launch").write_bytes(b"trusted test fixture")
            first = root / "first.so"
            second = root / "second.so"
            first.write_bytes(b"first SDK runtime")
            second.write_bytes(b"different SDK runtime")
            with patch.object(deploy, "run", return_value="Advanced Micro Devices X86-64"):
                with patch.object(deploy, "dependencies", side_effect=[
                    [("libfixture.so.1", first)], [("libfixture.so.1", second)]
                ]):
                    with self.assertRaisesRegex(RuntimeError, "Conflicting SDK libraries"):
                        deploy.deploy(appdir, root / "empty-plugins", Path(__file__).parent)

    def test_relocated_apprun_preserves_external_environment(self):
        with tempfile.TemporaryDirectory(prefix="prismatic bundle ") as temporary:
            appdir = Path(temporary)
            (appdir / "usr/bin").mkdir(parents=True)
            (appdir / "usr/bin/prismatic-launch").symlink_to(shutil.which("env"))
            shutil.copy2(Path(__file__).parent / "AppRun", appdir / "AppRun")
            (appdir / "AppRun").chmod(0o755)
            env = os.environ.copy()
            env["LD_LIBRARY_PATH"] = "/fixture/original-library"
            env["QT_PLUGIN_PATH"] = "/fixture/original-plugins"
            output = subprocess.check_output([str(appdir / "AppRun")], text=True, env=env)
            values = dict(line.split("=", 1) for line in output.splitlines() if "=" in line)
            self.assertEqual(values["PRISMATIC_BUNDLED_ENV"], "1")
            self.assertEqual(values["PRISMATIC_ORIGINAL_LD_LIBRARY_PATH"], "/fixture/original-library")
            self.assertEqual(values["PRISMATIC_ORIGINAL_QT_PLUGIN_PATH"], "/fixture/original-plugins")
            self.assertEqual(values["QT_PLUGIN_PATH"], str(appdir / "usr/plugins"))
            self.assertTrue(values["LD_LIBRARY_PATH"].startswith(str(appdir / "usr/lib") + ":"))

    def test_desktop_integration_handles_spaces_and_field_codes(self):
        spec = importlib.util.spec_from_file_location("install_desktop", Path(__file__).parent / "install-desktop.py")
        desktop = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(desktop)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            appimage = root / "Launcher with % spaces.AppImage"
            appimage.write_bytes(b"local fixture; never executed")
            appimage.chmod(0o755)
            destination = desktop.install(appimage, root / "fixture-data")
            self.assertIn('Launcher with %% spaces.AppImage"', destination.read_text())
            subprocess.run(["desktop-file-validate", str(destination)], check=True)
            self.assertTrue((root / "fixture-data/icons/hicolor/scalable/apps/org.prismatic.launcher.svg").is_file())

    def test_missing_runtime_dependency_is_rejected(self):
        with patch.object(deploy, "run", return_value="\tlibarchive.so.13 => not found\n"):
            with self.assertRaisesRegex(RuntimeError, "Unresolved dependency"):
                deploy.dependencies(Path("fixture-launcher"))

    def test_dependency_paths_may_contain_spaces(self):
        output = "\tlibQt6Core.so.6 => /tmp/SDK path/libQt6Core.so.6 (0x00007ffff)\n"
        with patch.object(deploy, "run", return_value=output):
            self.assertEqual(deploy.dependencies(Path("fixture")), [
                ("libQt6Core.so.6", Path("/tmp/SDK path/libQt6Core.so.6"))
            ])

    def test_qt_and_libarchive_cannot_be_satisfied_by_host_libraries(self):
        for name in ("libQt6Core.so.6", "libarchive.so.13", "libstdc++.so.6", "libz.so.1"):
            self.assertIsNone(deploy.HOST_LIBRARIES.fullmatch(name))

    def test_loader_failure_prevents_deployment(self):
        with patch.object(deploy, "run", side_effect=subprocess.CalledProcessError(1, "ldd")):
            with self.assertRaises(subprocess.CalledProcessError):
                deploy.dependencies(Path("fixture"))

    def test_verification_rejects_dependency_outside_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(deploy, "dependencies", return_value=[
                ("libarchive.so.13", Path("/usr/lib/libarchive.so.13"))
            ]):
                with self.assertRaisesRegex(RuntimeError, "Unbundled dependency"):
                    deploy.verify(root)

    def test_incomplete_qt_deployment_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(deploy, "dependencies", return_value=[]):
                with self.assertRaisesRegex(RuntimeError, "Required runtime was not deployed"):
                    deploy.verify(Path(temporary))


if __name__ == "__main__":
    unittest.main()

"""Host-independent deployment tests; no downloaded or application code executes."""

import contextlib
import importlib.util
import io
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("macos_deploy", Path(__file__).with_name("deploy.py"))
deploy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deploy)


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.bundle = self.root / "Prismatic Launcher.app"
        self.executable = self.binary(self.bundle / "Contents/MacOS/prismatic-launch")
        self.frameworks = self.bundle / "Contents/Frameworks"
        self.frameworks.mkdir()

    @staticmethod
    def binary(path, payload=b"fixture"):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\xcf\xfa\xed\xfe" + payload)
        return path

    def test_system_paths_are_narrowly_scoped(self):
        self.assertTrue(deploy.system_path("/System/Library/Frameworks/Foundation.framework/Foundation"))
        self.assertTrue(deploy.system_path("/usr/lib/libSystem.B.dylib"))
        self.assertFalse(deploy.system_path("/usr/local/lib/libarchive.dylib"))
        self.assertFalse(deploy.system_path("/opt/homebrew/lib/libarchive.dylib"))
        self.assertFalse(deploy.system_path("/usr/lib/../../opt/homebrew/lib/libarchive.dylib"))

    def test_fat_dependency_listing_deduplicates_imports(self):
        output = "fixture (architecture x86_64):\n\t@rpath/QtCore (compatibility version 6.0.0, current version 6.8.3)\n"
        output += "fixture (architecture arm64):\n\t@rpath/QtCore (compatibility version 6.0.0, current version 6.8.3)\n"
        with patch.object(deploy, "run", return_value=output):
            self.assertEqual(deploy.dependencies(self.executable), ["@rpath/QtCore"])

    def test_minimum_os_uses_highest_requirement_across_slices(self):
        output = """Load command 1
      cmd LC_BUILD_VERSION
  cmdsize 32
 platform 1
    minos 12.0
      sdk 14.0
    ntools 1
     tool 3
  version 1022.1
Load command 2
      cmd LC_VERSION_MIN_MACOSX
  cmdsize 16
  version 13.3
      sdk 14.0
"""
        with patch.object(deploy, "run", return_value=output):
            self.assertEqual(deploy.minimum_macos_version(self.executable), (13, 3, 0))

    def test_bundle_advertises_actual_dependency_minimum_os(self):
        info = self.bundle / "Contents/Info.plist"
        info.write_bytes(plistlib.dumps({"LSMinimumSystemVersion": "12.0"}))
        self.binary(self.frameworks / "libarchive.dylib")
        minima = lambda binary: (14, 0, 0) if binary.name == "libarchive.dylib" else (12, 0, 0)
        with patch.object(deploy, "minimum_macos_version", side_effect=minima), contextlib.redirect_stdout(io.StringIO()):
            deploy.update_minimum_os(self.bundle)
        self.assertEqual(plistlib.loads(info.read_bytes())["LSMinimumSystemVersion"], "14.0.0")

    def test_bundle_updates_read_only_info_plist_and_preserves_mode(self):
        info = self.bundle / "Contents/Info.plist"
        info.write_bytes(plistlib.dumps({"LSMinimumSystemVersion": "12.0"}))
        info.chmod(0o444)
        self.binary(self.frameworks / "libarchive.dylib")
        minima = lambda binary: (14, 0, 0) if binary.name == "libarchive.dylib" else (12, 0, 0)
        with patch.object(deploy, "minimum_macos_version", side_effect=minima), contextlib.redirect_stdout(io.StringIO()):
            deploy.update_minimum_os(self.bundle)
        self.assertEqual(plistlib.loads(info.read_bytes())["LSMinimumSystemVersion"], "14.0.0")
        self.assertEqual(info.stat().st_mode & 0o777, 0o444)

    def test_signs_nested_code_before_the_app_bundle(self):
        executable = self.bundle / "Contents/MacOS/prismatic-launch"
        plugin = self.binary(self.bundle / "Contents/PlugIns/imageformats/libqjpeg.dylib")
        commands = []
        with patch.object(deploy, "macho_files", return_value=[executable, plugin]), patch.object(
            deploy.subprocess, "run", return_value=type("Result", (), {"returncode": 1})()
        ), patch.object(deploy, "run", side_effect=lambda *args: commands.append(args) or ""):
            deploy.sign(self.bundle, "adhoc")
        self.assertEqual(commands[0][-1], str(plugin))
        self.assertEqual(commands[-1][-1], str(self.bundle))
        self.assertNotIn(str(executable), [command[-1] for command in commands])

    def test_escaping_bundle_symlink_is_rejected(self):
        (self.frameworks / "escape").symlink_to(self.root)
        with self.assertRaisesRegex(RuntimeError, "escapes app"):
            deploy.validate_symlinks(self.bundle)

    def test_escaping_framework_directory_fails_before_any_mutation(self):
        external = self.root / "external"
        external.mkdir()
        sentinel = external / "sentinel"
        sentinel.write_text("unchanged")
        self.frameworks.rmdir()
        self.frameworks.symlink_to(external, target_is_directory=True)
        with patch.object(deploy, "run") as tool:
            with self.assertRaisesRegex(RuntimeError, "escapes app"):
                deploy.deploy(self.bundle)
            tool.assert_not_called()
        self.assertEqual(list(external.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_text(), "unchanged")

    def test_dependency_filename_collision_fails(self):
        existing = self.binary(self.frameworks / "libarchive.dylib", b"one")
        source = self.binary(self.root / "sdk/libarchive.dylib", b"two")
        with self.assertRaisesRegex(RuntimeError, "filename collision"):
            deploy.copy_dependency(source, self.frameworks)
        self.assertTrue(existing.read_bytes().endswith(b"one"))

    def test_unavailable_dependency_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "Cannot resolve"):
            deploy.resolve_dependency("@loader_path/missing.dylib", self.executable,
                                      self.executable, self.frameworks, [])

    def test_absolute_external_import_fails_audit(self):
        def tool(*args):
            return "file:\n\t/opt/homebrew/lib/libarchive.dylib (compatibility version 1.0.0)\n" if args[1] == "-L" else "file:\n"
        with patch.object(deploy, "run", side_effect=tool):
            with self.assertRaisesRegex(RuntimeError, "External/unresolved import"):
                deploy.audit(self.bundle)

    def test_transitive_libraries_are_copied_and_rewritten(self):
        archive = self.binary(self.root / "sdk/libarchive.13.dylib")
        lzma = self.binary(self.root / "sdk/liblzma.5.dylib")
        imports = {
            self.executable.name: [str(archive)],
            archive.name: [str(lzma), "/usr/lib/libSystem.B.dylib"],
            lzma.name: ["/usr/lib/libSystem.B.dylib"],
        }
        identifiers = {archive.name: str(archive), lzma.name: str(lzma)}

        def tool(*args):
            binary = Path(args[-1])
            if args[0] == "otool":
                if args[1] == "-l":
                    return ""
                if args[1] == "-D":
                    return str(binary) + ":\n" + identifiers.get(binary.name, "") + "\n"
                dependencies = ([identifiers[binary.name]] if binary.name in identifiers else []) + imports[binary.name]
                return str(binary) + ":\n" + "".join(f"\t{value} (compatibility version 1.0.0)\n" for value in dependencies)
            if args[0] == "install_name_tool":
                if args[1] == "-change":
                    imports[binary.name] = [args[3] if value == args[2] else value for value in imports[binary.name]]
                elif args[1] == "-id":
                    identifiers[binary.name] = args[2]
                return ""
            self.fail(f"Unexpected command: {args}")

        with patch.object(deploy, "run", side_effect=tool), contextlib.redirect_stdout(io.StringIO()):
            deploy.deploy(self.bundle)
        self.assertTrue((self.frameworks / archive.name).is_file())
        self.assertTrue((self.frameworks / lzma.name).is_file())
        self.assertEqual(imports[self.executable.name], ["@loader_path/../Frameworks/libarchive.13.dylib"])
        self.assertEqual(imports[archive.name][0], "@loader_path/liblzma.5.dylib")


if __name__ == "__main__":
    unittest.main()

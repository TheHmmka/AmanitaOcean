#!/usr/bin/env python3
"""Exercise the real macOS installer with release bundles in an isolated root.

Usage: python3 Tests/test_macos_installer.py path/to/macos-release.zip
No files in the user's actual plug-in folders are read or changed.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
INSTALLER = REPO / "packaging/macos/Install Amanita Ocean.command"
FORMATS = {"VST3": "vst3", "Components": "component", "CLAP": "clap"}
QUARANTINE = "com.apple.quarantine"
MARKER = "audio.amanitaocean.installer-test"


def set_attr(path, name, value):
    subprocess.run(["/usr/bin/xattr", "-wx", name, value.hex(), str(path)], check=True)


def get_attr(path, name):
    result = subprocess.run(["/usr/bin/xattr", "-px", name, str(path)], check=True, text=True, stdout=subprocess.PIPE)
    return bytes.fromhex(result.stdout)


def list_attrs(path):
    result = subprocess.run(["/usr/bin/xattr", str(path)], check=True, text=True, stdout=subprocess.PIPE)
    return result.stdout.splitlines()


class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.suite_temp = tempfile.TemporaryDirectory(prefix="amanita-installer-tests-", dir="/private/tmp")
        cls.root = Path(cls.suite_temp.name)
        extracted = cls.root / "release"
        subprocess.run(["/usr/bin/ditto", "-xk", str(ARCHIVE), str(extracted)], check=True)
        cls.source = next(extracted.glob("*/Amanita Ocean.vst3")).parent

    @classmethod
    def tearDownClass(cls):
        cls.suite_temp.cleanup()

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(prefix="case-", dir=self.root))
        self.package = self.case / "Релиз с пробелами"
        shutil.copytree(self.source, self.package)
        self.script = self.package / INSTALLER.name
        shutil.copy2(INSTALLER, self.script)
        self.target = self.case / "User Plug-Ins"
        self.env = dict(os.environ, AMANITA_OCEAN_INSTALL_ROOT=str(self.target))

    def run_installer(self, *, succeeds=True):
        result = subprocess.run(
            ["/bin/bash", str(self.script)], env=self.env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45,
        )
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("Installed Amanita Ocean", result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertNotIn("Installed Amanita Ocean", result.stdout)
        return result

    def bundle(self, format_name):
        return self.target / format_name / f"Amanita Ocean.{FORMATS[format_name]}"

    def verify_installed(self):
        for format_name in FORMATS:
            bundle = self.bundle(format_name)
            self.assertTrue(bundle.is_dir())
            subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
            for entry in [bundle, *bundle.rglob("*")]:
                self.assertNotIn(QUARANTINE, list_attrs(entry))
        self.assertFalse((self.target / ".Amanita-Ocean-install.lock").exists())

    def seed_existing(self):
        for format_name in FORMATS:
            bundle = self.bundle(format_name)
            bundle.mkdir(parents=True)
            (bundle / "previous-version.txt").write_text(format_name)

    def assert_previous_restored(self):
        for format_name in FORMATS:
            bundle = self.bundle(format_name)
            self.assertEqual((bundle / "previous-version.txt").read_text(), format_name)
            self.assertEqual(len(list(bundle.iterdir())), 1)
        self.assertFalse(list(self.target.glob(".Amanita-Ocean-install*")))

    def inject(self, original, replacement):
        source = self.script.read_text()
        self.assertEqual(source.count(original), 1, "Fault injection location changed")
        self.script.write_text(source.replace(original, replacement))

    def test_quarantine_removed_only_from_installed_bundles(self):
        # Simulate a browser download, including quarantine on nested files.
        for extension in FORMATS.values():
            source = self.package / f"Amanita Ocean.{extension}"
            subprocess.run(["/usr/bin/xattr", "-wr", QUARANTINE, "0083;12345678;InstallerTests;", str(source)], check=True)
            set_attr(source, MARKER, b"preserve-me")
            set_attr(source / "Contents/MacOS/Amanita Ocean", MARKER, b"preserve-binary-attribute")
        unrelated = self.target / "VST3/Another Plugin.vst3"
        unrelated.mkdir(parents=True)
        set_attr(unrelated, QUARANTINE, b"leave-me-alone")
        self.run_installer()
        self.verify_installed()
        for format_name, extension in FORMATS.items():
            self.assertEqual(get_attr(self.bundle(format_name), MARKER), b"preserve-me")
            self.assertEqual(get_attr(self.bundle(format_name) / "Contents/MacOS/Amanita Ocean", MARKER), b"preserve-binary-attribute")
            self.assertIn(QUARANTINE, list_attrs(self.package / f"Amanita Ocean.{extension}"))
        self.assertEqual(get_attr(unrelated, QUARANTINE), b"leave-me-alone")

    def test_repeat_install_preserves_previous_versions(self):
        self.run_installer()
        for format_name in FORMATS:
            set_attr(self.bundle(format_name), MARKER, format_name.encode())
        result = self.run_installer()
        self.verify_installed()
        backups = list(self.target.glob(".Amanita-Ocean-install.*/previous"))
        self.assertEqual(len(backups), 1)
        self.assertIn(str(backups[0]), result.stdout)
        for format_name in FORMATS:
            previous = backups[0] / f"{format_name}.backup"
            self.assertEqual(get_attr(previous, MARKER), format_name.encode())
            subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(previous)], check=True)

    def test_incomplete_package_does_not_change_existing_install(self):
        self.seed_existing()
        shutil.rmtree(self.package / "Amanita Ocean.clap")
        result = self.run_installer(succeeds=False)
        self.assertIn("Missing plug-in", result.stdout)
        self.assert_previous_restored()

    def test_invalid_signature_does_not_change_existing_install(self):
        self.seed_existing()
        executable = self.package / "Amanita Ocean.component/Contents/MacOS/Amanita Ocean"
        with executable.open("ab") as stream:
            stream.write(b"invalid-signature")
        result = self.run_installer(succeeds=False)
        self.assertIn("Invalid code signature", result.stdout)
        self.assert_previous_restored()

    def test_source_symlink_is_rejected(self):
        self.seed_existing()
        outside = self.case / "outside"
        outside.write_text("untouched")
        set_attr(outside, QUARANTINE, b"leave-me-alone")
        (self.package / "Amanita Ocean.vst3/linked-file").symlink_to(outside)
        result = self.run_installer(succeeds=False)
        self.assertIn("contains symbolic links", result.stdout)
        self.assert_previous_restored()
        self.assertEqual(get_attr(outside, QUARANTINE), b"leave-me-alone")

    def test_destination_symlink_is_rejected(self):
        outside = self.case / "outside"
        outside.mkdir()
        self.target.mkdir()
        (self.target / "VST3").symlink_to(outside, target_is_directory=True)
        result = self.run_installer(succeeds=False)
        self.assertIn("Symbolic links are not supported", result.stdout)
        self.assertEqual(list(outside.iterdir()), [])
        self.assertFalse((self.target / "Components").exists())

    def test_existing_lock_is_preserved(self):
        lock = self.target / ".Amanita-Ocean-install.lock"
        lock.mkdir(parents=True)
        result = self.run_installer(succeeds=False)
        self.assertIn("Another installation may be running", result.stdout)
        self.assertTrue(lock.is_dir())
        self.assertFalse((self.target / "VST3").exists())

    def test_commit_failure_restores_all_previous_versions(self):
        self.seed_existing()
        # Fail after moving the second previous version to its backup.
        command = '    /bin/mv "$staged" "$destination"\n'
        self.inject(command, '    [[ "$i" != 1 ]] || exit 97\n' + command)
        result = self.run_installer(succeeds=False)
        self.assertEqual(result.returncode, 97, result.stdout)
        self.assertIn("Restoring the previous installation", result.stdout)
        self.assert_previous_restored()

    def test_signal_after_rename_restores_previous_versions(self):
        self.seed_existing()
        command = '    /bin/mv "$staged" "$destination"\n'
        self.inject(command, command + '    if [[ "$i" == 1 ]]; then kill -TERM "$$"; fi\n')
        result = self.run_installer(succeeds=False)
        self.assertEqual(result.returncode, 143, result.stdout)
        self.assertIn("Restoring the previous installation", result.stdout)
        self.assert_previous_restored()

    def test_killed_install_reports_recovery_files_on_retry(self):
        self.seed_existing()
        command = '    /bin/mv "$staged" "$destination"\n'
        self.inject(command, '    if [[ "$i" == 0 ]]; then kill -KILL "$$"; fi\n' + command)
        result = self.run_installer(succeeds=False)
        self.assertEqual(result.returncode, -9, result.stdout)
        pending = list(self.target.glob(".Amanita-Ocean-install.*/pending"))
        self.assertEqual(len(pending), 1)
        recovery = pending[0].parent
        self.assertEqual((recovery / "previous/VST3.backup/previous-version.txt").read_text(), "VST3")
        shutil.copy2(INSTALLER, self.script)
        result = self.run_installer(succeeds=False)
        self.assertIn(str(recovery), result.stdout)
        self.assertIn("recover any previous plug-ins", result.stdout)
        self.assertTrue((recovery / "previous/VST3.backup").is_dir())
        # Removing the stale lock alone must not hide an unfinished transaction.
        (self.target / ".Amanita-Ocean-install.lock").rmdir()
        result = self.run_installer(succeeds=False)
        self.assertIn(str(recovery), result.stdout)
        self.assertTrue((recovery / "previous/VST3.backup").is_dir())

    def test_commit_failure_removes_partial_fresh_install(self):
        command = '    /bin/mv "$staged" "$destination"\n'
        self.inject(command, '    [[ "$i" != 1 ]] || exit 97\n' + command)
        result = self.run_installer(succeeds=False)
        self.assertEqual(result.returncode, 97, result.stdout)
        for format_name in FORMATS:
            self.assertFalse(self.bundle(format_name).exists())
        self.assertFalse(list(self.target.glob(".Amanita-Ocean-install*")))


if __name__ == "__main__":
    if sys.platform != "darwin" or len(sys.argv) != 2:
        sys.exit("Run on macOS: python3 Tests/test_macos_installer.py path/to/macos-release.zip")
    ARCHIVE = Path(sys.argv.pop()).resolve(strict=True)
    unittest.main(verbosity=2)

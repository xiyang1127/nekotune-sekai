from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("release", Path(__file__).parents[1] / "scripts/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = {"id": "nekotune.example", "version": "1.0.1", "apiVersion": 2}
        self.source = self.root / "extension.json"
        self.source.write_text(json.dumps(self.manifest), encoding="utf-8")
        self.packages = self.root / "packages"
        self.packages.mkdir()
        self.archive = self.packages / "old-name.zip"
        self.info = self.root / "build-info.json"
        self.info.write_text(json.dumps({"qt": "6.11.2", "compiler": "GNU", "compilerVersion": "13"}))
        self.output = self.root / "release"

    def archive_for(self, platform: str = "linux-x86_64", **changes) -> None:
        manifest = self.manifest | {"qt": "6.11", "library": {platform: "lib/plugin.so"}} | changes
        with zipfile.ZipFile(self.archive, "w") as package:
            package.writestr("extension.json", json.dumps(manifest))
            package.writestr("lib/plugin.so", b"native library fixture")

    def prepare(self) -> None:
        release.prepare(self.source, self.packages, self.output, self.info)

    def test_native_assets_and_checksum_detect_tampering(self) -> None:
        self.archive_for()
        self.prepare()
        assets = release.validate_assets(self.manifest, self.output, ["linux-x86_64"], "v1.0.1")
        self.assertEqual([p.name for p in assets], ["nekotune.example-1.0.1-linux.zip"])
        self.assertEqual(list(self.output.glob("*.json")), [])
        before = release.checksums(assets)
        with assets[0].open("ab") as file:
            file.write(b"modified")
        self.assertNotEqual(before, release.checksums(assets))

    def test_reject_stale_version(self) -> None:
        self.archive_for(version="1.0.0")
        with self.assertRaisesRegex(ValueError, "ID/version"):
            self.prepare()

    def test_reject_unsafe_entry(self) -> None:
        self.archive_for()
        with zipfile.ZipFile(self.archive, "a") as package:
            package.writestr("../escape", "bad")
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            self.prepare()

    def test_reject_missing_platform_or_wrong_tag(self) -> None:
        self.archive_for()
        self.prepare()
        with self.assertRaises(FileNotFoundError):
            release.validate_assets(self.manifest, self.output, ["linux-x86_64", "windows-x86_64"], "v1.0.1")
        with self.assertRaisesRegex(ValueError, "tag"):
            release.validate_assets(self.manifest, self.output, ["linux-x86_64"], "v1.0.0")

    def test_reject_json_attachments(self) -> None:
        self.archive_for()
        self.prepare()
        (self.output / "stale.json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "Unexpected"):
            release.validate_assets(self.manifest, self.output, ["linux-x86_64"], "v1.0.1")

    def test_reject_wrong_build_environment(self) -> None:
        self.archive_for()
        self.info.write_text(json.dumps({"qt": "6.10.0", "compiler": "GNU", "compilerVersion": "13"}))
        with self.assertRaisesRegex(ValueError, "Qt build information"):
            self.prepare()

    def test_reject_architecture_ambiguity(self) -> None:
        self.archive_for("linux-arm64")
        with self.assertRaisesRegex(ValueError, "x86_64"):
            self.prepare()

    def test_windows_short_name(self) -> None:
        self.archive_for("windows-x86_64")
        self.prepare()
        assets = release.validate_assets(self.manifest, self.output, ["windows-x86_64"], "v1.0.1")
        self.assertEqual([p.name for p in assets], ["nekotune.example-1.0.1-windows.zip"])

    def test_theme_has_platform_independent_name(self) -> None:
        self.manifest.update(type="theme", apiVersion=1)
        self.source.write_text(json.dumps(self.manifest))
        with zipfile.ZipFile(self.archive, "w") as package:
            package.writestr("extension.json", json.dumps(self.manifest))
        release.prepare(self.source, self.packages, self.output, None)
        assets = release.validate_assets(self.manifest, self.output, ["any"], "v1.0.1")
        self.assertEqual([p.name for p in assets], ["nekotune.example-1.0.1.zip"])


if __name__ == "__main__":
    unittest.main()

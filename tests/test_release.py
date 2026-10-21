from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
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
        with patch.dict("os.environ", {"GITHUB_SHA": "a" * 40}):
            release.prepare(self.source, self.packages, self.output, self.info)

    def test_native_assets_and_checksum_detect_tampering(self) -> None:
        self.archive_for()
        self.prepare()
        assets = release.validate_assets(self.manifest, self.output, ["linux-x86_64"], "v1.0.1")
        self.assertEqual(len(assets), 2)
        self.assertTrue(all(p.name.startswith("nekotune.example-1.0.1-linux-x86_64") for p in assets))
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

    def test_reject_extra_files_and_mismatched_commit(self) -> None:
        self.archive_for()
        self.prepare()
        (self.output / "stale.json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "Unexpected"):
            release.validate_assets(self.manifest, self.output, ["linux-x86_64"], "v1.0.1")
        (self.output / "stale.json").unlink()
        metadata_path = next(self.output.glob("*.json"))
        metadata = release.read_json(metadata_path)
        metadata["sourceCommit"] = "unknown"
        metadata_path.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(ValueError, "source commit"):
            release.validate_assets(self.manifest, self.output, ["linux-x86_64"], "v1.0.1")

    def test_theme_has_platform_independent_name(self) -> None:
        self.manifest.update(type="theme", apiVersion=1)
        self.source.write_text(json.dumps(self.manifest))
        with zipfile.ZipFile(self.archive, "w") as package:
            package.writestr("extension.json", json.dumps(self.manifest))
        with patch.dict("os.environ", {"GITHUB_SHA": "a" * 40}):
            release.prepare(self.source, self.packages, self.output, None)
        assets = release.validate_assets(self.manifest, self.output, ["any"], "v1.0.1")
        self.assertTrue(any(p.name == "nekotune.example-1.0.1-any.nktheme.zip" for p in assets))


if __name__ == "__main__":
    unittest.main()

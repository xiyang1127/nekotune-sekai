"""Prepare and verify versioned NekoTune release assets using only the stdlib."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import zipfile


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def identity(manifest: dict) -> tuple[str, str]:
    extension_id, version = manifest["id"], manifest["version"]
    require(bool(re.fullmatch(r"[a-z0-9]+(?:[.-][a-z0-9]+)*", extension_id)), "Invalid extension ID")
    require(bool(re.fullmatch(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?", version)), "Invalid version")
    return extension_id, version


def inspect_package(path: Path, source: dict) -> tuple[str, dict]:
    with zipfile.ZipFile(path) as package:
        names = package.namelist()
        require(len(names) == len(set(names)), "Duplicate archive entries")
        for name in names:
            parts = PurePosixPath(name)
            require(not parts.is_absolute() and ".." not in parts.parts and "\\" not in name,
                    f"Unsafe archive entry: {name}")
        require("extension.json" in names, "Manifest must be at ZIP root")
        manifest = json.loads(package.read("extension.json"))
        require(identity(manifest) == identity(source), "Archive ID/version differs from source")
        require(manifest.get("apiVersion") == source.get("apiVersion"), "API version mismatch")
        if source.get("type") == "theme":
            require(manifest.get("type") == "theme" and "library" not in manifest,
                    "Invalid theme package")
            platform = "any"
        else:
            libraries = manifest.get("library", {})
            require(len(libraries) == 1, "Expected exactly one native platform")
            platform, library = next(iter(libraries.items()))
            require(bool(re.fullmatch(r"(?:linux|windows)-(?:x86_64|arm64)", platform)),
                    f"Unsupported platform: {platform}")
            require(isinstance(library, str) and library in names, "Native library missing")
            require(bool(manifest.get("qt")), "Qt ABI baseline missing")
        require(package.testzip() is None, "Corrupt ZIP entry")
    return platform, manifest


def asset_stem(manifest: dict, platform: str) -> str:
    extension_id, version = identity(manifest)
    return f"{extension_id}-{version}-{platform}"


def package_suffix(manifest: dict) -> str:
    return ".nktheme.zip" if manifest.get("type") == "theme" else ".nekotune.zip"


def prepare(manifest_path: Path, packages: Path, output: Path, build_info: Path | None) -> None:
    manifest = read_json(manifest_path)
    archives = sorted(packages.glob("*.zip"))
    require(len(archives) == 1, "Expected exactly one package per build")
    platform, packaged = inspect_package(archives[0], manifest)
    metadata = read_json(build_info) if build_info else {}
    if platform != "any":
        require(bool(metadata), "Native build information required")
        require(all(metadata.get(key) for key in ("qt", "compiler", "compilerVersion")),
                "Incomplete native build information")
        require(".".join(metadata["qt"].split(".")[:2]) == packaged["qt"], "Qt build information mismatch")
        if "platform" in metadata:
            require(metadata["platform"] == platform, "Build platform mismatch")
    require(not output.exists() or not any(output.iterdir()), "Release output must be empty")
    output.mkdir(parents=True, exist_ok=True)
    stem = asset_stem(manifest, platform)
    filename = stem + package_suffix(manifest)
    shutil.copyfile(archives[0], output / filename)
    commit = os.environ.get("GITHUB_SHA") or subprocess.check_output(
        ["git", "rev-parse", "HEAD"], text=True
    ).strip()
    metadata.update({"id": manifest["id"], "version": manifest["version"],
                     "apiVersion": manifest["apiVersion"], "platform": platform,
                     "sourceCommit": commit, "package": filename})
    (output / f"{stem}.build-info.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Prepared {filename}")


def validate_assets(manifest: dict, directory: Path, platforms: list[str], tag: str | None) -> list[Path]:
    extension_id, version = identity(manifest)
    require(tag is None or tag == f"v{version}", "Release tag does not match manifest version")
    require(len(platforms) == len(set(platforms)), "Duplicate expected platforms")
    require(platforms == ["any"] if manifest.get("type") == "theme" else "any" not in platforms,
            "Incorrect platform selection")
    assets: list[Path] = []
    commits = set()
    for platform in platforms:
        stem = asset_stem(manifest, platform)
        package = directory / (stem + package_suffix(manifest))
        info = directory / f"{stem}.build-info.json"
        actual_platform, packaged = inspect_package(package, manifest)
        require(actual_platform == platform, "Package platform differs from filename")
        metadata = read_json(info)
        require((metadata.get("id"), metadata.get("version"), metadata.get("platform"))
                == (extension_id, version, platform), "Build information identity mismatch")
        require(metadata.get("package") == package.name, "Build information package mismatch")
        require(metadata.get("apiVersion") == manifest["apiVersion"], "Build information API mismatch")
        require(bool(re.fullmatch(r"[0-9a-f]{40}", metadata.get("sourceCommit", ""))), "Invalid source commit")
        if platform != "any":
            require(all(metadata.get(key) for key in ("qt", "compiler", "compilerVersion")),
                    "Incomplete native build information")
            require(".".join(metadata["qt"].split(".")[:2]) == packaged["qt"], "Qt metadata mismatch")
        commits.add(metadata["sourceCommit"])
        assets.extend((package, info))
    require(len(commits) == 1, "Platform builds came from different commits")
    expected = {p.name for p in assets} | {"SHA256SUMS.txt"}
    require(all(p.is_file() and p.name in expected for p in directory.iterdir()), "Unexpected release asset")
    return sorted(assets)


def checksums(assets: list[Path]) -> str:
    return "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in assets)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "finalize", "verify"))
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--packages", type=Path, default=Path("build/packages"))
    parser.add_argument("--output", type=Path, default=Path("build/release"))
    parser.add_argument("--build-info", type=Path)
    parser.add_argument("--platforms", nargs="+", default=["linux-x86_64", "windows-x86_64"])
    parser.add_argument("--tag")
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(args.manifest, args.packages, args.output, args.build_info)
    else:
        assets = validate_assets(read_json(args.manifest), args.output, args.platforms, args.tag)
        checksum_path = args.output / "SHA256SUMS.txt"
        expected = checksums(assets)
        if args.command == "finalize":
            checksum_path.write_text(expected, encoding="utf-8")
        else:
            require(checksum_path.read_text(encoding="utf-8") == expected, "Release checksum mismatch")
        print(f"Validated {len(assets)} release assets")


if __name__ == "__main__":
    main()

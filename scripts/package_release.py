#!/usr/bin/env python3
"""Create a reviewable, self-contained pre-flash firmware release."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence


DEFAULT_BOARD = "lckfb/szpi-esp32s3-dashboard"
DEFAULT_VARIANT = "lichuang-dev-dashboard"
DEFAULT_VERSION = "v0.1-pre-flash"
PUBLIC_FALLBACK_URL = "http://xiaozhi-dashboard.local:8765"
UPSTREAM_REPOSITORY = "https://github.com/78/xiaozhi-esp32"
UPSTREAM_REVISION = "d395220a83fb7a16ea5dc761c08a4816177cc880"
CHIP_PATTERN = re.compile(r"^esp32[a-z0-9]*$")


class PackagingError(RuntimeError):
    """Raised when build output cannot be safely packaged."""


@dataclass(frozen=True)
class FlashImage:
    offset: int
    source: Path
    release_name: str


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise PackagingError(f"missing required build file: {path}") from error
    except json.JSONDecodeError as error:
        raise PackagingError(f"invalid JSON in {path}: {error}") from error
    if not isinstance(value, dict):
        raise PackagingError(f"expected a JSON object in {path}")
    return value


def _parse_offset(value: object) -> int:
    if not isinstance(value, str):
        raise PackagingError(f"flash offset must be a string, got {value!r}")
    try:
        offset = int(value, 0)
    except ValueError as error:
        raise PackagingError(f"invalid flash offset: {value!r}") from error
    if offset < 0:
        raise PackagingError(f"flash offset cannot be negative: {value!r}")
    return offset


def _resolve_build_file(build_dir: Path, value: object) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise PackagingError(f"invalid flash image path: {value!r}")
    candidate = (build_dir / value).resolve()
    try:
        candidate.relative_to(build_dir)
    except ValueError as error:
        raise PackagingError(f"flash image escapes the build directory: {value!r}") from error
    if candidate.suffix.lower() != ".bin":
        raise PackagingError(f"flash image is not a .bin file: {value!r}")
    if not candidate.is_file() or candidate.stat().st_size == 0:
        raise PackagingError(f"flash image is missing or empty: {candidate}")
    return candidate


def _release_filename(source: Path, build_dir: Path, used: set[str]) -> str:
    name = source.name
    if name not in used:
        used.add(name)
        return name

    relative_parts = source.relative_to(build_dir).parts
    name = "-".join(relative_parts)
    if name in used:
        raise PackagingError(f"release filename collision for {source}")
    used.add(name)
    return name


def _load_flash_images(build_dir: Path, flasher: dict) -> list[FlashImage]:
    raw_files = flasher.get("flash_files")
    if not isinstance(raw_files, dict) or not raw_files:
        raise PackagingError("flasher_args.json has no non-empty flash_files object")

    parsed: list[tuple[int, Path]] = []
    for raw_offset, raw_path in raw_files.items():
        parsed.append(
            (_parse_offset(raw_offset), _resolve_build_file(build_dir, raw_path))
        )
    parsed.sort(key=lambda item: item[0])

    offsets = [offset for offset, _ in parsed]
    if len(offsets) != len(set(offsets)):
        raise PackagingError("flasher_args.json contains duplicate flash offsets")

    used: set[str] = {"merged-firmware.bin"}
    images = [
        FlashImage(offset, source, _release_filename(source, build_dir, used))
        for offset, source in parsed
    ]
    for current, following in zip(images, images[1:]):
        current_end = current.offset + current.source.stat().st_size
        if current_end > following.offset:
            raise PackagingError(
                f"flash images overlap at 0x{following.offset:x}: "
                f"{current.source.name} ends at 0x{current_end:x}"
            )
    return images


def _write_flash_args(flasher: dict) -> list[str]:
    value = flasher.get("write_flash_args", [])
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise PackagingError("flasher_args.json write_flash_args must be a string array")
    if any(item in {"--encrypt", "-e"} for item in value):
        raise PackagingError("encrypted builds are not supported by this release packager")
    return value


def _chip_name(flasher: dict) -> str:
    extra = flasher.get("extra_esptool_args", {})
    chip = extra.get("chip") if isinstance(extra, dict) else None
    if not isinstance(chip, str) or not CHIP_PATTERN.fullmatch(chip):
        raise PackagingError("flasher_args.json does not contain a valid ESP chip name")
    return chip


def _cmake_cache_value(path: Path, key: str) -> str:
    try:
        content = path.read_text(encoding="utf-8")
    except FileNotFoundError as error:
        raise PackagingError(f"missing required build file: {path}") from error
    match = re.search(rf"^{re.escape(key)}:[^=]+=(.*)$", content, re.MULTILINE)
    if not match:
        raise PackagingError(f"{path} does not define {key}")
    return match.group(1).strip()


def _validate_dashboard_build(
    build_dir: Path,
    project_root: Path,
    board: str,
    variant: str,
    chip: str,
) -> None:
    """Prove that the build really is the one public Dashboard variant."""
    if board != DEFAULT_BOARD or variant != DEFAULT_VARIANT:
        raise PackagingError(
            "this packager only accepts the canonical Dashboard board and identity"
        )

    sdkconfig = _read_json(build_dir / "config" / "sdkconfig.json")
    selected_boards = sorted(
        key
        for key, value in sdkconfig.items()
        if key.startswith("BOARD_TYPE_") and value is True
    )
    expected_symbol = "BOARD_TYPE_LICHUANG_DEV_S3_DASHBOARD"
    if selected_boards != [expected_symbol]:
        raise PackagingError(
            "build does not select only CONFIG_BOARD_TYPE_LICHUANG_DEV_S3_DASHBOARD"
        )
    if sdkconfig.get("IDF_TARGET") != "esp32s3" or chip != "esp32s3":
        raise PackagingError("build target is not the required esp32s3")
    if sdkconfig.get("DASHBOARD_SERVICE_TOKEN") != "":
        raise PackagingError(
            "refusing to publish a build with a non-empty compile-time Dashboard token"
        )
    if sdkconfig.get("DASHBOARD_SERVICE_URL") != PUBLIC_FALLBACK_URL:
        raise PackagingError(
            "refusing to publish a build with a customized compile-time Dashboard URL"
        )

    cache_board_name = _cmake_cache_value(build_dir / "CMakeCache.txt", "BOARD_NAME")
    if cache_board_name != variant:
        raise PackagingError("CMake BOARD_NAME does not match the Dashboard identity")

    description = _read_json(build_dir / "project_description.json")
    if description.get("target") != "esp32s3":
        raise PackagingError("project_description.json target is not esp32s3")

    source_config_path = (
        project_root / "firmware" / "main" / "boards" / Path(board) / "config.json"
    )
    source_config = _read_json(source_config_path)
    builds = source_config.get("builds")
    build_names = {
        item.get("name") for item in builds if isinstance(item, dict)
    } if isinstance(builds, list) else set()
    if (
        source_config.get("type") != variant
        or source_config.get("target") != "esp32s3"
        or variant not in build_names
    ):
        raise PackagingError(
            "source board config does not match the Dashboard build identity"
        )


def _run(command: Sequence[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def _default_esptool_runner(command: Sequence[str]) -> None:
    result = _run(command)
    if result.returncode != 0:
        rendered = " ".join(command)
        raise PackagingError(f"esptool failed ({rendered}):\n{result.stdout.strip()}")


def _tool_version(command: Sequence[str]) -> str | None:
    try:
        result = _run(command)
    except OSError:
        return None
    if result.returncode != 0:
        return None
    output = result.stdout.strip()
    return output.splitlines()[-1] if output else None


def _idf_version_command() -> tuple[str, ...]:
    idf_path = os.environ.get("IDF_PATH")
    if idf_path:
        idf_script = Path(idf_path) / "tools" / "idf.py"
        if idf_script.is_file():
            return (sys.executable, str(idf_script), "--version")
    return ("idf.py", "--version")


def _git_value(project_root: Path, *args: str) -> str | None:
    try:
        result = _run(("git", "-C", str(project_root), *args))
    except OSError:
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def _generated_at() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    try:
        instant = dt.datetime.fromtimestamp(int(epoch), tz=dt.timezone.utc) if epoch else dt.datetime.now(dt.timezone.utc)
    except ValueError as error:
        raise PackagingError("SOURCE_DATE_EPOCH must be an integer") from error
    return instant.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _manifest_entry(path: Path) -> dict[str, object]:
    return {"name": path.name, "size": path.stat().st_size, "sha256": _sha256(path)}


def package_release(
    *,
    build_dir: Path,
    output_dir: Path,
    project_root: Path,
    board: str,
    variant: str,
    release_version: str,
    source_revision: str | None = None,
    esptool_runner: Callable[[Sequence[str]], None] = _default_esptool_runner,
) -> dict:
    """Package one ESP-IDF build and return its manifest."""
    build_dir = build_dir.resolve()
    output_dir = output_dir.resolve()
    project_root = project_root.resolve()
    if not build_dir.is_dir():
        raise PackagingError(f"build directory does not exist: {build_dir}")
    existing_output = list(output_dir.iterdir()) if output_dir.exists() else []
    tracked_release_docs = {"README.md", "release_notes.md"}
    unexpected_output = [
        path
        for path in existing_output
        if path.name not in tracked_release_docs or not path.is_file()
    ]
    if unexpected_output:
        raise PackagingError(
            f"output directory contains generated or unknown files: {output_dir}; "
            "remove them before packaging"
        )
    output_dir.mkdir(parents=True, exist_ok=True)

    flasher_path = build_dir / "flasher_args.json"
    flasher = _read_json(flasher_path)
    images = _load_flash_images(build_dir, flasher)
    write_args = _write_flash_args(flasher)
    chip = _chip_name(flasher)
    _validate_dashboard_build(build_dir, project_root, board, variant, chip)

    for image in images:
        shutil.copyfile(image.source, output_dir / image.release_name)
    shutil.copyfile(flasher_path, output_dir / "flasher_args.json")

    merged_path = output_dir / "merged-firmware.bin"
    merge_command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        chip,
        "merge-bin",
        "-o",
        str(merged_path),
        *write_args,
    ]
    for image in images:
        merge_command.extend((hex(image.offset), str(image.source)))
    esptool_runner(merge_command)
    if not merged_path.is_file() or merged_path.stat().st_size == 0:
        raise PackagingError("esptool did not create a non-empty merged firmware image")
    expected_size = max(image.offset + image.source.stat().st_size for image in images)
    if merged_path.stat().st_size < expected_size:
        raise PackagingError(
            "merged firmware is truncated: "
            f"expected at least {expected_size} bytes, got {merged_path.stat().st_size}"
        )

    flash_args = output_dir / "flash_args"
    flash_args.write_text(
        " ".join(write_args)
        + "\n"
        + "\n".join(f"0x{image.offset:x} {image.release_name}" for image in images)
        + "\n",
        encoding="utf-8",
        newline="\n",
    )

    revision = source_revision or os.environ.get("GITHUB_SHA") or _git_value(
        project_root, "rev-parse", "HEAD"
    )
    dirty = _git_value(project_root, "status", "--porcelain", "--untracked-files=no")
    payload_files = sorted(
        [path for path in output_dir.iterdir() if path.is_file()],
        key=lambda path: path.name,
    )
    manifest = {
        "schema_version": 1,
        "release": release_version,
        "status": "pre-flash",
        "hardware_verified": False,
        "board_directory": board,
        "board_identity": variant,
        "chip": chip,
        "generated_at": _generated_at(),
        "provenance": {
            "source_revision": revision or "unknown",
            "source_dirty": bool(dirty),
            "upstream_repository": UPSTREAM_REPOSITORY,
            "upstream_revision": UPSTREAM_REVISION,
            "esp_idf": _tool_version(_idf_version_command()),
            "esptool": _tool_version((sys.executable, "-m", "esptool", "version")),
            "packager": "scripts/package_release.py",
        },
        "flash": {
            "merged_image": "merged-firmware.bin",
            "merged_offset": "0x0",
            "partition_args": "flash_args",
        },
        "files": [_manifest_entry(path) for path in payload_files],
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    checksummed = sorted(
        [path for path in output_dir.iterdir() if path.is_file()],
        key=lambda path: path.name,
    )
    (output_dir / "SHA256SUMS").write_text(
        "".join(f"{_sha256(path)}  {path.name}\n" for path in checksummed),
        encoding="utf-8",
        newline="\n",
    )
    return manifest


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("firmware/build"))
    parser.add_argument("--output-dir", type=Path, default=Path("release"))
    parser.add_argument("--project-root", type=Path, default=Path.cwd())
    parser.add_argument("--board", default=DEFAULT_BOARD)
    parser.add_argument("--variant", default=DEFAULT_VARIANT)
    parser.add_argument("--release-version", default=DEFAULT_VERSION)
    parser.add_argument("--source-revision")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        manifest = package_release(
            build_dir=args.build_dir,
            output_dir=args.output_dir,
            project_root=args.project_root,
            board=args.board,
            variant=args.variant,
            release_version=args.release_version,
            source_revision=args.source_revision,
        )
    except PackagingError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(
        f"packaged {len(manifest['files'])} payload files in "
        f"{args.output_dir.resolve()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

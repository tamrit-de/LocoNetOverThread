#!/usr/bin/env python3
"""Package ESP-IDF flash images and versioned GitHub Release manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
from urllib.parse import quote

from deploy_hardware import (
    DeploymentError,
    application_configuration,
    flash_configuration,
)


REPOSITORY = "tamrit-de/LocoNetOverThread"
HARDWARE = {
    "client": {"chip": "ESP32-H2", "board": "ESP32-H2-DevKitM-1-N4"},
    "border-router": {"chip": "ESP32-C6", "board": "ESP32-C6-DevKitM-1-N4"},
}
RELEASE_TAG = re.compile(r"^v\d+\.\d+\.\d+(?:-(alpha|beta)\.\d+)?$")
FLASH_SETTINGS = {
    "--flash_mode": "flash_mode",
    "--flash_freq": "flash_freq",
    "--flash_size": "flash_size",
}


def release_channel(tag: str) -> str:
    match = RELEASE_TAG.fullmatch(tag)
    if match is None:
        raise DeploymentError(
            "Release tags must use vMAJOR.MINOR.PATCH, optionally followed by "
            "-alpha.N or -beta.N."
        )
    return match.group(1) or "stable"


def flash_settings(arguments: list[str]) -> dict[str, str]:
    settings = {}
    index = 0
    while index < len(arguments):
        option = arguments[index]
        if option in FLASH_SETTINGS:
            if index + 1 >= len(arguments):
                raise DeploymentError(f"Missing value for flash option {option}.")
            settings[FLASH_SETTINGS[option]] = arguments[index + 1]
            index += 2
        else:
            index += 1
    if set(settings) != set(FLASH_SETTINGS.values()):
        raise DeploymentError("Build is missing required flash mode, frequency, or size.")
    return settings


def package_application(
    application: str, tag: str, build_directory: Path, package_directory: Path
) -> Path:
    channel = release_channel(tag)
    configuration = application_configuration(application)
    target = configuration["target"]
    extra_arguments, flash_arguments = flash_configuration(build_directory, target)
    del extra_arguments

    raw_configuration = json.loads(
        (build_directory / "flasher_args.json").read_text(encoding="utf-8")
    )
    settings = flash_settings(raw_configuration.get("write_flash_args", []))
    image_pairs = flash_arguments[len(raw_configuration.get("write_flash_args", [])) :]
    package_directory.mkdir(parents=True, exist_ok=True)
    images = []
    for index in range(0, len(image_pairs), 2):
        address, source = image_pairs[index : index + 2]
        source_path = Path(source)
        image_name = re.sub(r"[^a-zA-Z0-9-]+", "-", source_path.stem).strip("-")
        filename = f"{application}-{target}-{index // 2}-{image_name}-{tag}.bin"
        destination = package_directory / filename
        shutil.copyfile(source_path, destination)
        with destination.open("rb") as image_file:
            digest = hashlib.file_digest(image_file, "sha256").hexdigest()
        images.append(
            {
                "name": image_name,
                "address": hex(int(address, 0)),
                "filename": filename,
                "url": (
                    f"https://github.com/{REPOSITORY}/releases/download/"
                    f"{quote(tag, safe='')}/{quote(filename, safe='')}"
                ),
                "sha256": digest,
                "size": destination.stat().st_size,
            }
        )

    fragment = {
        "schema_version": 1,
        "version": tag,
        "channel": channel,
        "hardware": [
            {
                "application": application,
                "target": target,
                **HARDWARE[application],
                "flash": settings,
                "images": images,
            }
        ],
    }
    fragment_path = package_directory / f"manifest-{tag}-{application}.json"
    fragment_path.write_text(
        json.dumps(fragment, indent=2) + "\n", encoding="utf-8"
    )
    return fragment_path


def combine_manifests(tag: str, package_directory: Path) -> Path:
    channel = release_channel(tag)
    hardware = []
    for application in sorted(HARDWARE):
        fragment_path = package_directory / f"manifest-{tag}-{application}.json"
        try:
            fragment = json.loads(fragment_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise DeploymentError(
                f"Cannot read release manifest fragment {fragment_path}: {error}"
            ) from error
        if (
            fragment.get("version") != tag
            or fragment.get("channel") != channel
            or len(fragment.get("hardware", [])) != 1
            or fragment["hardware"][0].get("application") != application
        ):
            raise DeploymentError(f"Invalid manifest fragment: {fragment_path}.")
        hardware.extend(fragment["hardware"])
        fragment_path.unlink()

    manifest_path = package_directory / f"manifest-{tag}.json"
    manifest_path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "version": tag,
                "channel": channel,
                "hardware": hardware,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return manifest_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--package-directory", type=Path, required=True)
    parser.add_argument("--application", choices=sorted(HARDWARE))
    parser.add_argument("--build-directory", type=Path)
    parser.add_argument("--combine", action="store_true")
    arguments = parser.parse_args()

    if arguments.combine:
        if arguments.application or arguments.build_directory:
            parser.error("--combine cannot be used with build arguments")
        result = combine_manifests(arguments.tag, arguments.package_directory)
    else:
        if not arguments.application or not arguments.build_directory:
            parser.error("packaging requires --application and --build-directory")
        result = package_application(
            arguments.application,
            arguments.tag,
            arguments.build_directory,
            arguments.package_directory,
        )
    print(result)


if __name__ == "__main__":
    main()

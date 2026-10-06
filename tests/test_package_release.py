from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from deploy_hardware import DeploymentError  # noqa: E402
from package_release import (  # noqa: E402
    combine_manifests,
    package_application,
    release_channel,
)


class ReleasePackageTests(unittest.TestCase):
    def test_release_channels_are_derived_from_versioned_tags(self) -> None:
        self.assertEqual(release_channel("v1.2.251006.1"), "stable")
        self.assertEqual(release_channel("v1.2.251006.2-beta"), "beta")
        self.assertEqual(release_channel("v1.2.251006.3-alpha"), "alpha")
        with self.assertRaises(DeploymentError):
            release_channel("main")
        with self.assertRaises(DeploymentError):
            release_channel("v1.2.251332.1")
        with self.assertRaises(DeploymentError):
            release_channel("v1.2.251006.4-rc")

    def test_packages_every_image_with_flash_address_and_sha256(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            build = root / "build"
            (build / "bootloader").mkdir(parents=True)
            (build / "partition_table").mkdir()
            images = {
                "bootloader/bootloader.bin": b"boot",
                "partition_table/partition-table.bin": b"partitions",
                "client.bin": b"application",
            }
            for relative_path, data in images.items():
                (build / relative_path).write_bytes(data)
            (build / "flasher_args.json").write_text(
                json.dumps(
                    {
                        "flash_files": {
                            "0x0": "bootloader/bootloader.bin",
                            "0x8000": "partition_table/partition-table.bin",
                            "0x10000": "client.bin",
                        },
                        "write_flash_args": [
                            "--flash_mode",
                            "dio",
                            "--flash_freq",
                            "40m",
                            "--flash_size",
                            "4MB",
                        ],
                        "extra_esptool_args": {
                            "after": "hard_reset",
                            "before": "default_reset",
                            "stub": True,
                            "chip": "esp32h2",
                        },
                    }
                ),
                encoding="utf-8",
            )
            package = root / "package"

            fragment_path = package_application(
                "client", "v1.2.251006.1-alpha", build, package
            )
            fragment = json.loads(fragment_path.read_text(encoding="utf-8"))

            hardware = fragment["hardware"][0]
            self.assertEqual(fragment["version"], "v1.2.251006.1-alpha")
            self.assertEqual(fragment["channel"], "alpha")
            self.assertEqual(hardware["chip"], "ESP32-H2")
            self.assertEqual(hardware["board"], "ESP32-H2-DevKitM-1-N4")
            self.assertEqual(
                [image["address"] for image in hardware["images"]],
                ["0x0", "0x8000", "0x10000"],
            )
            for image in hardware["images"]:
                binary = (package / image["filename"]).read_bytes()
                self.assertEqual(image["sha256"], hashlib.sha256(binary).hexdigest())
                self.assertIn(
                    "/releases/download/v1.2.251006.1-alpha/", image["url"]
                )

    def test_combines_target_manifests_and_removes_fragments(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            package = Path(temporary_directory)
            for application, chip in (
                ("client", "ESP32-H2"),
                ("border-router", "ESP32-C6"),
            ):
                (package / f"manifest-v1.0.251006.1-{application}.json").write_text(
                    json.dumps(
                        {
                            "version": "v1.0.251006.1",
                            "channel": "stable",
                            "hardware": [
                                {"application": application, "chip": chip}
                            ],
                        }
                    ),
                    encoding="utf-8",
                )

            manifest_path = combine_manifests("v1.0.251006.1", package)

            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(
                [entry["chip"] for entry in manifest["hardware"]],
                ["ESP32-C6", "ESP32-H2"],
            )
            self.assertEqual(
                list(package.glob("manifest-v1.0.251006.1-*.json")), []
            )


if __name__ == "__main__":
    unittest.main()

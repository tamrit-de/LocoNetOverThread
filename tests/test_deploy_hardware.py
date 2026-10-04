from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from deploy_hardware import (  # noqa: E402
    DeploymentError,
    APPLICATIONS,
    application_configuration,
    assert_single_device_assignment,
    contains_ready_marker,
    flash_configuration,
    normalize_extra_esptool_args,
    override_before_reset,
)


class DeploymentToolTests(unittest.TestCase):
    def test_application_configuration_matches_supported_targets(self) -> None:
        self.assertEqual(application_configuration("client")["target"], "esp32h2")
        self.assertEqual(
            application_configuration("border-router")["target"], "esp32c6"
        )

    def test_unknown_application_is_rejected(self) -> None:
        with self.assertRaises(DeploymentError):
            application_configuration("unknown")

    def test_duplicate_aliases_for_one_device_are_allowed(self) -> None:
        self.assertEqual(
            assert_single_device_assignment(
                ["/dev/ttyACM0", "/dev/ttyACM0"]
            ),
            os.path.realpath("/dev/ttyACM0"),
        )

    def test_multiple_physical_devices_are_rejected(self) -> None:
        with self.assertRaises(DeploymentError):
            assert_single_device_assignment(["/dev/ttyACM0", "/dev/ttyACM1"])

    def test_only_one_physical_device_is_required(self) -> None:
        with self.assertRaises(DeploymentError):
            assert_single_device_assignment(
                ["/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_a",
                 "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_b"]
            )

    def test_ready_marker_is_detected_in_text_and_bytes(self) -> None:
        marker = APPLICATIONS["client"]["ready_marker"]
        self.assertTrue(contains_ready_marker(f"I: {marker}", marker))
        self.assertTrue(contains_ready_marker(f"I: {marker}".encode(), marker))
        self.assertFalse(contains_ready_marker("boot failed", marker))

    def test_flash_configuration_resolves_images_inside_build_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_directory = Path(temporary_directory)
            image = build_directory / "bootloader.bin"
            image.write_bytes(b"firmware")
            (build_directory / "flasher_args.json").write_text(
                '{"flash_files":{"0x1000":"bootloader.bin"},'
                '"write_flash_args":["--flash_mode","dio"],'
                '"extra_esptool_args":["--after","hard_reset"]}',
                encoding="utf-8",
            )

            extra_args, flash_args = flash_configuration(build_directory)

        self.assertEqual(extra_args, ["--after", "hard_reset"])
        self.assertEqual(flash_args[0:2], ["--flash_mode", "dio"])
        self.assertEqual(flash_args[2], "0x1000")
        self.assertEqual(Path(flash_args[3]), image)

    def test_flash_configuration_accepts_esp_idf_extra_arguments(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_directory = Path(temporary_directory)
            image = build_directory / "bootloader.bin"
            image.write_bytes(b"firmware")
            (build_directory / "flasher_args.json").write_text(
                '{"flash_files":{"0x1000":"bootloader.bin"},'
                '"extra_esptool_args":{"after":"hard_reset",'
                '"before":"default_reset","stub":true,"chip":"esp32h2"}}',
                encoding="utf-8",
            )

            extra_args, _ = flash_configuration(build_directory, "esp32h2")

        self.assertEqual(
            extra_args,
            ["--before", "default_reset", "--after", "hard_reset"],
        )

    def test_flash_configuration_rejects_mismatched_esp_idf_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_directory = Path(temporary_directory)
            (build_directory / "flasher_args.json").write_text(
                '{"flash_files":{"0x1000":"bootloader.bin"},'
                '"extra_esptool_args":{"after":"hard_reset",'
                '"before":"default_reset","stub":true,"chip":"esp32c6"}}',
                encoding="utf-8",
            )

            with self.assertRaises(DeploymentError):
                flash_configuration(build_directory, "esp32h2")

    def test_extra_esptool_arguments_reject_unknown_options(self) -> None:
        with self.assertRaises(DeploymentError):
            normalize_extra_esptool_args(
                {"after": "hard_reset", "unexpected": "value"},
                Path("flasher_args.json"),
                "esp32h2",
            )

    def test_before_reset_override_replaces_build_mode(self) -> None:
        self.assertEqual(
            override_before_reset(
                ["--before", "default_reset", "--after", "hard_reset"],
                "usb_reset",
            ),
            ["--before", "usb_reset", "--after", "hard_reset"],
        )

    def test_before_reset_override_adds_missing_mode(self) -> None:
        self.assertEqual(
            override_before_reset(["--after", "hard_reset"], "usb_reset"),
            ["--before", "usb_reset", "--after", "hard_reset"],
        )

    def test_flash_configuration_rejects_images_outside_build_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            build_directory = Path(temporary_directory) / "build"
            build_directory.mkdir()
            (build_directory / "flasher_args.json").write_text(
                '{"flash_files":{"0x1000":"../outside.bin"}}',
                encoding="utf-8",
            )

            with self.assertRaises(DeploymentError):
                flash_configuration(build_directory)


if __name__ == "__main__":
    unittest.main()

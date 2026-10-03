from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from deploy_hardware import (  # noqa: E402
    DeploymentError,
    APPLICATIONS,
    application_configuration,
    assert_single_device_assignment,
    contains_ready_marker,
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

    def test_ready_marker_is_detected_in_text_and_bytes(self) -> None:
        marker = APPLICATIONS["client"]["ready_marker"]
        self.assertTrue(contains_ready_marker(f"I: {marker}", marker))
        self.assertTrue(contains_ready_marker(f"I: {marker}".encode(), marker))
        self.assertFalse(contains_ready_marker("boot failed", marker))


if __name__ == "__main__":
    unittest.main()

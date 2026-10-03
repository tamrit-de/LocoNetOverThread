#!/usr/bin/env python3
"""Flash one prepared ESP-IDF application and verify its boot marker."""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path


APPLICATIONS = {
    "client": {
        "target": "esp32h2",
        "ready_marker": "client firmware is running",
    },
    "border-router": {
        "target": "esp32c6",
        "ready_marker": "Border Router ready; AP SSID:",
    },
}
DEFAULT_PORT = "/dev/lnot-board"
DEFAULT_BAUDRATE = 115200


class DeploymentError(RuntimeError):
    """Raised when the runner cannot safely complete a deployment."""


def application_configuration(application: str) -> dict[str, str]:
    try:
        return APPLICATIONS[application]
    except KeyError as error:
        supported = ", ".join(sorted(APPLICATIONS))
        raise DeploymentError(
            f"Unsupported application '{application}'. Choose one of: {supported}."
        ) from error


def canonical_paths(paths: list[str]) -> set[str]:
    """Return unique physical paths for a set of serial aliases."""
    return {os.path.realpath(path) for path in paths}


def assert_single_device_assignment(paths: list[str]) -> str:
    """Reject empty or ambiguous runner assignments."""
    physical_paths = canonical_paths(paths)
    if len(physical_paths) != 1:
        if not physical_paths:
            raise DeploymentError("No serial device was assigned to this runner.")
        raise DeploymentError(
            "More than one physical serial device was assigned: "
            + ", ".join(sorted(physical_paths))
        )
    return next(iter(physical_paths))


def validate_port(port: str) -> Path:
    """Require the configured path to be a character device."""
    device = Path(port)
    try:
        mode = device.stat().st_mode
    except OSError as error:
        raise DeploymentError(f"Cannot access assigned serial device {device}: {error}") from error
    if not stat.S_ISCHR(mode):
        raise DeploymentError(f"Assigned path {device} is not a character device.")
    return device


def discover_candidate_ports(explicit_port: Path) -> list[str]:
    candidates = [str(explicit_port)]
    for pattern in ("/dev/serial/by-id/*", "/dev/ttyACM*", "/dev/ttyUSB*"):
        for candidate in Path("/").glob(pattern.lstrip("/")):
            try:
                if stat.S_ISCHR(candidate.stat().st_mode):
                    candidates.append(str(candidate))
            except OSError:
                continue
    return sorted(set(candidates))


def contains_ready_marker(line: bytes | str, marker: str) -> bool:
    if isinstance(line, bytes):
        line = line.decode("utf-8", errors="replace")
    return marker in line


def flash(project: Path, port: Path, expected_target: str) -> None:
    idf = shutil.which("idf.py")
    if idf is None:
        raise DeploymentError(
            "idf.py was not found. Source the native ESP-IDF export.sh before deploying."
        )

    environment = os.environ.copy()
    environment["IDF_TARGET"] = expected_target
    subprocess.run(
        [idf, "-C", str(project), "-p", str(port), "flash"],
        check=True,
        env=environment,
    )


def wait_for_ready(port: Path, marker: str, timeout_seconds: float) -> None:
    try:
        import serial
    except ImportError as error:
        raise DeploymentError(
            "pyserial is unavailable. Source the native ESP-IDF environment."
        ) from error

    deadline = time.monotonic() + timeout_seconds
    connection = None
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            connection = serial.Serial(
                str(port), DEFAULT_BAUDRATE, timeout=min(1.0, max(0.1, deadline - time.monotonic()))
            )
            break
        except serial.SerialException as error:
            last_error = error
            time.sleep(0.5)
    if connection is None:
        raise DeploymentError(f"Could not open {port} for verification: {last_error}")

    try:
        with connection:
            # Toggle the standard ESP auto-reset lines so verification always observes
            # the current boot, even when flashing completed before the port opened.
            connection.dtr = False
            connection.rts = True
            time.sleep(0.1)
            connection.rts = False
            connection.dtr = True

            while time.monotonic() < deadline:
                line = connection.readline()
                if line:
                    decoded = line.decode("utf-8", errors="replace").rstrip()
                    print(decoded, flush=True)
                    if contains_ready_marker(decoded, marker):
                        return
    except serial.SerialException as error:
        raise DeploymentError(f"Serial verification failed on {port}: {error}") from error

    raise DeploymentError(f"Timed out waiting for firmware ready marker: {marker!r}")


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--application", required=True, choices=sorted(APPLICATIONS))
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv or sys.argv[1:])
    try:
        configuration = application_configuration(arguments.application)
        if arguments.timeout_seconds <= 0:
            raise DeploymentError("--timeout-seconds must be greater than zero.")
        port = validate_port(arguments.port)
        assigned_devices = discover_candidate_ports(port)
        assigned_physical_path = assert_single_device_assignment(assigned_devices)
        if os.path.realpath(port) != assigned_physical_path:
            raise DeploymentError(
                f"Configured port {port} does not resolve to the sole assigned device "
                f"{assigned_physical_path}."
            )
        project = Path(__file__).resolve().parents[1] / "apps" / arguments.application
        build_directory = project / "build"
        if not build_directory.is_dir():
            raise DeploymentError(f"Firmware build artifact is missing: {build_directory}")
        print(
            f"Deploying {arguments.application} ({configuration['target']}) "
            f"to {port}."
        )
        flash(project, port, configuration["target"])
        wait_for_ready(port, configuration["ready_marker"], arguments.timeout_seconds)
        print("Hardware deployment verified.", flush=True)
        return 0
    except (DeploymentError, OSError, subprocess.CalledProcessError) as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

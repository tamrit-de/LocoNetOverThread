from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
COMMON_COMPONENT = REPOSITORY_ROOT / "components" / "lnot_common"
TESTS = {
    "test_device_role": (
        COMMON_COMPONENT / "lnot_device_role.c",
        REPOSITORY_ROOT / "tests" / "lnot_common" / "test_device_role.c",
    ),
    "test_wifi_identity": (
        COMMON_COMPONENT / "lnot_wifi_identity.c",
        REPOSITORY_ROOT / "tests" / "lnot_common" / "test_wifi_identity.c",
    ),
}


def main() -> None:
    compiler = os.environ.get("CC", "cc")
    compiler_path = shutil.which(compiler)
    if compiler_path is None:
        raise SystemExit(
            f"C compiler '{compiler}' was not found. Set CC to a C11-capable compiler."
        )

    with tempfile.TemporaryDirectory() as temporary_directory:
        for name, sources in TESTS.items():
            executable = Path(temporary_directory) / name
            if os.name == "nt":
                executable = executable.with_suffix(".exe")

            subprocess.run(
                [
                    compiler_path,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{COMMON_COMPONENT / 'include'}",
                    *(str(source) for source in sources),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()

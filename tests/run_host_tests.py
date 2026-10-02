from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
COMMON_COMPONENT = REPOSITORY_ROOT / "components" / "lnot_common"
TEST_SOURCE = REPOSITORY_ROOT / "tests" / "lnot_common" / "test_device_role.c"


def main() -> None:
    compiler = os.environ.get("CC", "cc")
    compiler_path = shutil.which(compiler)
    if compiler_path is None:
        raise SystemExit(
            f"C compiler '{compiler}' was not found. Set CC to a C11-capable compiler."
        )

    with tempfile.TemporaryDirectory() as temporary_directory:
        executable = Path(temporary_directory) / "test_device_role"
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
                str(COMMON_COMPONENT / "lnot_device_role.c"),
                str(TEST_SOURCE),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()

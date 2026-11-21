"""Build real native C++23 regression programs exclusively with Windows MSVC.

The C++ programs and original/published curriculum stay unchanged. CMake/Ninja
and the pinned full application have their separate build contracts.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil


def windows_executable(path: str | Path) -> Path:
    result = Path(path)
    return result if result.suffix.lower() == ".exe" else Path(str(result) + ".exe")


def msvc_cpp23_command(executable: str | Path, *sources: str | Path,
                        include: str | Path | None = None) -> list[str]:
    if os.name != "nt" or not (shutil.which("cl.exe") or shutil.which("cl")):
        raise RuntimeError("Windows MSVC C++23 is required for this native QA execution")
    if not sources:
        raise ValueError("Specify at least one original C++ translation unit")
    binary = windows_executable(executable)
    binary.parent.mkdir(parents=True, exist_ok=True)
    command = ["cl", "/nologo", "/std:c++latest", "/EHsc", "/Od",
               "/W4", "/permissive-", "/Zc:__cplusplus", "/utf-8"]
    if include is not None:
        command += ["/I", str(include)]
    command += [str(path) for path in sources]
    command.append("/Fe:" + str(binary))
    if len(sources) == 1:
        command.append("/Fo:" + str(binary.with_suffix(".obj")))
    else:
        command.append("/Fo:" + str(binary.parent) + os.sep)
    return command

"""Reproduce the U4L3 CMake preparation lesson on an isolated Windows runner.

This executes the actual lesson CMakeLists/main.cpp twice with MSVC and Ninja:
configure, build, run, edit source, run stale binary, rebuild, run new binary.
It NEVER installs into the student's E: drive or certifies standalone AssetCheck.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/"
    "programming_practice_tools/proyecto_assetcheck/"
    "lesson_preparar_y_comprobar_cmake_en_windows.json"
)


def command(args: list[str], *, env: dict[str, str], timeout: int = 120) -> str:
    print("+ " + " ".join(args), flush=True)
    result = subprocess.run(
        args, env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=timeout, check=False,
    )
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n", flush=True)
    if result.stderr:
        print(result.stderr, file=sys.stderr, flush=True)
    if result.returncode != 0:
        raise RuntimeError(f"Exit {result.returncode}: {args[0]}")
    return result.stdout


def lesson_sources() -> tuple[str, str, str]:
    obj = json.loads(LESSON.read_text(encoding="utf-8"))
    blocks = [
        b for page in obj["pages"] for b in page["blocks"]
        if b.get("type") == "code"
    ]

    def unique(language: str, marker: str) -> str:
        matches = [
            b["code"] for b in blocks
            if b.get("language") == language and marker in b["code"]
        ]
        if len(matches) != 1:
            raise ValueError(
                f"Expected one unmodified U4L3 code example: {marker!r}; "
                f"found {len(matches)}"
            )
        return matches[0]

    cmakelists = unique("cmake", "project(PreparacionAssetCheck LANGUAGES CXX)")
    original = unique("cpp", 'std::puts("AssetCheck: entorno listo")')
    updated = unique("cpp", 'std::puts("AssetCheck: entorno actualizado")')
    if cmakelists.count("add_executable(preparar_entorno main.cpp)") != 1:
        raise ValueError("Unexpected target in curricular CMakeLists.txt")
    if original == updated:
        raise ValueError("U4L3 edit must actually change source")
    return cmakelists, original, updated


def msvc_x64_environment() -> tuple[dict[str, str], str]:
    if platform.system() != "Windows":
        raise RuntimeError("This recipe is for a real Windows MSVC runner")
    installer = Path(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    ) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not installer.is_file():
        raise FileNotFoundError(f"Visual Studio locator missing: {installer}")
    proc = subprocess.run(
        [
            str(installer), "-latest", "-version", "[18.0,19.0)",
            "-products", "*", "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ],
        capture_output=True, text=True, timeout=40, check=False,
    )
    if proc.returncode != 0 or not proc.stdout.strip():
        raise RuntimeError(
            f"No Visual Studio 2026 installation with C++ x64: {proc.stderr}"
        )
    install = Path(proc.stdout.strip())
    vcvars = install / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    if not vcvars.is_file():
        raise FileNotFoundError(f"Missing vcvarsall.bat: {vcvars}")
    # Avoid cmd.exe /S /C's quote stripping on nested, spaced VS paths.
    # A disposable .cmd file preserves the exact vcvarsall.bat path and does
    # not modify the runner's installed tools or the student's environment.
    with tempfile.TemporaryDirectory(prefix="assetcheck-msvc-env-") as scratch:
        activate = Path(scratch) / "activate.cmd"
        activate.write_text(
            '@echo off\n'
            + f'call "{vcvars}" x64 >nul\n'
            + 'if errorlevel 1 exit /b 1\n'
            + 'set\n',
            encoding="utf-8",
        )
        proc = subprocess.run(
            ["cmd.exe", "/d", "/c", str(activate)],
            capture_output=True, text=True, errors="replace",
            timeout=90, check=False,
        )
    if proc.returncode != 0:
        raise RuntimeError(
            f"Could not initialize x64 MSVC: {proc.stdout}\n{proc.stderr}"
        )
    env = os.environ.copy()
    for line in proc.stdout.splitlines():
        if "=" in line:
            name, value = line.split("=", 1)
            if name and not name.startswith("="):
                env[name] = value
    target = env.get("VSCMD_ARG_TGT_ARCH", "").lower()
    host = env.get("VSCMD_ARG_HOST_ARCH", "").lower()
    if target not in ("x64", "amd64") or host not in ("x64", "amd64"):
        raise RuntimeError(f"Expected x64 host and target, found {host}/{target}")
    cl = shutil.which("cl.exe", path=env.get("PATH"))
    if not cl:
        raise RuntimeError("cl.exe was not exposed by vcvarsall")
    print(f"Verified developer environment: host={host}, target={target}")
    print(f"cl.exe: {cl}")
    return env, cl


def read_binary(exe: Path) -> tuple[int, str, str]:
    if not exe.is_file():
        raise FileNotFoundError(f"Expected built executable: {exe}")
    proc = subprocess.run(
        [str(exe)], capture_output=True, text=True, encoding="utf-8",
        errors="replace", timeout=15, check=False,
    )
    return proc.returncode, proc.stdout, proc.stderr


def assert_output(exe: Path, label: str, expected: str) -> None:
    code, out, err = read_binary(exe)
    if code != 0 or err or out != expected:
        raise AssertionError(
            f"{label}: exit={code}, stderr={err!r}, "
            f"expected={expected!r}, actual={out!r}"
        )
    print(f"PASS {label}: {out.strip()} (exit 0)", flush=True)


def check(report: Path | None) -> None:
    cmake_source, original, modified = lesson_sources()
    env, cl = msvc_x64_environment()
    cmake = shutil.which("cmake.exe", path=env.get("PATH"))
    ninja = shutil.which("ninja.exe", path=env.get("PATH"))
    if not cmake or not ninja:
        raise RuntimeError("The Windows runner needs CMake and Ninja on PATH")
    cmake_version = command([cmake, "--version"], env=env).splitlines()[0]
    ninja_version = command([ninja, "--version"], env=env).strip()
    with tempfile.TemporaryDirectory(prefix="assetcheck-u4l3-msvc-") as tmp:
        source_dir = Path(tmp) / "01_prueba_cmake"
        build_dir = source_dir / "build"
        source_dir.mkdir()
        (source_dir / "CMakeLists.txt").write_text(cmake_source, encoding="utf-8")
        main = source_dir / "main.cpp"
        main.write_text(original, encoding="utf-8")

        config = command(
            [
                cmake, "-S", str(source_dir), "-B", str(build_dir),
                "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
            ],
            env=env, timeout=180,
        )
        if not re.search(r"Configuring done", config) or not re.search(
            r"Generating done", config
        ):
            raise AssertionError("Configuration/generation evidence missing")
        if not re.search(r"CXX compiler identification is MSVC", config):
            raise AssertionError("Expected real MSVC compiler in CMake log")
        cache = build_dir / "CMakeCache.txt"
        if not cache.is_file() or not (build_dir / "build.ninja").is_file():
            raise AssertionError("Missing CMake configuration products")
        if "CMAKE_BUILD_TYPE:STRING=Debug" not in cache.read_text(
            encoding="utf-8", errors="replace"
        ):
            raise AssertionError("Ninja Debug configuration not selected")

        exe = build_dir / "preparar_entorno.exe"
        command(
            [cmake, "--build", str(build_dir), "--target", "preparar_entorno"],
            env=env, timeout=180,
        )
        assert_output(exe, "initial-build", "AssetCheck: entorno listo\n")
        main.write_text(modified, encoding="utf-8")
        assert_output(exe, "stale-binary-before-rebuild", "AssetCheck: entorno listo\n")
        command(
            [cmake, "--build", str(build_dir), "--target", "preparar_entorno"],
            env=env, timeout=180,
        )
        assert_output(exe, "updated-after-rebuild", "AssetCheck: entorno actualizado\n")

    summary = {
        "scope": "U4L3 isolated CMake reconstruction only; not student E: drive or standalone AssetCheck",
        "runner": platform.platform(),
        "generator": "Ninja",
        "configuration": "Debug",
        "toolchain": "MSVC x64",
        "cl_path": cl,
        "cmake_version": cmake_version,
        "ninja_version": ninja_version,
        "configure_and_generate": "passed",
        "initial_build_and_run": "passed",
        "stale_binary_before_rebuild": "confirmed",
        "edited_source_rebuilt_and_run": "passed",
        "native_screenshots": "not collected",
        "student_manual_reconstruction": "not performed",
    }
    print("SUMMARY " + json.dumps(summary, ensure_ascii=False), flush=True)
    if report is not None:
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(
            json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        check(args.report)
    except (OSError, RuntimeError, ValueError, AssertionError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

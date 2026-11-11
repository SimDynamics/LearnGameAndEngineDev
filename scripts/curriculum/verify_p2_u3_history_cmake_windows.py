"""Real MSVC/Ninja reconstruction of the published U3 selective-recovery exercise.

The two project states come from the CURRENT authored cpp_compiler block, not
from a substitute CMake example. A temporary byte-exact snapshot represents
the recorded base. Its restoration is a QA simulation, NOT proof of using the
student's Win32 History UI, which is tested independently.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from verify_assetcheck_cmake_preparation_windows import msvc_x64_environment

ROOT = Path(__file__).resolve().parents[2]
LESSON = (ROOT / "app/src/main/assets/content/es/programming"
          / "programming_practice_tools/comprobaciones_repetibles"
          / "lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
LESSON_ID = ("programming_practice_tools__comprobaciones_repetibles"
             "__reorganizar_la_descripcion_conservando_el_comprobador")
OWNER = "block_0010_0003"
ROOT_CMAKE = "CMakeLists.txt"
LIBRARY_SOURCE = "componente/src/cajas.cpp"
RUN_TARGET = "comprobar_cajas"
ORIGINAL_EXPR = "return (piezas + 3) / 4;"
BROKEN_EXPR = "return piezas / 4;"
QA_COMMENT = "// QA: conservar esta modificación independiente tras la recuperación.\n"
CASES = ((0, 0), (4, 1), (8, 2), (20, 5), (1, 1), (5, 2), (17, 5))


def sha(data: str) -> str:
    return hashlib.sha256(data.encode("utf-8")).hexdigest()


def oracle(broken: bool) -> str:
    lines = []
    for pieces, expected in CASES:
        actual = pieces // 4 if broken else (pieces + 3) // 4
        lines.append(
            f"piezas={pieces} esperado={expected} obtenido={actual} "
            f"{'OK' if actual == expected else 'FALLO'}\n"
        )
    lines.append("Bateria: FALLO\n" if broken else "Bateria: OK\n")
    return "".join(lines)


def published_project() -> tuple[dict[str, str], str]:
    doc = json.loads(LESSON.read_text(encoding="utf-8"))
    if doc.get("id") != LESSON_ID:
        raise ValueError("Published U3 lesson identity changed")
    owners = []
    for page, metadata in zip(doc["pages"], doc["stateLayout"]["current"]):
        for block, stable in zip(page["blocks"], metadata["blocks"]):
            if stable == OWNER:
                owners.append(block)
    if len(owners) != 1 or owners[0].get("type") != "cpp_compiler":
        raise ValueError("The selected historical editor must exist exactly once")
    editor = owners[0]
    if editor.get("cmake", {}).get("target") != RUN_TARGET:
        raise ValueError("The historical editor changed its CMake target")
    files = editor.get("projectFiles")
    if not isinstance(files, list) or len(files) != 6:
        raise ValueError("The published project must retain its six files")
    project: dict[str, str] = {}
    for item in files:
        path, source = item["path"], item["initialCode"]
        if (path in project or path.startswith("/") or "\\" in path
                or any(part in ("", ".", "..") for part in path.split("/"))
                or not isinstance(source, str)):
            raise ValueError("Unsafe or repeated published project path")
        project[path] = source
    if ROOT_CMAKE not in project or LIBRARY_SOURCE not in project:
        raise ValueError("Missing authored CMake root or library source")
    if project[LIBRARY_SOURCE].count(ORIGINAL_EXPR) != 1:
        raise ValueError("The published source formula no longer matches the task")
    expected = editor.get("outputCheck", {}).get("cases", [])
    if (len(expected) != 1 or expected[0].get("standardInput") != ""
            or expected[0].get("outputs") != [oracle(False)]):
        raise ValueError("The published success case differs from the independent oracle")
    if "add_executable(comprobar_cajas pruebas/cajas_casos.cpp)" not in project[ROOT_CMAKE]:
        raise ValueError("The original CMake target is not built from the shared tests")
    return project, expected[0]["outputs"][0]


def stages(base: dict[str, str]) -> tuple[dict[str, str], dict[str, str]]:
    """Simulate edits and recovery in memory, without touching user files."""
    failed = dict(base)
    failed[ROOT_CMAKE] += QA_COMMENT
    failed[LIBRARY_SOURCE] = failed[LIBRARY_SOURCE].replace(ORIGINAL_EXPR, BROKEN_EXPR)
    if failed[LIBRARY_SOURCE] == base[LIBRARY_SOURCE]:
        raise ValueError("The known defect was not introduced")
    if {path for path in base if base[path] != failed[path]} != {ROOT_CMAKE, LIBRARY_SOURCE}:
        raise ValueError("The simulated defect touched unrelated files")
    repaired = dict(failed)
    repaired[LIBRARY_SOURCE] = base[LIBRARY_SOURCE]
    if (repaired[ROOT_CMAKE] != failed[ROOT_CMAKE]
            or any(repaired[p] != failed[p] for p in failed if p != LIBRARY_SOURCE)
            or repaired[LIBRARY_SOURCE] != base[LIBRARY_SOURCE]):
        raise ValueError("Recovery failed to conserve the unrelated CMake draft")
    return failed, repaired


def run_tool(args: list[str], env: dict[str, str], *, timeout: int = 240) -> str:
    proc = subprocess.run(args, env=env, capture_output=True, text=True,
                          encoding="utf-8", errors="replace",
                          timeout=timeout, check=False)
    if proc.returncode:
        raise RuntimeError(
            f"Tool failed ({proc.returncode}): {' '.join(args)}\n"
            + proc.stdout[-2500:] + proc.stderr[-2500:]
        )
    return proc.stdout


def write_project(directory: Path, files: dict[str, str]) -> None:
    for relative, text in files.items():
        destination = directory / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(text.encode("utf-8"))


def execute(executable: Path, expected: str, exit_code: int) -> dict:
    if not executable.is_file():
        raise FileNotFoundError(f"No executable built: {executable}")
    p = subprocess.run([str(executable)], capture_output=True, text=True,
                       encoding="utf-8", errors="strict", timeout=20, check=False)
    if (p.returncode != exit_code or p.stdout != expected or p.stderr):
        raise AssertionError(
            f"Wrong observed program result: exit={p.returncode}, "
            f"stdout={p.stdout!r}, stderr={p.stderr!r}"
        )
    return {
        "exit_code": p.returncode,
        "stdout_sha256": sha(p.stdout),
        "stdout_lines": len(p.stdout.splitlines()),
        "stderr_empty": True,
    }


def verify(report: Path | None) -> None:
    if platform.system() != "Windows":
        raise RuntimeError("This verification requires a real Windows x64 runner")
    base, success = published_project()
    failed, restored = stages(base)
    env, cl = msvc_x64_environment()
    cmake = shutil.which("cmake.exe", path=env.get("PATH"))
    ninja = shutil.which("ninja.exe", path=env.get("PATH"))
    if not cmake or not ninja:
        raise RuntimeError("CMake and Ninja must be installed in the real MSVC environment")
    cmake_version = run_tool([cmake, "--version"], env).splitlines()[0]
    ninja_version = run_tool([ninja, "--version"], env).strip()
    with tempfile.TemporaryDirectory(prefix="lgaed-u3-history-qa-") as scratch:
        root = Path(scratch) / "source"
        build = Path(scratch) / "build"
        root.mkdir()
        write_project(root, failed)
        configured = run_tool([
            cmake, "-S", str(root), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug",
        ], env)
        if ("Configuring done" not in configured
                or "Generating done" not in configured
                or "MSVC" not in configured):
            raise AssertionError("Ninja/MSVC configure and generation not proven")
        run_tool([cmake, "--build", str(build), "--target", RUN_TARGET], env)
        exe = build / (RUN_TARGET + ".exe")
        broken = execute(exe, oracle(True), 1)
        if (root / ROOT_CMAKE).read_bytes() != failed[ROOT_CMAKE].encode():
            raise AssertionError("First real build mutated the independent CMake comment")

        # Recovery is exactly ONE file. Preserve the other files and the build
        # directory without cleaning or reconstructing an unrelated project.
        (root / LIBRARY_SOURCE).write_bytes(base[LIBRARY_SOURCE].encode("utf-8"))
        for path, source in restored.items():
            if (root / path).read_bytes() != source.encode("utf-8"):
                raise AssertionError(f"Selective recovery changed an unrelated file: {path}")
        run_tool([cmake, "--build", str(build), "--target", RUN_TARGET], env)
        correct = execute(exe, success, 0)
        if broken["stdout_sha256"] == correct["stdout_sha256"]:
            raise AssertionError("The failed and restored runs supplied identical evidence")

    result = {
        "scope": "Published U3L2 history editor: isolated actual MSVC/Ninja CMake runs; no student UI event",
        "owner": OWNER,
        "lesson_id": LESSON_ID,
        "project_files": len(base),
        "changed_paths_before_restore": [ROOT_CMAKE, LIBRARY_SOURCE],
        "restored_path": LIBRARY_SOURCE,
        "independent_cmake_comment_preserved": True,
        "revision_source_sha256": {p: sha(base[p]) for p in (ROOT_CMAKE, LIBRARY_SOURCE)},
        "failed_source_sha256": {p: sha(failed[p]) for p in (ROOT_CMAKE, LIBRARY_SOURCE)},
        "restored_source_sha256": {p: sha(restored[p]) for p in (ROOT_CMAKE, LIBRARY_SOURCE)},
        "original_source_other_files_preserved": all(
            failed[p] == restored[p] == base[p] for p in base if p not in (ROOT_CMAKE, LIBRARY_SOURCE)
        ),
        "failed_run": broken,
        "restored_run": correct,
        "toolchain": "MSVC x64",
        "compiler_path": cl,
        "cmake_version": cmake_version,
        "ninja_version": ninja_version,
        "student_history_actions": "not performed in this isolated CMake test",
        "curricular_credit": False,
    }
    print("PASS U3 historical bad build: three failures, normal exit 1 and empty stderr", flush=True)
    print("PASS U3 restored source: seven matches, normal exit 0 and empty stderr", flush=True)
    print("PASS same CMake draft comment survives source-only recovery", flush=True)
    if report is not None:
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=None)
    args = parser.parse_args()
    try:
        verify(args.report)
    except (OSError, RuntimeError, AssertionError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

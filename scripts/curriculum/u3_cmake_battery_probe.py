"""Compiled CMake evidence for the two original Programming 2 U3L1 batteries.

This isolated quality-assurance probe checks real project files, builds both
consumers, and mutates the shared library for each declared input. It does
not grant lesson completion or replace the Windows editor's own receipts.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
from typing import Mapping

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_bateria_como_segundo_consumidor.json"
)
TRACE = "LGAED_U3_SHARED_CALL:"


@dataclass(frozen=True)
class Scenario:
    owner: str
    library: str
    test_file: str
    client_target: str
    test_target: str
    original_return: str
    correct_expression: str
    values: tuple[tuple[int, int], ...]
    broken_original: bool


ENVios = Scenario(
    owner="block_0004_0003",
    library="src/cajas.cpp",
    test_file="pruebas/cajas_casos.cpp",
    client_target="consultar_cajas",
    test_target="comprobar_cajas",
    original_return="    return piezas / 4;",
    correct_expression="(piezas + 3) / 4",
    values=((0, 0), (4, 1), (8, 2), (20, 5), (1, 1), (5, 2), (17, 5)),
    broken_original=True,
)
LOTES = Scenario(
    owner="block_0010_0003",
    library="motor/cantidad.cpp",
    test_file="pruebas/lotes_casos.cpp",
    client_target="consultar_lotes",
    test_target="verificar_lotes",
    original_return="    return (piezas + 5) / 6;",
    correct_expression="(piezas + 5) / 6",
    values=((0, 0), (6, 1), (7, 2), (18, 3)),
    broken_original=False,
)


@dataclass(frozen=True)
class Result:
    accepted: bool
    reason: str
    phases: tuple[tuple[str, int], ...] = ()
    observed: tuple[int, ...] = ()


def published_files(scenario: Scenario) -> dict[str, str]:
    doc = json.loads(LESSON.read_text(encoding="utf-8"))
    for page_index, layout in enumerate(doc["stateLayout"]["current"]):
        for slot, identity in enumerate(layout["blocks"]):
            if identity != scenario.owner:
                continue
            editor = doc["pages"][page_index]["blocks"][slot]
            if editor.get("type") != "cpp_compiler" or not editor.get("cmake"):
                raise ValueError("Original curricular owner is not a CMake editor")
            files = {item["path"]: item["initialCode"]
                     for item in editor["projectFiles"]}
            if len(files) != len(editor["projectFiles"]):
                raise ValueError("Duplicate curricular project path")
            for key in files:
                parts = PurePosixPath(key).parts
                if (key.startswith("/") or "\\" in key or
                    ".." in parts or "." in parts or not parts):
                    raise ValueError("Unsafe original project path")
            if (scenario.library not in files or scenario.test_file not in files or
                "CMakeLists.txt" not in files):
                raise ValueError("A required original project file is missing")
            if files[scenario.library].count(scenario.original_return) != 1:
                raise ValueError("Published library contract changed")
            return files
    raise ValueError("Original CMake exercise owner was not found")


def _check_candidate(scenario: Scenario, candidate: Mapping[str, str]) -> dict[str, str]:
    reference = published_files(scenario)
    if set(candidate) != set(reference):
        raise ValueError("The candidate changed the set of project files")
    for path, original in reference.items():
        if path != scenario.test_file and candidate[path] != original:
            raise ValueError("The shared library, client or CMake contract was changed")
    return dict(candidate)


def _instrument(scenario: Scenario, source: str, phase: str,
                case: int | None = None) -> str:
    if source.count(scenario.original_return) != 1:
        raise ValueError("The original library was not preserved")
    if phase not in ("correct", "mutant", "original-bug"):
        raise ValueError("Unknown CMake battery phase")
    if phase == "original-bug" and not scenario.broken_original:
        raise ValueError("This library has no published bug")
    if phase == "mutant":
        if type(case) is not int or case not in (value for value, _ in scenario.values):
            raise ValueError("Mutation must identify one declared input")
        expr = f"(piezas == {case} ? ({scenario.correct_expression}) + 1 : ({scenario.correct_expression}))"
    else:
        if case is not None:
            raise ValueError("Only a mutant identifies an input")
        expr = ("piezas / 4" if phase == "original-bug"
                else scenario.correct_expression)
    replacement = (
        f'    std::cerr << "{TRACE}" << piezas << std::endl;\n'
        f"    return {expr};"
    )
    return "#include <iostream>\n" + source.replace(scenario.original_return, replacement)


def _command(args: list[str], cwd: Path, env: dict[str, str],
             timeout: int = 90) -> subprocess.CompletedProcess[str]:
    response = subprocess.run(args, cwd=cwd, env=env, capture_output=True,
                              text=True, errors="replace", timeout=timeout)
    if response.returncode != 0:
        output = (response.stderr + "\n" + response.stdout)[-3500:]
        raise ValueError("Command failed: " + " ".join(args) + "\n" + output)
    return response


def _binary(build: Path, target: str) -> Path:
    return build / (target + ".exe" if sys.platform == "win32" else target)


def _run(binary: Path) -> tuple[int, str, list[int]]:
    run = subprocess.run([str(binary)], cwd=binary.parent,
                         capture_output=True, text=True, errors="replace", timeout=12)
    observed: list[int] = []
    for line in run.stderr.splitlines():
        if line.startswith(TRACE):
            observed.append(int(line[len(TRACE):]))
    return run.returncode, run.stdout, observed


def audit(scenario: Scenario, submitted: Mapping[str, str], cxx: str) -> Result:
    """Require all library calls and a verdict sensitive to every single case."""
    try:
        files = _check_candidate(scenario, submitted)
        with tempfile.TemporaryDirectory(prefix="lgaed-u3-cmake-battery-") as tmp:
            source_dir = Path(tmp) / "source"
            build_dir = Path(tmp) / "build"
            for name, body in files.items():
                location = source_dir.joinpath(*PurePosixPath(name).parts)
                location.parent.mkdir(parents=True, exist_ok=True)
                location.write_text(body, encoding="utf-8")
            env = os.environ.copy()
            env["CXX"] = cxx
            _command(["cmake", "-S", str(source_dir), "-B", str(build_dir),
                      "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug"], source_dir, env)
            # Both real CMake consumers must be available in the original graph.
            _command(["cmake", "--build", str(build_dir), "--target",
                      scenario.client_target], source_dir, env)
            client_status, client_stdout, _ = _run(
                _binary(build_dir, scenario.client_target))
            if client_status != 0 or client_stdout.strip() != "1:2":
                return Result(False, "The original client did not run successfully")

            source = files[scenario.library]
            library_path = source_dir.joinpath(*PurePosixPath(scenario.library).parts)
            phases = [("correct", None)]
            phases.extend((f"mutant-{value}", value) for value, _ in scenario.values)
            if scenario.broken_original:
                phases.append(("original-bug", None))
            measured = []
            for phase, value in phases:
                mode = "mutant" if value is not None else phase
                instrumented = _instrument(scenario, source, mode, value)
                library_path.write_text(instrumented, encoding="utf-8")
                # Force the library and its test consumer to rebuild. A stale
                # executable must never count as a fresh mutation result.
                _command(["cmake", "--build", str(build_dir), "--target",
                          scenario.test_target, "--clean-first"], source_dir, env)
                # Do not leave the private implementation on disk when main runs.
                library_path.write_text(source, encoding="utf-8")
                code, _, calls = _run(_binary(build_dir, scenario.test_target))
                measured.append((phase, code))
                if Counter(calls) != Counter(value for value, _ in scenario.values):
                    return Result(False, phase + ": missing, repeated or skipped library calls",
                                  tuple(measured), tuple(calls))
                expected = 0 if phase == "correct" else 1
                if code != expected:
                    return Result(False, phase + ": exit status did not depend on all comparisons",
                                  tuple(measured), tuple(calls))
            return Result(True, "Both CMake targets ran; all independent mutations were detected",
                          tuple(measured), tuple(value for value, _ in scenario.values))
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        return Result(False, "Unable to verify compiled CMake battery: " + str(error))


def envios_candidate() -> dict[str, str]:
    files = published_files(ENVios)
    target = files[ENVios.test_file]
    anchor = "    if (!correcto) {\n"
    if target.count(anchor) != 1:
        raise ValueError("Published four-case battery changed")
    additions = "".join(
        f"    if (!comprobar({value}, {expected})) {{ correcto = false; }}\n"
        for value, expected in ENVios.values[4:]
    )
    files[ENVios.test_file] = target.replace(anchor, additions + anchor)
    return files


def lotes_candidate() -> dict[str, str]:
    files = published_files(LOTES)
    target = files[LOTES.test_file]
    old = """int main() {
    // Escribe aqui tus comprobaciones antes de consultar la referencia.
    return 0;
}
"""
    replacement = """bool comprobar(int piezas, int esperado) {
    const int obtenido = lotes_para(piezas);
    const bool coincide = obtenido == esperado;
    std::cout << piezas << ": " << obtenido << "/" << esperado
              << (coincide ? " OK" : " FALLO") << std::endl;
    return coincide;
}

int main() {
    bool correcto = true;
    if (!comprobar(0, 0)) correcto = false;
    if (!comprobar(6, 1)) correcto = false;
    if (!comprobar(7, 2)) correcto = false;
    if (!comprobar(18, 3)) correcto = false;
    return correcto ? 0 : 1;
}
"""
    if target.count(old) != 1:
        raise ValueError("Published empty lotes battery changed")
    files[LOTES.test_file] = target.replace(old, replacement)
    return files

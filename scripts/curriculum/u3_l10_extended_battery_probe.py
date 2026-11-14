"""Independent CMake/mutation QA for the original Programming 2 U3L10 page 7.

A future in-app evaluator must bind its own evidence to the student's revision.
This isolated regression does not edit published lessons or grant progress.
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
LESSON = ROOT / ("app/src/main/assets/content/es/programming/programming_practice_tools/"
                 "comprobaciones_repetibles/lesson_integrar_un_proyecto_y_sostener_su_evidencia.json")
OWNER = "block_0006_0011"
LIBRARY = "src/ajuste.cpp"
BATTERY = "pruebas/casos.cpp"
FUNCTION = "bool intentar_ajuste(int valor, int inferior, int superior, int& ajuste) {"
TRACE = "LGAED_U3L10_CALL:"
ORIGINAL = (
    (2, 3, 7, 77), (5, 3, 7, -8), (9, 3, 7, 19),
    (4, 4, 4, -4), (-20, 20, 20, 8), (20, -20, -20, 17),
    (0, 7, 3, 77), (21, 3, 7, -8), (-21, 3, 7, 19),
    (0, -21, 7, -4), (0, 3, 21, 8),
)


@dataclass(frozen=True)
class Audit:
    accepted: bool
    reason: str
    new_call: tuple[int, int, int, int] | None = None
    phases: tuple[tuple[str, int], ...] = ()


def published_files() -> dict[str, str]:
    doc = json.loads(LESSON.read_text(encoding="utf-8"))
    for page, record in zip(doc["pages"], doc["stateLayout"]["current"]):
        for block, identity in zip(page["blocks"], record["blocks"]):
            if identity != OWNER:
                continue
            if block.get("type") != "cpp_compiler" or not block.get("cmake"):
                raise ValueError("Original owner is not a CMake editor")
            files = {entry["path"]: entry["initialCode"] for entry in block["projectFiles"]}
            required = {"CMakeLists.txt", "publico/control/ajuste.h", LIBRARY,
                        "app/consulta.cpp", BATTERY}
            if len(files) != len(block["projectFiles"]) or set(files) != required:
                raise ValueError("Original CMake files changed")
            for name in files:
                parts = PurePosixPath(name).parts
                if name.startswith("/") or "\\" in name or any(part in ("", ".", "..")
                                                              for part in parts):
                    raise ValueError("Unsafe CMake file path")
            if files[LIBRARY].count(FUNCTION) != 1:
                raise ValueError("Published shared function changed")
            return files
    raise ValueError("Original U3L10 owner not found")


def check_files(candidate: Mapping[str, str]) -> dict[str, str]:
    original = published_files()
    if set(candidate) != set(original):
        raise ValueError("Project added or removed historical files")
    for name, source in original.items():
        if not isinstance(candidate[name], str):
            raise ValueError("Nontext project source")
        if name != BATTERY and candidate[name] != source:
            raise ValueError("Shared library, client, header or CMake contract changed")
    return dict(candidate)


def instrument(library: str, phase: str,
               target: tuple[int, int, int, int] | None = None) -> str:
    if library.count(FUNCTION) != 1:
        raise ValueError("Cannot instrument original library")
    if phase not in ("baseline", "flip", "destination") or (
            (phase == "baseline") != (target is None)):
        raise ValueError("Invalid private mutation")
    if target is not None and (type(target) is not tuple or len(target) != 4 or
                               any(type(x) is not int or x < -(2**31) or x >= 2**31
                                   for x in target)):
        raise ValueError("Mutation requires four bounded integers")
    renamed = library.replace(FUNCTION, FUNCTION.replace(
        "intentar_ajuste(", "lgaed_original_intentar_ajuste("), 1)
    guard = "false" if target is None else (
        "(valor == " + str(target[0]) +
        " && inferior == " + str(target[1]) +
        " && superior == " + str(target[2]) +
        " && anterior == " + str(target[3]) + ")")
    mutation = ("if (seleccionado) return !aceptado;" if phase == "flip" else
                "if (seleccionado) ajuste = (ajuste == 0 ? 1 : 0);" if
                phase == "destination" else "")
    return ("#include <iostream>\n" + renamed +
            "\nbool intentar_ajuste(int valor, int inferior, int superior, int& ajuste) {\n"
            "    const int anterior = ajuste;\n"
            '    std::cerr << "' + TRACE + '" << valor << "," << inferior\n'
            '              << "," << superior << "," << anterior << "\\n";\n'
            "    const bool aceptado = lgaed_original_intentar_ajuste(\n"
            "        valor, inferior, superior, ajuste);\n"
            "    const bool seleccionado = " + guard + ";\n"
            "    " + mutation + "\n"
            "    return aceptado;\n}\n")


def command(args: list[str], directory: Path, env: dict[str, str]) -> None:
    result = subprocess.run(args, cwd=directory, env=env, capture_output=True,
                            text=True, errors="replace", timeout=120)
    if result.returncode:
        raise ValueError("Failed " + " ".join(args) + ": " +
                         (result.stderr + result.stdout)[-3000:])


def executable(build: Path, name: str) -> Path:
    return build / (name + ".exe" if sys.platform == "win32" else name)


def execute(exe: Path) -> tuple[int, str, str, tuple[tuple[int, int, int, int], ...]]:
    env = os.environ.copy()
    env.pop("LGAED_U3L10_MUTANT", None)
    done = subprocess.run([str(exe)], cwd=exe.parent, env=env,
                          capture_output=True, text=True,
                          errors="replace", timeout=12)
    calls = []
    for line in done.stderr.splitlines():
        if not line.startswith(TRACE):
            continue
        parts = line[len(TRACE):].split(",")
        if len(parts) != 4:
            raise ValueError("Malformed CMake call witness")
        calls.append(tuple(int(x) for x in parts))
    return done.returncode, done.stdout, done.stderr, tuple(calls)


def audit(candidate: Mapping[str, str], cxx: str) -> Audit:
    """Only a real twelfth case, sensitive to changed results, passes."""
    try:
        files = check_files(candidate)
        with tempfile.TemporaryDirectory(prefix="lgaed-u3l10-") as tmp:
            root = Path(tmp)
            source, build = root / "source", root / "build"
            for name, contents in files.items():
                dest = source.joinpath(*PurePosixPath(name).parts)
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_text(contents, encoding="utf-8")
            env = os.environ.copy()
            env["CXX"] = cxx
            command(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                     "-DCMAKE_BUILD_TYPE=Debug"], source, env)
            command(["cmake", "--build", str(build), "--target", "consultar_ajustes"],
                    source, env)
            client_code, stdout, stderr, _ = execute(executable(build, "consultar_ajustes"))
            if (client_code != 1 or "Aceptadas=4 Rechazadas=2 Suma=-1" not in stdout or
                    stderr.count("Rechazo:") != 2):
                return Audit(False, "The published client did not meet its runtime contract")
            library = source / LIBRARY
            original = files[LIBRARY]
            results: list[tuple[str, int]] = []

            def run_phase(label: str, phase: str,
                          target: tuple[int, int, int, int] | None):
                library.write_text(instrument(original, phase, target),
                                   encoding="utf-8")
                command(["cmake", "--build", str(build), "--target",
                         "comprobar_ajustes", "--clean-first"], source, env)
                # The candidate cannot read the mutant source while executing.
                library.write_text(original, encoding="utf-8")
                code, _, _, calls = execute(executable(build, "comprobar_ajustes"))
                results.append((label, code))
                return code, Counter(calls)

            code, observed = run_phase("baseline", "baseline", None)
            old = Counter(ORIGINAL)
            if code != 0:
                return Audit(False, "The correct shared library did not pass",
                             phases=tuple(results))
            extra = observed - old
            if old - observed or len(extra) != 1 or sum(extra.values()) != 1:
                return Audit(False, "Eleven original cases plus exactly one new call are required",
                             phases=tuple(results))
            new = next(iter(extra))
            if new[:3] in {item[:3] for item in ORIGINAL}:
                return Audit(False, "A new input triple, not a repeated label, is required",
                             new, tuple(results))
            for index, item in enumerate(ORIGINAL, 1):
                status, calls = run_phase("original-" + str(index), "flip", item)
                if status != 1 or calls != observed:
                    return Audit(False, "Original case " + str(index) +
                                 " missed a privately changed result", new, tuple(results))
            for kind in ("flip", "destination"):
                status, calls = run_phase("new-" + kind, kind, new)
                if status != 1 or calls != observed:
                    return Audit(False, "New case did not compare its " + kind +
                                 " result", new, tuple(results))
            return Audit(True, "The eleven retained cases and new one detect all mutations",
                         new, tuple(results))
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        return Audit(False, "Independent CMake evidence unavailable: " + str(error))


def candidate_with_new_case(triple: tuple[int, int, int], initial: int) -> dict[str, str]:
    if triple not in ((3, 3, 7), (7, 3, 7)):
        raise ValueError("Only two published QA fixtures are prepared")
    original = published_files()
    anchor = '    std::cout << "Fallos=" << fallos << "\\n";'
    if original[BATTERY].count(anchor) != 1:
        raise ValueError("The historical eleven-case program changed")
    added = ("    if (!comprobar(12, " +
             ", ".join(str(x) for x in (*triple, initial)) +
             ", true, 0)) { ++fallos; }\n")
    original[BATTERY] = original[BATTERY].replace(anchor, added + anchor)
    return original

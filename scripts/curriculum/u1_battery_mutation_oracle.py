"""Mutation-based evidence for two original Programming 2 U1 C++ batteries.

This isolated CI oracle checks a candidate's own main across independent
behavior mutations compiled in separate executables. It does NOT change lesson assets or grant in-app progress.
The future curricular integration must bind evidence to an actual student
revision and preserve the existing editor identity and stored history.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "evidencia_y_diagnostico/lesson_versiones_del_fuente_y_del_ejecutable.json"
)
FUNCTION = "bool debe_reponer(int disponibles, int minimo) {"
TRACE = "LGAED_U1_PROBE:"
THREE = ((4, 5), (5, 5), (6, 5))
SIX = (*THREE, (7, 8), (8, 8), (9, 8))


def validate_cases(cases: tuple[tuple[int, int], ...]) -> None:
    """A bounded, non-vacuous set of separately observable C++ int inputs."""
    if (type(cases) is not tuple or not 1 <= len(cases) <= 32 or
            any(type(pair) is not tuple or len(pair) != 2 or
                any(type(value) is not int or
                    not -(2 ** 31) <= value < 2 ** 31 for value in pair)
                for pair in cases) or len(set(cases)) != len(cases)):
        raise ValueError("Expected 1 to 32 distinct, representable integer input pairs")



@dataclass(frozen=True)
class Audit:
    accepted: bool
    reason: str
    observations: tuple[tuple[int, int], ...] = ()
    phases: tuple[tuple[str, int], ...] = ()


def source_for(identity: str) -> str:
    document = json.loads(SOURCE.read_text(encoding="utf-8"))
    for index, page in enumerate(document["stateLayout"]["current"]):
        for slot, owner in enumerate(page["blocks"]):
            if owner != identity:
                continue
            block = document["pages"][index]["blocks"][slot]
            if block.get("type") != "cpp_compiler":
                raise ValueError("The original owner is not a C++ editor")
            return block["initialCode"]
    raise ValueError("The original C++ owner is missing")


def instrument(source: str, cases: tuple[tuple[int, int], ...],
               *, broken_original: bool, mode: str = "correct",
               mutant: int | None = None) -> str:
    """Compile a distinct C++ implementation for every independent mutation.

    No phase switches, hidden arguments, or environment variables are exposed
    to the candidate's main. Only the exact published original function body
    may be substituted; other code is preserved byte for byte. This limited
    instrumenter is not a general-purpose C++ source rewriter.
    """
    validate_cases(cases)
    if source.count(FUNCTION) != 1:
        raise ValueError("Missing or ambiguous original debe_reponer")
    start = source.index(FUNCTION) + len(FUNCTION)
    end = source.find("}", start)
    if end < 0:
        raise ValueError("Missing original function ending")
    baseline = "return disponibles < minimo;" if broken_original else (
        "return disponibles <= minimo;")
    if source[start:end].strip() != baseline:
        raise ValueError("The published function was changed instead of the battery")
    if mode not in ("correct", "mutant", "original-defect"):
        raise ValueError("Unknown mutation phase")
    if mode == "mutant":
        if type(mutant) is not int or not 0 <= mutant < len(cases):
            raise ValueError("A mutation must identify exactly one original case")
        available, minimum = cases[mutant]
        condition = f"(disponibles == {available} && minimo == {minimum})"
    else:
        if mutant is not None:
            raise ValueError("Only a mutant may name a case index")
        if mode == "original-defect" and not broken_original:
            raise ValueError("The original defective comparison is not this owner's contract")
        condition = "false"
    comparison = "<" if mode == "original-defect" else "<="
    body = (
        f"\n    const bool resultado = disponibles {comparison} minimo;\n"
        f"    const bool invertir = {condition};\n"
        f'    std::cerr << "{TRACE}" << disponibles << "," << minimo << std::endl;\n'
        "    return invertir ? !resultado : resultado;\n"
    )
    return source[:start] + body + source[end:]


def compile_candidate(source: str, directory: Path, cxx: str) -> Path:
    cpp = directory / "candidate.cpp"
    cpp.write_text(source, encoding="utf-8")
    exe = directory / ("candidate.exe" if sys.platform == "win32" else "candidate")
    if Path(cxx).name.lower() in ("cl", "cl.exe"):
        args = [cxx, "/nologo", "/std:c++latest", "/EHsc", "/W4",
                "/WX", "/permissive-", "/utf-8", str(cpp), "/Fe:" + str(exe)]
    else:
        args = [cxx, "-std=c++23", "-Wall", "-Wextra", "-Werror",
                "-pedantic", str(cpp), "-o", str(exe)]
    result = subprocess.run(args, cwd=directory, capture_output=True, text=True,
                            errors="replace", timeout=90)
    if result.returncode or not exe.is_file():
        raise ValueError("Compilation failed: " + result.stderr + result.stdout)
    # A student executable must not be able to inspect the private mutant's
    # generated source. The next phase recreates the same source path anew.
    cpp.unlink()
    return exe


def execute(exe: Path) -> tuple[int, list[tuple[int, int]]]:
    # Do not pass mutation selection to a candidate through its environment or
    # arguments. Every variant is an independently compiled private artifact.
    env = os.environ.copy()
    env.pop("LGAED_U1_BROKEN", None)
    env.pop("LGAED_U1_MUTANT", None)
    result = subprocess.run([str(exe)], cwd=exe.parent, env=env,
                            capture_output=True, text=True,
                            errors="replace", timeout=12)
    calls = []
    for line in result.stderr.splitlines():
        if not line.startswith(TRACE):
            continue
        parts = line[len(TRACE):].split(",")
        if len(parts) != 2:
            raise ValueError("Malformed original-call instrumentation")
        calls.append((int(parts[0]), int(parts[1])))
    return result.returncode, calls


def audit(source: str, cases: tuple[tuple[int, int], ...], cxx: str,
          *, broken_original: bool) -> Audit:
    """Require all calls and independent failed mutants in separate binaries.

    The candidate's main must fail each wrong-result implementation, while
    succeeding with a correct comparator. Every variant is recompiled in the
    SAME private workspace; phase identifiers, source bytes and process
    arguments are not exposed to the candidate.
    This proof is isolated QA, not a persistent completion or exercise oracle.
    """
    try:
        with tempfile.TemporaryDirectory(prefix="lgaed-u1-battery-") as tmp:
            phases = [("correct", None)]
            phases.extend((f"mutant-{i}", i) for i in range(len(cases)))
            if broken_original:
                phases.append(("original-defect", None))
            seen = []
            # The same absolute source path and executable path are used for
            # every private mutation. Otherwise __FILE__, argv[0], or the
            # current working directory would reveal phase-0/phase-1.
            directory = Path(tmp) / "sandbox"
            directory.mkdir()
            for label, index in phases:
                mode = ("original-defect" if label == "original-defect" else
                        "mutant" if index is not None else "correct")
                program = instrument(source, cases, broken_original=broken_original,
                                     mode=mode, mutant=index)
                exe = compile_candidate(program, directory, cxx)
                code, observed = execute(exe)
                seen.append((label, code))
                if Counter(observed) != Counter(cases):
                    return Audit(False, label + ": missing, repeated or skipped calls",
                                 tuple(observed), tuple(seen))
                expected_success = label == "correct"
                if (code == 0) != expected_success:
                    return Audit(False, label + ": a comparison did not drive the exit status",
                                 tuple(observed), tuple(seen))
            return Audit(True, "Every independent mutant detected and all calls executed",
                         tuple(cases), tuple(seen))
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        return Audit(False, "Compilation or execution failed: " + str(exc))


def three_case_candidate() -> str:
    original = source_for("block_0004_0004")
    old = """    // Compara las tres llamadas con esperados obtenidos del requisito.
    // Muestra los casos que fallan y comunica el resultado con el retorno.
    return 0;"""
    new = """    const auto comprobar = [](int disponibles, int minimo, bool esperado) {
        const bool obtenido = debe_reponer(disponibles, minimo);
        if (obtenido != esperado) {
            std::cout << "Caso distinto: " << disponibles << ',' << minimo << std::endl;
            return false;
        }
        return true;
    };
    bool correcto = true;
    if (!comprobar(4, 5, true)) correcto = false;
    if (!comprobar(5, 5, true)) correcto = false;
    if (!comprobar(6, 5, false)) correcto = false;
    return correcto ? 0 : 1;"""
    if original.count(old) != 1:
        raise ValueError("The first published U1 editor changed")
    return original.replace(old, new)


def six_case_candidate() -> str:
    original = source_for("block_0005_0009")
    old = "    if (!correcto) { return 1; }\n    return 0;"
    additional = """    if (!comprobar(7, 8, true)) { correcto = false; }
    if (!comprobar(8, 8, true)) { correcto = false; }
    if (!comprobar(9, 8, false)) { correcto = false; }
"""
    if original.count(old) != 1:
        raise ValueError("The second published U1 editor changed")
    return original.replace(old, additional + old)

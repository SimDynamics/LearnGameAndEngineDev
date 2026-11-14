"""Mutation-based evidence for two original Programming 2 U1 C++ batteries.

This isolated CI oracle checks a candidate's own main across independent
behavior mutations. It does NOT change lesson assets or grant in-app progress.
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
               *, broken_original: bool) -> str:
    """Replace only the bounded original function; preserve candidate main.

    This intentionally requires the EXACT two published simple bodies. A
    general student-code transformation needs a compiler-aware source mapper.
    """
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
    mutated = " || ".join(
        f"(disponibles == {a} && minimo == {b} && mutant == {i})"
        for i, (a, b) in enumerate(cases)
    )
    # The QA wrapper reads only runner-owned environment variables. MSVC's
    # safe CRT uses _dupenv_s; do not disable C4996 (or weaken /WX) globally.
    env_reader = r"""
#include <cstdlib>
#include <string>
static std::string lgaed_u1_env(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0) return {};
    const std::string result = value ? value : "";
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}
"""
    body = (
        "\n    const bool original_defect = !lgaed_u1_env(\"LGAED_U1_BROKEN\").empty();\n"
        "    const bool resultado = original_defect "
        "? disponibles < minimo : disponibles <= minimo;\n"
        "    const std::string cambio = lgaed_u1_env(\"LGAED_U1_MUTANT\");\n"
        "    const int mutant = cambio.empty() ? -1 : std::stoi(cambio);\n"
        f"    const bool invertir = {mutated};\n"
        f'    std::cerr << "{TRACE}" << disponibles << "," << minimo << std::endl;\n'
        "    return invertir ? !resultado : resultado;\n"
    )
    return env_reader + source[:start] + body + source[end:]


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
    return exe


def execute(exe: Path, *, original_defect: bool = False,
            mutant: int | None = None) -> tuple[int, list[tuple[int, int]]]:
    env = os.environ.copy()
    env.pop("LGAED_U1_BROKEN", None)
    env.pop("LGAED_U1_MUTANT", None)
    if original_defect:
        env["LGAED_U1_BROKEN"] = "1"
    if mutant is not None:
        env["LGAED_U1_MUTANT"] = str(mutant)
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
    """Require every original call even after a mismatch, plus mutation kills.

    A successful stdout or a fixed exit code is NOT proof of comparisons.
    Every case receives a private wrong-result mutation; the candidate's own
    main must fail each mutation while passing the correct implementation.
    """
    try:
        compiled = instrument(source, cases, broken_original=broken_original)
        with tempfile.TemporaryDirectory(prefix="lgaed-u1-battery-") as tmp:
            exe = compile_candidate(compiled, Path(tmp), cxx)
            phases = [("correct", None)]
            phases.extend((f"mutant-{i}", i) for i in range(len(cases)))
            if broken_original:
                phases.append(("original-defect", None))
            seen = []
            for label, index in phases:
                code, observed = execute(
                    exe, original_defect=label == "original-defect", mutant=index)
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

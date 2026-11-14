"""Independent compiled evidence of the U1 original-state reduction exercise.

A disposable QA instrumenter preserves the published investigated function
byte-exact. It runs the student main with the original cumulative behavior
and with a privately changed SECOND return. A printed literal '1' cannot
explain both observations. This is a technical regression prototype, not an
in-app progress receipt or a general-purpose C++ rewriting engine.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import subprocess
import tempfile

from u1_battery_mutation_oracle import compile_candidate

ROOT = Path(__file__).resolve().parents[2]
LESSON = (ROOT / "app/src/main/assets/content/es/programming/programming_practice_tools"
          / "evidencia_y_diagnostico/lesson_reducir_codigo_sin_borrar_el_fallo.json")
OWNER = "block_0002_0003"
TRACE = "LGAED_U1_REDUCTION:"
FUNCTION = "int contar_altos(std::vector<int> datos, int minimo) {"
ORIGINAL_CLOSURE = "    return acumulado;\n}"


@dataclass(frozen=True)
class ReductionAudit:
    accepted: bool
    reason: str
    original_stdout: str = ""
    changed_stdout: str = ""


def published_source() -> str:
    document = json.loads(LESSON.read_text(encoding="utf-8"))
    for page, layout in enumerate(document["stateLayout"]["current"]):
        for index, identity in enumerate(layout["blocks"]):
            if identity == OWNER:
                block = document["pages"][page]["blocks"][index]
                if block.get("type") != "cpp_compiler":
                    raise ValueError("The original reduction owner is not a compiler")
                return block["initialCode"]
    raise ValueError("The original reduction owner disappeared")


def original_function() -> str:
    source = published_source()
    first = source.find(FUNCTION)
    if first < 0 or source.find(FUNCTION, first + 1) >= 0:
        raise ValueError("Missing or ambiguous investigated function")
    last = source.find(ORIGINAL_CLOSURE, first)
    if last < 0:
        raise ValueError("Missing cumulative return")
    return source[first:last + len(ORIGINAL_CLOSURE)]


def instrument(submitted: str, *, changed_second: bool) -> str:
    reference = published_source()
    original = original_function()
    if submitted.count(original) != 1:
        raise ValueError("The investigated original function body must be retained")
    # An actual reduction must remove nonessential work, not simply print an
    # invented result. Threshold is fixture-specific, not a general source metric.
    if len(submitted.encode("utf-8")) > len(reference.encode("utf-8")) - 120:
        raise ValueError("No meaningful reduction of published accessory code")
    if "#include <iostream>" not in submitted or "#include <vector>" not in submitted:
        raise ValueError("The candidate lost one of its required C++ dependencies")
    replacement = (
        "    const int observado = acumulado"
        + (" + (datos.empty() ? 1 : 0)" if changed_second else "")
        + ";\n"
        f'    std::cerr << "{TRACE}" << datos.size() << ","'
        ' << minimo << "," << observado << std::endl;\n'
        "    return observado;\n}"
    )
    return submitted.replace(original, original.replace(ORIGINAL_CLOSURE, replacement))


def _run_once(executable: Path) -> tuple[int, str, list[tuple[int, int, int]]]:
    result = subprocess.run([str(executable)], cwd=executable.parent,
                            capture_output=True, text=True, errors="replace", timeout=12)
    calls = []
    for line in result.stderr.splitlines():
        if not line.startswith(TRACE):
            continue
        payload = line[len(TRACE):].split(",")
        if len(payload) != 3:
            raise ValueError("Malformed independent reduction trace")
        calls.append(tuple(int(value) for value in payload))
    return result.returncode, result.stdout, calls


def audit(submitted: str, compiler: str) -> ReductionAudit:
    try:
        with tempfile.TemporaryDirectory(prefix="lgaed-u1-reduction-") as root:
            # Reusing the same absolute source and executable paths keeps the
            # private mutation index out of __FILE__, argv[0] and cwd.
            workspace = Path(root) / "sandbox"
            workspace.mkdir()
            results = []
            for changed in (False, True):
                executable = compile_candidate(
                    instrument(submitted, changed_second=changed), workspace, compiler)
                status, stdout, calls = _run_once(executable)
                required = ([(1, 2, 1), (0, 2, 2)] if changed else
                            [(1, 2, 1), (0, 2, 1)])
                if status != 0:
                    return ReductionAudit(False, "The reduced program did not terminate normally")
                if calls != required:
                    return ReductionAudit(False, "Missing, reordered or incorrect cumulative calls")
                results.append(stdout)
            original, modified = results
            # The result must come from the return value, not a hard-coded
            # explanation. Keep every other byte of the authored output intact.
            if (len(original) != len(modified) or
                sum(a != b for a, b in zip(original, modified)) != 1 or
                not any(a == "1" and b == "2"
                        for a, b in zip(original, modified))):
                return ReductionAudit(False, "Second result was not reflected in the output",
                                      original, modified)
            return ReductionAudit(True, "The same reduced program exposes the cumulative defect",
                                  original, modified)
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        return ReductionAudit(False, "Unable to verify the reduced program: " + str(error))


def reduced_candidate() -> str:
    original = published_source()
    retained = original_function()
    return ("#include <iostream>\n#include <vector>\n\nint acumulado{0};\n\n"
            + retained + "\n\n"
            + "int main() {\n"
            + "    (void)contar_altos({2}, 2);\n"
            + "    const int segundo = contar_altos({}, 2);\n"
            + '    std::cout << "Segundo=" << segundo << std::endl;\n'
            + "    return 0;\n}\n")

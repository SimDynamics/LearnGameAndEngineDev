#!/usr/bin/env python3
"""Execute the current U2L5 first-case contracts with hosted Windows MSVC.

This does NOT use the application's pinned compiler toolchain or verify audio.
All C++ programs are compiled and run for real in a disposable QA directory.
The private mutations come from the unchanged production C++ probe helper.
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[2]
LESSON_PATH = (
    ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
    / "objetos_y_estado/lesson_enteros_y_aproximaciones.json"
)
PROBE_HELPER = ROOT / "scripts/windows/u2l5_local_int_variant.cpp"
PAIRS = (
    ("block_0006_0002", "block_0007_0001"),
    ("p1u2l5_reparar_agrupacion", "p1u2l5_referencia_agrupacion"),
)


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def run(command: list[str], directory: Path, *, stdin: bytes = b"", timeout: int = 75) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        command, cwd=directory, input=stdin, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=timeout, check=False
    )


def diagnostic(result: subprocess.CompletedProcess[bytes]) -> str:
    return (result.stdout + b"\n" + result.stderr).decode("utf-8", errors="replace")[-4000:]


def canonical(output: bytes) -> bytes:
    # Match the production console's only accepted line-ending equivalence.
    return output.replace(b"\r\n", b"\n")


def matches(stdout: bytes, accepted: list[str]) -> bool:
    return canonical(stdout) in {canonical(x.encode("utf-8")) for x in accepted}


def get_authored():
    document = json.loads(LESSON_PATH.read_text(encoding="utf-8-sig"))
    require(document["id"] == "cpp_fundamentals__objetos_y_estado__enteros_y_aproximaciones",
            "Unexpected current lesson identity")
    require(document.get("compilerPuzzlesRequired") is True,
            "The U2L5 compiler contract became optional")
    pages = document["pages"]
    current = document["stateLayout"]["current"]
    require(len(pages) == len(current), "U2L5 page/state owner alignment changed")
    owners = {}
    for page, record in zip(pages, current):
        require(len(page["blocks"]) == len(record["blocks"]),
                "U2L5 block/state owner alignment changed")
        for owner, block in zip(record["blocks"], page["blocks"]):
            require(owner not in owners, "Duplicate active curricular owner")
            owners[owner] = block
    active = {key for key, value in owners.items() if value["type"] == "cpp_compiler"}
    require(active == {pair[0] for pair in PAIRS},
            "Current two-editor U2L5 inventory changed")
    probes = document.get("compilerLocalIntChecks", [])
    require({item["activity"] for item in probes} == active and len(probes) == 2,
            "Independent private probe owners changed")
    return owners, {item["activity"]: item["probes"] for item in probes}


def build_cpp(source: str, name: str, work: Path, *, include_native: bool = False) -> Path:
    source_path = work / (name + ".cpp")
    target = work / (name + ".exe")
    source_path.write_text(source, encoding="utf-8", newline="\n")
    cmd = [
        "cl", "/nologo", "/std:c++latest", "/EHsc", "/permissive-",
        "/utf-8", "/W4", "/Zc:__cplusplus",
    ]
    if include_native:
        cmd.append("/I" + str(ROOT / "native"))
    cmd += ["/Fe" + str(target), "/Fo" + str(work / (name + ".obj")), str(source_path)]
    result = run(cmd, work)
    require(result.returncode == 0 and target.exists(),
            "Native MSVC compilation failed for " + name + ":\n" + diagnostic(result))
    return target


def execute(executable: Path, work: Path, stdin: str = "") -> bytes:
    result = run([str(executable)], work, stdin=stdin.encode("utf-8"), timeout=15)
    require(result.returncode == 0,
            "Native C++ process failed: " + str(executable.name) + "\n" + diagnostic(result))
    return result.stdout


def generate_variant(
    helper: Path, source_path: Path, probe: dict, output_path: Path, work: Path
) -> subprocess.CompletedProcess[bytes]:
    require(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", probe["variable"]) is not None,
            "Invalid published local-int variable")
    replacement = probe["replacement"]
    require(type(replacement) is int and -(2 ** 31) <= replacement < (2 ** 31),
            "Invalid published int32 replacement")
    after = probe.get("afterUpdate", False)
    require(type(after) is bool, "Invalid published afterUpdate flag")
    return run([
        str(helper), str(source_path), probe["variable"], str(replacement),
        "1" if after else "0", str(output_path),
    ], work, timeout=15)


def test() -> None:
    require(sys.platform == "win32" and shutil.which("cl") is not None,
            "This check requires a real Windows MSVC developer environment")
    owners, private = get_authored()
    with TemporaryDirectory(prefix="lgaed_u2l5_native_") as directory:
        work = Path(directory)
        helper = build_cpp(PROBE_HELPER.read_text(encoding="utf-8"),
                           "native_local_int_mutator", work, include_native=True)
        successes = 0
        for number, (owner, reference) in enumerate(PAIRS, start=1):
            goal = owners[owner]
            answer = owners[reference]
            require(goal["type"] == "cpp_compiler" and answer["type"] == "code",
                    "The U2L5 executable or reference has the wrong type")
            cases = goal["outputCheck"]["cases"]
            require(cases and isinstance(cases[0]["outputs"], list) and cases[0]["outputs"],
                    "The current first-case output contract is missing")
            first_case = cases[0]
            stdin = first_case["standardInput"]
            allowed = first_case["outputs"]
            require(type(stdin) is str and all(type(x) is str for x in allowed),
                    "Invalid actual first-case input/output")
            checks = private[owner]
            require(len(checks) == 2 and all(p.get("outputs") for p in checks),
                    "The independent two-probe objective is incomplete")

            # An untouched starter must not earn the first case.
            starter = build_cpp(goal["initialCode"], f"editor{number}_starter", work)
            require(not matches(execute(starter, work, stdin), allowed),
                    "The published starter passed its supposedly incomplete goal")

            # Printed answers alone are insufficient, even if a learner keeps
            # the named local ints around to fool a purely lexical check.
            fixed_line = json.dumps(allowed[0], ensure_ascii=True)
            declarations = "".join(
                f"    int {var} = 2;\n" for var in sorted({p["variable"] for p in checks})
            )
            fixed = (
                '#include <iostream>\nint main() {\n'
                + declarations + "    std::cout << " + fixed_line
                + ";\n    return 0;\n}\n"
            )
            fixed_exe = build_cpp(fixed, f"editor{number}_fixed", work)
            require(matches(execute(fixed_exe, work, stdin), allowed),
                    "The adversarial fixed-output program did not reproduce visible stdout")

            # The actual reference must compile and satisfy the first case.
            solution = answer["code"]
            solution_exe = build_cpp(solution, f"editor{number}_answer", work)
            require(matches(execute(solution_exe, work, stdin), allowed),
                    "The authored reference failed the first native Windows output case")
            solution_source = work / f"editor{number}_answer.cpp"
            fixed_source = work / f"editor{number}_fixed.cpp"

            for index, probe in enumerate(checks, start=1):
                output = work / f"editor{number}_variant{index}.cpp"
                prepared = generate_variant(helper, solution_source, probe, output, work)
                require(prepared.returncode == 0 and output.exists(),
                        "The original production probe cannot transform the authored solution:\n"
                        + diagnostic(prepared))
                compiled = build_cpp(output.read_text(encoding="utf-8"),
                                     f"editor{number}_variant{index}_run", work)
                require(matches(execute(compiled, work, stdin), probe["outputs"]),
                        "The independent native probe result differs from its authored output: "
                        + owner + "/" + probe["variable"])

                # A superficial solution still compiles and prints the right
                # visible result, but changing its variables has no effect.
                fixed_variant = work / f"editor{number}_fixed_variant{index}.cpp"
                forged = generate_variant(helper, fixed_source, probe, fixed_variant, work)
                require(forged.returncode == 0 and fixed_variant.exists(),
                        "Adversarial source did not exercise the same safe probe")
                forged_exe = build_cpp(fixed_variant.read_text(encoding="utf-8"),
                                       f"editor{number}_fixed_probe{index}", work)
                require(not matches(execute(forged_exe, work, stdin), probe["outputs"]),
                        "A fixed stdout incorrectly survived a private native probe")

            # Without the authored int declarations, even applying the private
            # probe must fail closed instead of silently rewriting a string.
            literal_only = (
                '#include <iostream>\nint main() {\n    std::cout << '
                + fixed_line + ';\n    return 0;\n}\n'
            )
            literal_path = work / f"editor{number}_literal.cpp"
            literal_path.write_text(literal_only, encoding="utf-8")
            refused = generate_variant(helper, literal_path, checks[0],
                                       work / f"editor{number}_refused.cpp", work)
            require(refused.returncode == 3,
                    "The original production source probe accepted a constant-only stdout")
            successes += 1
            print(f"PASS U2L5 real Windows C++ editor {number}: first case, "
                  f"starter rejection, reference, two independent native probes "
                  f"and fixed-stdout rejection", flush=True)

        require(successes == 2, "The active two-editor objective was not exhausted")
    print("PASS U2L5: 2 real MSVC programs, 4 independent native variants "
          "and fixed-output counterexamples; hosted Windows only, "
          "no application playback or production-toolchain claim", flush=True)


if __name__ == "__main__":
    try:
        test()
    except (AssertionError, OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print("FAIL current U2L5 real MSVC execution: " + str(error), file=sys.stderr)
        raise SystemExit(1) from error

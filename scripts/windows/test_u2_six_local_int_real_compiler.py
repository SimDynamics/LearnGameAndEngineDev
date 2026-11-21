#!/usr/bin/env python3
"""Real hosted-Windows MSVC execution of the six published P1 U2L1-L3 editors.

Read the EXACT same temporary opt-in authoring plans as the native WinRT
StateStore tests. No course JSON, profiles or source drafts are changed.
The application's pinned production toolchain and Kokoro audio are separate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_u2l5_real_compiler import (
    ROOT, PROBE_HELPER, build_cpp, diagnostic, execute,
    generate_variant, matches, require, run,
)

LESSON_DIRECTORY = (
    ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
    / "objetos_y_estado"
)
EXPECTED_LESSONS = (
    "lesson_objetos_valores_y_tipos.json",
    "lesson_declarar_e_inicializar.json",
    "lesson_estado_asignacion_y_copia.json",
)
EXPECTED_EDITORS = {
    EXPECTED_LESSONS[0]: {"block_0004_0002", "block_0004_0006"},
    EXPECTED_LESSONS[1]: {"block_0004_0002", "block_0004_0006"},
    EXPECTED_LESSONS[2]: {"block_0006_0002", "block_0006_0006"},
}


def lesson_owners(document: dict) -> dict:
    pages = document["pages"]
    current = document["stateLayout"]["current"]
    require(len(pages) == len(current), "Current lesson pages/state layout differ")
    found = {}
    for page, record in zip(pages, current):
        require(len(page["blocks"]) == len(record["blocks"]),
                "The current page has a changed historical block mapping")
        for owner, block in zip(record["blocks"], page["blocks"]):
            require(owner not in found, "Duplicate active authored block identity")
            found[owner] = block
    return found


def fixture_plan(executable: Path) -> list:
    result = run([str(executable), "--export-six-u2-qa"], executable.parent, timeout=60)
    require(result.returncode == 0,
            "The original Windows WinRT QA could not export its in-memory plan:\n"
            + diagnostic(result))
    try:
        plan = json.loads(result.stdout.decode("utf-8-sig"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise AssertionError("WinRT QA emitted invalid source/probe JSON") from error
    require(isinstance(plan, list) and len(plan) == len(EXPECTED_LESSONS),
            "The existing three-lesson WinRT authoring inventory changed")
    return plan


def fake_output_program(observed: str, probes: list[dict]) -> str:
    """One executable with all declared ints, but NO arithmetic output dependency."""
    variables = sorted({probe["variable"] for probe in probes})
    lines = ["#include <iostream>", "int main() {"]
    lines += [f"    int {name} = 5;" for name in variables]
    for probe in probes:
        if probe.get("afterUpdate", False):
            lines.append(f"    {probe['variable']} += 3;")
    lines += [
        "    std::cout << " + json.dumps(observed, ensure_ascii=True) + ";",
        "    return 0;",
        "}",
    ]
    return "\n".join(lines) + "\n"


def test(exporter: Path) -> None:
    require(sys.platform == "win32" and shutil.which("cl") is not None,
            "The six real execution checks require native Windows MSVC")
    require(exporter.is_file() and exporter.suffix.lower() == ".exe",
            "Supply the exact compiled native local-int StateStore QA executable")
    plan = fixture_plan(exporter)
    require(tuple(item["filename"] for item in plan) == EXPECTED_LESSONS,
            "The native WinRT proposal inventory changed order or lesson names")
    with TemporaryDirectory(prefix="lgaed_u2_six_real_") as directory:
        work = Path(directory)
        helper = build_cpp(
            PROBE_HELPER.read_text(encoding="utf-8"),
            "u2_shared_production_local_int_mutator", work, include_native=True)
        owners_checked = probes_checked = counterexamples = 0
        originals = {}
        for number, proposal in enumerate(plan, start=1):
            filename = proposal["filename"]
            path = LESSON_DIRECTORY / filename
            require(path.is_file(), "Missing published U2 curriculum input")
            source_bytes = path.read_bytes()
            originals[path] = hashlib.sha256(source_bytes).hexdigest()
            document = json.loads(source_bytes.decode("utf-8-sig"))
            owners = lesson_owners(document)
            editors = {key for key, value in owners.items() if value["type"] == "cpp_compiler"}
            require(editors == EXPECTED_EDITORS[filename],
                    "The current two-editor U2 lesson inventory changed")
            checks = proposal["checks"]
            answers = proposal["answers"]
            require({p["activity"] for p in checks} == editors and
                    {a["id"] for a in answers} == editors and
                    len(checks) == len(answers) == 2,
                    "The native test's temporary authoring plan changed its owners")
            by_owner = {p["activity"]: p["probes"] for p in checks}
            for second, authored in enumerate(answers, start=1):
                owner = authored["id"]
                goal = owners[owner]
                cases = goal["outputCheck"]["cases"]
                require(bool(cases) and bool(cases[0]["outputs"]),
                        "The first visible output case is missing")
                case = cases[0]
                standard_input = case["standardInput"]
                allowed = case["outputs"]
                require(type(standard_input) is str and
                        isinstance(allowed, list) and all(type(v) is str for v in allowed),
                        "The published first output contract is invalid")
                probes = by_owner[owner]
                require(1 <= len(probes) <= 8, "The opt-in authoring plan lost its probes")
                name = f"p{number}_editor{second}"
                source = authored["source"]
                reference = build_cpp(source, name + "_reference", work)
                visible = execute(reference, work, standard_input)
                require(matches(visible, allowed),
                        "A real U2 reference failed its unchanged first visible case: "+filename+"/"+owner)

                fake = fake_output_program(allowed[0], probes)
                decoy = build_cpp(fake, name + "_fixed_stdout", work)
                require(matches(execute(decoy, work, standard_input), allowed),
                        "The fixed-output counterexample failed to reproduce the visible answer")
                refer_path = work / (name + "_reference.cpp")
                fake_path = work / (name + "_fixed_stdout.cpp")
                for i, probe in enumerate(probes, start=1):
                    require(bool(probe.get("outputs")),
                            "The in-memory authoring case lost expected private output")
                    variant_path = work / f"{name}_probe{i}.cpp"
                    prepared = generate_variant(helper, refer_path, probe, variant_path, work)
                    require(prepared.returncode == 0 and variant_path.is_file(),
                            "The real production source mutator rejected the authored valid solution:\n"
                            + diagnostic(prepared))
                    variant = build_cpp(variant_path.read_text(encoding="utf-8"),
                                        f"{name}_run_probe{i}", work)
                    changed = execute(variant, work, standard_input)
                    require(matches(changed, probe["outputs"]),
                            "A private MSVC execution disagrees with its independent output: "
                            +filename+"/"+owner+"/"+probe["variable"])
                    require(not matches(visible, probe["outputs"]),
                            "A private probe did not distinguish the original visible output")

                    fake_variant_path = work / f"{name}_fake_probe{i}.cpp"
                    fake_prepared = generate_variant(helper, fake_path, probe, fake_variant_path, work)
                    require(fake_prepared.returncode == 0 and fake_variant_path.is_file(),
                            "The fixed-output adversary could not be probed independently:\n"
                            + diagnostic(fake_prepared))
                    fake_variant = build_cpp(
                        fake_variant_path.read_text(encoding="utf-8"),
                        f"{name}_run_fake_probe{i}", work)
                    require(not matches(execute(fake_variant, work, standard_input),
                                        probe["outputs"]),
                            "An unrelated fixed output passed a private real Windows probe")
                    probes_checked += 1
                    counterexamples += 1

                # U2L3's second editor must copy the updated int into
                # 'anterior'. Printing aforo twice matches BOTH visible lines,
                # but the private 'anterior' probe cannot be constructed.
                if filename == EXPECTED_LESSONS[2] and second == 2:
                    without_copy = (
                        '#include <iostream>\nint main(){int aforo=5; aforo+=3;\n'
                        'std::cout << "Antes: " << aforo << "\\nAhora: " << aforo;\n}\n'
                    )
                    wrong = build_cpp(without_copy, name + "_no_copy", work)
                    require(matches(execute(wrong, work, standard_input), allowed),
                            "The intentionally deceptive duplicate-output case changed")
                    no_copy_file = work / (name + "_no_copy.cpp")
                    for probe in probes:
                        if probe["variable"] != "anterior":
                            continue
                        result = generate_variant(
                            helper, no_copy_file, probe,
                            work / (name + "_no_copy_variant.cpp"), work)
                        require(result.returncode == 3,
                                "The duplicate-aforo solution passed without its required copy")
                        counterexamples += 1
                owners_checked += 1
                print(f"PASS U2 {filename} {owner}: real first case, "
                      f"{len(probes)} independent hidden native probes and fixed-stdout rejection",
                      flush=True)
        require(owners_checked == 6 and probes_checked == 12 and counterexamples == 13,
                "The six current U2 objectives or their twelve private probes changed")
        for path, before in originals.items():
            require(hashlib.sha256(path.read_bytes()).hexdigest() == before,
                    "The authored U2 JSON was modified during an isolated regression")
    print("PASS six U2 editors: 12 real MSVC private probes; source-equivalent "
          "authoring plans shared with WinRT loader and StateStore tests. "
          "No curriculum integration, application runner or student audio claimed.",
          flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-fixture", type=Path, required=True)
    args = parser.parse_args()
    try:
        test(args.state_fixture.resolve())
    except (AssertionError, OSError, ValueError, KeyError,
            json.JSONDecodeError, subprocess.TimeoutExpired) as error:
        print("FAIL six-U2 real Windows local-int authoring: " + str(error),
              file=sys.stderr)
        sys.exit(1)

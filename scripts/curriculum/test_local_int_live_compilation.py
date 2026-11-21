"""Compile and execute the core local-int probe's real generated C++ programs.

The emitter calls the production cpp_local_int_override implementation, then
this fixture runs the ORIGINAL and ALTERED programs on the host compiler. No
curricular JSON, user account, saved profile or application toolchain is edited.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def build_command(compiler: str, source: Path, binary: Path) -> list[str]:
    if compiler == "MSVC":
        return ["cl", "/nologo", "/std:c++latest", "/EHsc", "/utf-8", "/W4",
                "/Fe:" + str(binary), "/Fo:" + str(source.with_suffix(".obj")),
                str(source)]
    if compiler not in {"g++", "clang++"}:
        raise ValueError("Unsupported compiler for local-int integration")
    return [compiler, "-std=c++23", "-Wall", "-Wextra", "-pedantic",
            str(source), "-o", str(binary)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emitter", required=True, type=Path)
    parser.add_argument("--compiler", required=True, choices=["g++", "clang++", "MSVC"])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="lgaed-local-int-live-") as tmp:
        output = Path(tmp)
        emitted = subprocess.run([str(args.emitter.resolve()), str(output)],
                                 capture_output=True, timeout=30, check=True)
        if b"PASS generated 30" not in emitted.stdout:
            raise RuntimeError("The real source-mutation fixture did not finish")
        sources = sorted(output.glob("*.cpp"))
        if len(sources) != 30:
            raise RuntimeError(f"Expected exactly 30 source programs, got {len(sources)}")
        # Verify that the emitted U2L5 originals remain the EXACT currently
        # authored code references, not a convenient rewritten illustration.
        # The private probes must also match their published values/outputs.
        curriculum = (Path(__file__).resolve().parents[2] /
                      "app/src/main/assets/content/es/programming/cpp_fundamentals/"
                      "objetos_y_estado/lesson_enteros_y_aproximaciones.json")
        authored = json.loads(curriculum.read_text(encoding="utf-8-sig"))
        pages = authored["pages"]
        layout = authored["stateLayout"]["current"]
        blocks = {}
        for page, state_page in zip(pages, layout, strict=True):
            for block, identity in zip(page["blocks"], state_page["blocks"], strict=True):
                if identity in blocks:
                    raise RuntimeError(f"Duplicate active U2L5 owner: {identity}")
                blocks[identity] = block

        specs = {item["activity"]: item["probes"]
                 for item in authored["compilerLocalIntChecks"]}
        if len(specs) != 2:
            raise RuntimeError("U2L5 no longer has exactly two independent probe owners")
        contracts = [
            ("u2l5_division_variable", "block_0006_0002",
             "block_0007_0001", "Parte: 4.5 m",
             [("partes", 4, "Parte: 2.25 m"), ("partes", 8, "Parte: 1.125 m")]),
            ("u2l5_suma_agrupada", "p1u2l5_reparar_agrupacion",
             "p1u2l5_referencia_agrupacion", "Parte: 2.75 m",
             [("tramo_adicional_metros", 3, "Parte: 3.75 m"),
              ("partes", 4, "Parte: 1.375 m")]),
        ]
        for name, owner, reference, visible, checks in contracts:
            target = blocks[owner]
            source = blocks[reference]
            if target["type"] != "cpp_compiler" or source["type"] != "code":
                raise RuntimeError(f"Current U2L5 source or editor retired: {owner}")
            visible_outputs = target["outputCheck"]["cases"][0]["outputs"]
            if visible_outputs != [visible, visible + "\n"]:
                raise RuntimeError(f"Current U2L5 visible output changed: {owner}")
            if (output / (name + ".cpp")).read_text(encoding="utf-8") != source["code"]:
                raise RuntimeError(f"Generated U2L5 C++ differs from its actual reference: {reference}")
            declared = specs.get(owner)
            expected = [{"variable": variable, "replacement": replacement,
                         "outputs": [stdout, stdout + "\n"]}
                        for variable, replacement, stdout in checks]
            if declared != expected:
                raise RuntimeError(f"Private U2L5 probe declarations changed: {owner}")
        accepted = rejected = 0
        for source in sources:
            evidence = source.with_suffix(".expected").read_bytes()
            kind, separator, expected = evidence.partition(b"\n")
            if not separator or kind not in {b"A", b"R"}:
                raise RuntimeError(f"Missing or invalid expected stdout: {source.name}")
            binary = source.with_suffix(".exe" if sys.platform == "win32" else ".bin")
            command = build_command(args.compiler, source, binary)
            compile_run = subprocess.run(command, cwd=output, capture_output=True,
                                         timeout=90, check=False)
            if compile_run.returncode:
                raise RuntimeError(
                    f"{args.compiler} could not compile {source.name}:\n"
                    + compile_run.stdout.decode(errors="replace")[-4000:]
                    + compile_run.stderr.decode(errors="replace")[-4000:])
            observed = subprocess.run([str(binary)], cwd=output,
                                      capture_output=True, timeout=20, check=False)
            if observed.returncode or observed.stderr:
                raise RuntimeError(
                    f"Student program {source.name} terminated abnormally: "
                    f"exit={observed.returncode}, stderr={observed.stderr!r}")
            actual = observed.stdout.replace(b"\r\n", b"\n")
            should_match = kind == b"A"
            if (actual == expected) != should_match:
                raise RuntimeError(
                    f"{source.name}: {'accepted' if should_match else 'rejected'} "
                    f"local-int case did not match its real compiled behavior; "
                    f"actual={actual!r}, expected={expected!r}")
            if should_match:
                accepted += 1
            else:
                rejected += 1
        if (accepted, rejected) != (24, 6):
            raise RuntimeError(f"Incorrect execution counts: {(accepted, rejected)}")
        print(f"PASS {args.compiler} on {sys.platform}: {accepted} positive "
              f"original/probe executions, {rejected} actual hard-coded-copy "
              "counterexamples rejected; 30 real C++ compile-and-run cases.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.TimeoutExpired, subprocess.CalledProcessError,
            RuntimeError, ValueError) as error:
        print(f"FAIL local-int real compilation: {error}", file=sys.stderr)
        sys.exit(1)

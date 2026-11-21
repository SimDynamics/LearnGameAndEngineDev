"""Run the native U6L5 private ownership witness against the real authored C++.

Only the source of the existing editor and the header-only generic probe are
used. All ten variants compile and execute in a disposable directory; the
student's original files, state and curriculum are never modified.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "estado_y_ciclo_de_vida_de_clases/"
    "lesson_transferir_propiedad_entre_unique_ptr.json"
)
OWNER = "block_0021_0002"
MARKER = "PASS native independent two-owner transfer identity and cleanup"
EXPECTED = {
    "correct_move": True,
    "correct_swap_reset": True,
    "correct_release_reset": True,
    "original_no_transfer": False,
    "copy_instead_of_move": False,
    "swap_without_emptying": False,
    "damages_empty_source_destination": False,
    "same_value_without_identity": False,
    "correct_transfer_extra_stdout": False,
    "equal_value_hidden_copy": False,
}


def actual_editor_source() -> tuple[str, dict]:
    document = json.loads(LESSON.read_text(encoding="utf-8-sig"))
    matches = [
        (pi, bi)
        for pi, page in enumerate(document["stateLayout"]["current"])
        for bi, identity in enumerate(page["blocks"])
        if identity == OWNER
    ]
    if len(matches) != 1:
        raise RuntimeError("Real U6L5 editor identity is absent or duplicated")
    pi, bi = matches[0]
    block = document["pages"][pi]["blocks"][bi]
    if block.get("type") != "cpp_compiler" or not block.get("outputCheck", {}).get("cases"):
        raise RuntimeError("Real U6L5 editor lost its source and visible first case")
    source = block.get("initialCode")
    if not isinstance(source, str) or "bool Deposito::transferir(" not in source:
        raise RuntimeError("Real U6L5 original member function was removed")
    first_case = block["outputCheck"]["cases"][0]
    if (not isinstance(first_case.get("standardInput"), str)
            or not isinstance(first_case.get("outputs"), list)
            or not first_case["outputs"]
            or not all(isinstance(value, str) for value in first_case["outputs"])):
        raise RuntimeError("The first public U6L5 stdout oracle is invalid")
    return source, first_case


def build(compiler: str, windows: bool, source: Path, binary: Path, work: Path) -> None:
    if windows:
        command = [
            compiler, "/nologo", "/std:c++latest", "/EHsc",
            "/permissive-", "/Zc:__cplusplus", "/utf-8",
            "/I", str(ROOT / "native"), "/c", str(source),
            f"/Fo:{work / (binary.name + '.obj')}",
        ]
        subprocess.run(command, cwd=work, capture_output=True, text=True,
                       errors="replace", check=True, timeout=120)
        linker = [
            compiler, "/nologo", str(work / (binary.name + ".obj")),
            "/link", f"/OUT:{binary}",
        ]
        subprocess.run(linker, cwd=work, capture_output=True, text=True,
                       errors="replace", check=True, timeout=120)
    else:
        command = [
            compiler, "-std=c++23", "-O0", "-Wall", "-Wextra",
            "-pedantic", "-I", str(ROOT / "native"),
            str(source), "-o", str(binary),
        ]
        subprocess.run(command, cwd=work, capture_output=True, text=True,
                       errors="replace", check=True, timeout=120)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--windows", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l5-ownership-") as location:
        work = Path(location)
        original = work / "original.cpp"
        original_source, first_case = actual_editor_source()
        # Preserve the exact published LF bytes on Windows as well as Linux.
        original.write_bytes(original_source.encode("utf-8"))
        generator = work / ("generate.exe" if args.windows else "generate")
        build(args.compiler, args.windows, ROOT / (
            "native/core/cpp_indexed_unique_transfer_probe_tests.cpp"
        ), generator, work)
        output = work / "private-variants"
        result = subprocess.run(
            [str(generator), str(original), str(output)],
            cwd=work, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=30,
        )
        if result.returncode:
            raise RuntimeError(f"Private probe generation failed: {result.stderr}")
        declared = {}
        for line in result.stdout.splitlines():
            name, _, verdict = line.partition(" ")
            if name in EXPECTED and verdict in ("pass", "reject"):
                declared[name] = verdict == "pass"
        if declared != EXPECTED:
            raise RuntimeError(f"U6L5 generator changed its variants: {declared}")
        for name, should_accept in EXPECTED.items():
            candidate = output / (name + ".cpp")
            if not candidate.is_file():
                raise RuntimeError(f"Private U6L5 variant missing: {name}")
            binary = work / (name + (".exe" if args.windows else ""))
            build(args.compiler, args.windows, candidate, binary, work)
            execution = subprocess.run(
                [str(binary)], cwd=work, capture_output=True,
                text=True, encoding="utf-8", errors="replace", timeout=15,
            )
            # Production requires the complete private marker and no stdout.
            # A substring match could wrongly certify a candidate that also
            # printed unrelated data before the trusted driver finished.
            accepted = (execution.returncode == 0 and
                        execution.stdout == "" and execution.stderr == MARKER)
            if accepted != should_accept:
                raise RuntimeError(
                    f"{name}: expected {'accept' if should_accept else 'reject'}, "
                    f"got exit {execution.returncode}, "
                    f"stdout={execution.stdout[-600:]!r}; "
                    f"stderr_exact={execution.stderr == MARKER}; "
                    f"stderr={execution.stderr[-600:]}"
                )
            print(f"PASS private U6L5 {name}: "
                  f"{'accepted with identity/cleanup' if should_accept else 'rejected'}")
        # A semantic counterexample should pass the *actual* published first
        # visible case. Otherwise it would no longer demonstrate the blind
        # spot that necessitates the independent ownership proof.
        public_source = output / "equal_value_hidden_copy-public.cpp"
        if not public_source.is_file():
            raise RuntimeError("Missing authentic stdout-only counterexample")
        public_binary = work / ("equal_value_hidden_copy_public" +
                                (".exe" if args.windows else ""))
        build(args.compiler, args.windows, public_source, public_binary, work)
        public_result = subprocess.run(
            [str(public_binary)], input=first_case["standardInput"], cwd=work,
            capture_output=True, text=True, encoding="utf-8",
            errors="replace", timeout=15,
        )
        if (public_result.returncode != first_case.get("exitCode", 0)
                or public_result.stdout not in first_case["outputs"]
                or public_result.stderr):
            raise RuntimeError(
                "The ownership counterexample no longer passes the genuine "
                "visible first case; its private rejection cannot justify "
                "this particular regression"
            )
        print("PASS U6L5 false ownership implementation matches the genuine "
              "first public stdout but fails the independent identity witness")
    print("PASS ten U6L5 actual-source private witness executions; "
          "no curriculum metadata or saved learner state changed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Run the native U6L5 private ownership witness against the real authored C++.

Only the source of the existing editor and the header-only generic probe are
used. All seven variants compile and execute in a disposable directory; the
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
    "original_no_transfer": False,
    "copy_instead_of_move": False,
    "swap_without_emptying": False,
    "damages_empty_source_destination": False,
    "same_value_without_identity": False,
}


def actual_editor_source() -> str:
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
    return source


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
        original.write_text(actual_editor_source(), encoding="utf-8")
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
            accepted = execution.returncode == 0 and MARKER in execution.stderr
            if accepted != should_accept:
                raise RuntimeError(
                    f"{name}: expected {'accept' if should_accept else 'reject'}, "
                    f"got exit {execution.returncode}, "
                    f"marker={MARKER in execution.stderr}; "
                    f"stderr={execution.stderr[-600:]}"
                )
            print(f"PASS private U6L5 {name}: "
                  f"{'accepted with identity/cleanup' if should_accept else 'rejected'}")
    print("PASS seven U6L5 actual-source private witness executions; "
          "no curriculum metadata or saved learner state changed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

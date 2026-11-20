"""Compile independent ownership probes using the actual published U6L5 C++ source.

This does not alter course JSON, student progress or the original executable.
Only the function body is replaced; the probe is a separate native binary.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / ("app/src/main/assets/content/es/programming/"
                 "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
                 "lesson_transferir_propiedad_entre_unique_ptr.json")
OWNER = "block_0021_0002"
EXPECTED = {
    "correct_move": True,
    "correct_swap_reset": True,
    "original_no_transfer": False,
    "copy_instead_of_move": False,
    "swap_without_emptying": False,
    "damages_empty_source_destination": False,
    "same_value_without_identity": False,
}


def run(args, *, timeout=90, cwd=None):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                          errors="replace", timeout=timeout, check=False)


def compile_source(compiler, source, output, *, windows):
    if windows:
        command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4",
                   "/WX", "/permissive-", "/Zc:__cplusplus", "/utf-8",
                   "/Inative", str(source), "/Fe:" + str(output)]
    else:
        command = [compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror",
                   "-pedantic", "-Inative", str(source), "-o", str(output)]
    result = run(command, cwd=ROOT, timeout=90)
    if result.returncode:
        raise RuntimeError(f"{compiler} rejected {source.name}:\n"
                           f"{result.stdout}\n{result.stderr}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--windows", action="store_true")
    args = parser.parse_args()
    source = json.loads(LESSON.read_text(encoding="utf-8"))
    assert source["id"].endswith("__transferir_propiedad_entre_unique_ptr")
    at = [(p, i) for p, page in enumerate(source["stateLayout"]["current"])
          for i, identity in enumerate(page["blocks"]) if identity == OWNER]
    assert len(at) == 1, "Published owner not unique"
    p, i = at[0]
    owner = source["pages"][p]["blocks"][i]
    assert owner["type"] == "cpp_compiler" and "outputCheck" in owner
    original = owner["initialCode"]
    assert "bool Deposito::transferir(int origen, int destino)" in original
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l5-transfer-") as temp:
        temp = Path(temp)
        lesson_source = temp / "original.cpp"
        lesson_source.write_text(original, encoding="utf-8")
        suffix = ".exe" if args.windows else ""
        generator = temp / ("generator" + suffix)
        compile_source(args.compiler,
                       ROOT / "native/core/cpp_indexed_unique_transfer_probe_tests.cpp",
                       generator, windows=args.windows)
        generated = temp / "probes"
        proof = run([str(generator), str(lesson_source), str(generated)], timeout=15)
        if proof.returncode:
            raise RuntimeError("Probe generator failed: " + proof.stdout + proof.stderr)
        declarations = dict(line.split() for line in proof.stdout.splitlines()
                            if line.startswith(tuple(EXPECTED)))
        assert declarations == {name: "pass" if accepted else "reject"
                                for name, accepted in EXPECTED.items()}
        for name, expected_success in EXPECTED.items():
            binary = temp / (name + suffix)
            compile_source(args.compiler, generated / (name + ".cpp"), binary,
                           windows=args.windows)
            outcome = run([str(binary)], timeout=10)
            if expected_success:
                assert outcome.returncode == 0 and (
                    "PASS native independent two-owner transfer identity" in outcome.stderr), (
                    name, outcome.returncode, outcome.stdout, outcome.stderr)
            else:
                assert outcome.returncode != 0, (
                    name, "A bad transfer passed the independent identity probe")
            print(f"{args.compiler}: {name}: {'PASS' if expected_success else 'REJECT'}",
                  flush=True)
    print("PASS genuine U6L5 owner: 2 valid equivalent implementations, "
          "5 false positives rejected; no course or learner writes.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL U6L5 transfer probe: {error}", file=sys.stderr)
        sys.exit(1)

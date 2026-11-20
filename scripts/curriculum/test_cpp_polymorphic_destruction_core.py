"""End-to-end verification of native three-body source gate and private probe.

The corpus of correct and incorrect U8L5 solutions comes from the genuine
published lesson. Only temporary fixtures are written; student state and
curriculum JSON are untouched.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

import test_cpp_polymorphic_destruction as fixture

ROOT = Path(__file__).resolve().parents[2]
CPP = ROOT / "native/core/cpp_polymorphic_destruction_check_tests.cpp"


def run(args: list[str], timeout=90) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, check=False, text=True,
                          encoding="utf-8", errors="replace",
                          capture_output=True, timeout=timeout)


def build(compiler: str, source: Path, binary: Path, windows: bool) -> None:
    built = fixture.compile_cpp(compiler, source, binary, windows) if (
        source != CPP
    ) else run(
        [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
         "/permissive-", "/Zc:__cplusplus", "/utf-8", "/Inative",
         str(source), "/Fe:" + str(binary)]
        if windows else
        [compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror",
         "-pedantic", "-Inative", str(source), "-o", str(binary)]
    )
    if built.returncode:
        raise AssertionError(("C++ tool failed", source, built.stdout, built.stderr))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--windows", action="store_true")
    arguments = parser.parse_args()
    original=fixture.original_editor()
    cases = {
        "direct_derived_factory": (
            fixture.GOOD_BASE, fixture.GOOD_DERIVED,
            fixture.GOOD_FACTORY, True),
        "independent_move_factory": (
            fixture.GOOD_BASE_ALT, fixture.GOOD_DERIVED_ALT,
            fixture.GOOD_FACTORY_ALT, True),
        **{
            name: (body, fixture.GOOD_DERIVED, fixture.GOOD_FACTORY, False)
            for name, body in fixture.BAD_BASE.items()
        },
        **{
            name: (fixture.GOOD_BASE, body, fixture.GOOD_FACTORY, False)
            for name, body in fixture.BAD_DERIVED.items()
        },
        **{
            name: (fixture.GOOD_BASE, fixture.GOOD_DERIVED, body, False)
            for name, body in fixture.BAD_FACTORY.items()
        },
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l5-core-") as dirname:
        folder=Path(dirname)
        suffix=".exe" if arguments.windows else ""
        tool=folder / ("polymorphic_generator" + suffix)
        build(arguments.compiler, CPP, tool, arguments.windows)
        original_path=folder / "published.cpp"
        original_path.write_text(original, encoding="utf-8")
        for name, (base, derived, factory, expected) in cases.items():
            code=folder / (name + ".cpp")
            code.write_text(
                fixture.compose(original, base, derived, factory),
                encoding="utf-8")
            private=folder / (name + ".private.cpp")
            generated=run([str(tool), str(original_path), str(code), str(private)])
            if generated.returncode or not private.is_file():
                raise AssertionError((name, "genuine source gate rejected a "
                                      "legitimate body edit", generated.stdout,
                                      generated.stderr))
            executable=folder / (name + suffix)
            build(arguments.compiler, private, executable, arguments.windows)
            observed=run([str(executable)], timeout=16)
            passed=(observed.returncode == 0 and observed.stdout == "" and
                    observed.stderr == fixture.MARKER)
            if passed != expected:
                raise AssertionError(
                    (name, "wrong private semantic verdict",
                     observed.returncode, observed.stdout, observed.stderr))
            print(f"{arguments.compiler}: {name}: "
                  f"{'PASS' if passed else 'REJECT'}", flush=True)

        valid=fixture.compose(
            original, fixture.GOOD_BASE, fixture.GOOD_DERIVED,
            fixture.GOOD_FACTORY)
        forged={
            "mutated_main": valid.replace("int main() {", "int main(int argc) {", 1),
            "mutated_virtual_declaration": valid.replace(
                "virtual ~Medida() noexcept;",
                "~Medida() noexcept;", 1),
            "mutated_derived_constructor": valid.replace(
                "++balance_.derivadas_vivas;",
                "balance_.derivadas_vivas += 2;", 1),
            "append_extra_function": valid + "\nint hidden() { return 1; }\n",
            "added_line_directive": valid.replace(
                "    --ciclo_.bases_vivas;",
                "    #define DESTRUCTOR_OVERRIDE 1\n    --ciclo_.bases_vivas;", 1),
            "escaped_function_body": valid.replace(
                "    --ciclo_.bases_vivas;",
                "}\nvoid injected() {}\nvoid proxy() {\n    --ciclo_.bases_vivas;", 1),
        }
        for name, code in forged.items():
            file=folder / (name + ".cpp")
            file.write_text(code, encoding="utf-8")
            generated=run([str(tool), str(original_path), str(file),
                           str(folder / (name + ".private.cpp"))])
            if generated.returncode == 0:
                raise AssertionError((name, "protected student source was altered"))
            print(f"{arguments.compiler}: {name}: REJECT", flush=True)
    print("PASS original-source U8L5 three-body private code and "
          "counterexamples; no history or curriculum files modified.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL U8L5 private core proof: {error}", file=sys.stderr)
        sys.exit(1)

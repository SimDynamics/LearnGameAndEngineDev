"""End-to-end verification of native three-body source gate and private probe.

The corpus of correct and incorrect U8L5 solutions comes from the genuine
published lesson. Only temporary fixtures are written; student state and
curriculum JSON are untouched.
"""
from __future__ import annotations

import argparse
import json
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
    # Resolve every oracle through the authentic current stable owner.
    # No answer is inferred from the source or a copied example.
    lesson=json.loads(fixture.LESSON.read_text(encoding="utf-8"))
    owners=[(i,j) for i,page in enumerate(lesson["stateLayout"]["current"])
            for j,identity in enumerate(page["blocks"]) if identity==fixture.OWNER]
    if len(owners)!=1:
        raise AssertionError("Original U8L5 owner is missing or duplicated")
    i,j=owners[0]
    editor=lesson["pages"][i]["blocks"][j]
    if editor["type"]!="cpp_compiler" or editor["initialCode"]!=original:
        raise AssertionError("U8L5 public editor differs from native probe source")
    public_cases=editor["outputCheck"]["cases"]
    if len(public_cases)!=5:
        raise AssertionError("U8L5 published public case count changed")
    for index,case in enumerate(public_cases,1):
        if (not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or
            not case["outputs"] or
            not all(isinstance(value,str) for value in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise AssertionError(f"Invalid published U8L5 case {index}")
    cases = {
        "direct_derived_factory": (
            fixture.GOOD_BASE, fixture.GOOD_DERIVED,
            fixture.GOOD_FACTORY, True),
        "independent_move_factory": (
            fixture.GOOD_BASE_ALT, fixture.GOOD_DERIVED_ALT,
            fixture.GOOD_FACTORY_ALT, True),
        "commented_cycle_declaration": (
            fixture.GOOD_BASE, fixture.GOOD_DERIVED,
            fixture.GOOD_FACTORY, True),
        "independent_namespaced_types": (
            fixture.GOOD_BASE, fixture.GOOD_DERIVED,
            fixture.GOOD_FACTORY, True),
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
        original_path.write_bytes(original.encode("utf-8"))
        # A destructor that assigns zero to a live-object counter happens to
        # pass all five public cases (which hold only one object at a time).
        # The separately compiled driver must reject it with two live owners.
        public_variants={
            "direct_derived_factory","independent_move_factory",
            "commented_cycle_declaration","independent_namespaced_types",
            "base_resets_all_live_objects"
        }
        public_runs=0
        for name, (base, derived, factory, expected) in cases.items():
            code=folder / (name + ".cpp")
            source_for_case=original_path
            candidate=fixture.compose(original, base, derived, factory)
            if name=="commented_cycle_declaration":
                # This sentence used to close Ciclo before its real fields.
                note="// Example only: struct Ciclo { }; class Ficha {\n"
                source_for_case=folder / "commented-original.cpp"
                source_for_case.write_bytes((note+original).encode("utf-8"))
                candidate=note+candidate
            if name=="independent_namespaced_types":
                # Unrelated namespaced classes must not shadow or replace
                # the genuine global Ciclo and Ficha used by the original.
                extra=("\nnamespace LgaedOther {\n"
                       "struct Ciclo { int unrelated=0; };\n"
                       "class Ficha {};\n"
                       "class MedidaIdentificada {};\n"
                       "}\n")
                source_for_case=folder/"namespaced-original.cpp"
                source_for_case.write_bytes((original+extra).encode("utf-8"))
                candidate+=extra
            code.write_bytes(candidate.encode("utf-8"))
            private=folder / (name + ".private.cpp")
            generated=run([str(tool), str(source_for_case), str(code), str(private)])
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
            # Public cases 2-5 remain optional for the learner. Running
            # them in disposable QA must never replace the private semantic
            # requirement or create a learner completion receipt.
            if name in public_variants:
                public_binary=folder/(name+"-public"+suffix)
                build(arguments.compiler,code,public_binary,arguments.windows)
                for index,case in enumerate(public_cases,1):
                    visible=subprocess.run(
                        [str(public_binary)],cwd=ROOT,
                        input=case["standardInput"],text=True,
                        encoding="utf-8",errors="replace",
                        capture_output=True,check=False,timeout=15)
                    if (visible.returncode!=case.get("exitCode",0) or
                        visible.stdout not in case["outputs"] or
                        visible.stderr):
                        raise AssertionError(
                            (name,index,"Authentic public U8L5 result changed",
                             visible.returncode,visible.stdout[-500:],
                             visible.stderr[-500:]))
                    public_runs+=1
                print(f"{arguments.compiler}: public {name}: "
                      f"{len(public_cases)} authentic cases PASS; "
                      f"private semantics {'PASS' if passed else 'REJECT'}",
                      flush=True)
        if public_runs!=len(public_variants)*len(public_cases):
            raise AssertionError("U8L5 public/private comparison omitted a case")

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
    print("PASS original U8L5 source and three-body private probe: "
          "four valid implementations and the false-positive destructor "
          "matched all five authentic public cases, but only the valid "
          "implementations satisfied the private lifetime checks. "
          "No history or curriculum files modified.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL U8L5 private core proof: {error}", file=sys.stderr)
        sys.exit(1)

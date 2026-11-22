"""Source-bound U8L7 C++ probe, using the original protected class and main.

All fixture files are temporary. Two distinct safe implementations and the
direct-assignment idiom must pass; eleven-plus defective implementations and
changes to supplied declarations must be rejected.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import test_cpp_derived_preparation as fixture

ROOT=Path(__file__).resolve().parents[2]
GENERATOR=ROOT/"native/core/cpp_derived_preparation_check_tests.cpp"


def run(args: list[str],timeout=90) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args,cwd=ROOT,check=False,text=True,
                          encoding="utf-8",errors="replace",
                          capture_output=True,timeout=timeout)


def build_generator(compiler: str,path: Path,output: Path,windows: bool):
    if windows:
        args=[compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
              "/permissive-","/Zc:__cplusplus","/utf-8","/Inative",
              str(path),"/Fe:"+str(output)]
    else:
        args=[compiler,"-std=c++23","-Wall","-Wextra","-Werror",
              "-pedantic","-Inative",str(path),"-o",str(output)]
    outcome=run(args)
    if outcome.returncode:
        raise AssertionError(("Generator compilation failed",
                              outcome.stdout,outcome.stderr))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=fixture.original_editor()
    # Resolve the public oracles through the original stable lesson owner.
    # The native private driver does not infer any expected output from code.
    lesson=json.loads(fixture.LESSON.read_text(encoding="utf-8"))
    owners=[(p,b) for p,page in enumerate(lesson["stateLayout"]["current"])
            for b,identity in enumerate(page["blocks"])
            if identity==fixture.OWNER]
    if len(owners)!=1:
        raise AssertionError("U8L7 current owner is absent or duplicated")
    p,b=owners[0]
    editor=lesson["pages"][p]["blocks"][b]
    if editor["type"]!="cpp_compiler" or editor["initialCode"]!=original:
        raise AssertionError("U8L7 original source changed between readers")
    public_cases=editor["outputCheck"]["cases"]
    if len(public_cases)!=5:
        raise AssertionError("U8L7 published public case count changed")
    for index,case in enumerate(public_cases,1):
        if (not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or
            not case["outputs"] or
            not all(isinstance(x,str) for x in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise AssertionError(f"Invalid U8L7 published case {index}")
    examples={
        **{name:(body,True) for name,body in fixture.GOOD.items()},
        "documented_balance_contract":(fixture.GOOD["prepare_then_move"],True),
        **{name:(body,False) for name,body in fixture.BAD.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l7-private-") as directory:
        folder=Path(directory)
        suffix=".exe" if args.windows else ""
        generator=folder/("source_gate"+suffix)
        build_generator(args.compiler,GENERATOR,generator,args.windows)
        published=folder/"original.cpp"
        published.write_bytes(original.encode("utf-8"))
        # Every published case starts with an occupied destination. A
        # candidate may be built correctly yet discarded when an initially
        # empty destination is passed by another caller. That source must
        # pass stdout and fail the independent private ownership test.
        public_variants=set(fixture.GOOD)|{
            "documented_balance_contract",
            "discards_completed_candidate_for_empty_destination"
        }
        public_runs=0
        for name,(body,expected) in examples.items():
            source=folder/(name+".cpp")
            source_for_case=published
            candidate=fixture.compose(original,body)
            if name=="documented_balance_contract":
                # Former literal matching ended the Balance scope at this
                # fake, commented "};" before seeing any actual member.
                note="// struct Balance { }; class Recurso {\n"
                source_for_case=folder/"documented-original.cpp"
                source_for_case.write_bytes((note+original).encode("utf-8"))
                candidate=note+candidate
            source.write_bytes(candidate.encode("utf-8"))
            private=folder/(name+".private.cpp")
            generated=run([str(generator),str(source_for_case),str(source),str(private)])
            if generated.returncode or not private.is_file():
                raise AssertionError((name,"Valid editable body rejected",
                                      generated.stdout,generated.stderr))
            binary=folder/(name+suffix)
            built=fixture.compile_cpp(args.compiler,private,binary,args.windows)
            if built.returncode:
                raise AssertionError((name,"Private C++ did not compile",
                                      built.stdout,built.stderr))
            result=run([str(binary)],timeout=16)
            success=(result.returncode==0 and result.stdout=="" and
                     result.stderr==fixture.MARKER)
            if success!=expected:
                raise AssertionError((name,"Wrong ownership semantics",
                                      result.returncode,result.stdout,result.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if success else 'REJECT'}",flush=True)
            # Only the first public case is mandatory for the learner.
            # The remaining cases run automatically in isolated QA, never
            # as saved progress or as substitutes for the private witness.
            if name in public_variants:
                public_binary=folder/(name+"-public"+suffix)
                built=fixture.compile_cpp(
                    args.compiler,source,public_binary,args.windows)
                if built.returncode:
                    raise AssertionError(
                        (name,"Original U8L7 student main cannot compile",
                         built.stdout,built.stderr))
                for index,case in enumerate(public_cases,1):
                    observed=subprocess.run(
                        [str(public_binary)],cwd=ROOT,
                        input=case["standardInput"],text=True,
                        encoding="utf-8",errors="replace",
                        capture_output=True,check=False,timeout=15)
                    if (observed.returncode!=case.get("exitCode",0) or
                        observed.stdout not in case["outputs"] or
                        observed.stderr):
                        raise AssertionError(
                            (name,index,"Authentic U8L7 public result changed",
                             observed.returncode,observed.stdout[-500:],
                             observed.stderr[-500:]))
                    public_runs+=1
                print(f"{args.compiler}: public {name}: "
                      f"{len(public_cases)} authentic cases PASS; "
                      f"private semantics {'PASS' if success else 'REJECT'}",
                      flush=True)
        if public_runs!=len(public_variants)*len(public_cases):
            raise AssertionError("U8L7 public/private QA omitted a variant")

        correct=fixture.compose(original,fixture.GOOD["prepare_then_swap"])
        forged={
            "alter_base_destructor":correct.replace(
                "virtual ~Medida() noexcept { ++balance_.bases_destruidas; }",
                "virtual ~Medida() noexcept { balance_.bases_destruidas += 2; }",1),
            "alter_derived_constructor":correct.replace(
                "++balance_.derivadas_completadas;",
                "balance_.derivadas_completadas += 2;",1),
            "change_main":correct.replace(
                "int main() {","int main(int unused) {",1),
            "alter_failure_stage":correct.replace(
                "if (modo == 3) { throw Interrupcion{3}; }",
                "if (modo == 3) { throw Interrupcion{1}; }",1),
            "replace_signature":correct.replace(
                fixture.SIGNATURE,
                fixture.SIGNATURE.replace("int modo","int otro"),1),
            "append_hidden_function":correct+"\nint hidden() { return 1; }\n",
            "preprocessor_directive":correct.replace(
                "    std::unique_ptr<Medida> candidate=",
                "    #define IGNORE_FAILURE 1\n    std::unique_ptr<Medida> candidate=",1),
            "escape_body":correct.replace(
                "    std::unique_ptr<Medida> candidate=",
                "}\nvoid injected() {}\nvoid another() {\n"
                "    std::unique_ptr<Medida> candidate=",1),
        }
        for name,code in forged.items():
            source=folder/(name+".cpp")
            source.write_text(code,encoding="utf-8")
            attempted=run([str(generator),str(published),str(source),
                           str(folder/(name+".private.cpp"))])
            if attempted.returncode==0:
                raise AssertionError((name,"Changed immutable source was accepted"))
            print(f"{args.compiler}: {name}: REJECT",flush=True)
    print("PASS U8L7 authenticated source and private derived preparation: "
          "four safe approaches and thirteen invalid semantics tested; "
          "all five real public cases also compared for the safe approaches "
          "and the empty-destination false positive. Original history intact.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L7 private ownership proof: {error}",file=sys.stderr)
        sys.exit(1)

"""Source-bound U8L7 C++ probe, using the original protected class and main.

All fixture files are temporary. Two distinct safe implementations and the
direct-assignment idiom must pass; eleven-plus defective implementations and
changes to supplied declarations must be rejected.
"""
from __future__ import annotations

import argparse
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
    examples={
        **{name:(body,True) for name,body in fixture.GOOD.items()},
        **{name:(body,False) for name,body in fixture.BAD.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l7-private-") as directory:
        folder=Path(directory)
        suffix=".exe" if args.windows else ""
        generator=folder/("source_gate"+suffix)
        build_generator(args.compiler,GENERATOR,generator,args.windows)
        published=folder/"original.cpp"
        published.write_text(original,encoding="utf-8")
        for name,(body,expected) in examples.items():
            source=folder/(name+".cpp")
            source.write_text(fixture.compose(original,body),encoding="utf-8")
            private=folder/(name+".private.cpp")
            generated=run([str(generator),str(published),str(source),str(private)])
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
    print("PASS U8L7 authentic one-body source, three safe approaches, "
          "twelve invalid semantics and eight forged source changes rejected.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L7 private ownership proof: {error}",file=sys.stderr)
        sys.exit(1)

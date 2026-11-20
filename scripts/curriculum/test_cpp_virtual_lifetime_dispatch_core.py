"""Compile and run the genuine source-bound U8L6 private dispatch probe.

The generated driver must accept different correct function bodies and
reject semantic counterexamples, while refusing changes to published
declarations, constructors, destructors, main or saved output cases.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

import test_cpp_virtual_lifetime_dispatch as fixture

ROOT=Path(__file__).resolve().parents[2]
GENERATOR=ROOT/"native/core/cpp_virtual_lifetime_dispatch_check_tests.cpp"


def run(args: list[str], timeout=90) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args,cwd=ROOT,check=False,text=True,
                          encoding="utf-8",errors="replace",
                          capture_output=True,timeout=timeout)


def compile_generator(compiler: str,source: Path,binary: Path,windows: bool):
    if windows:
        args=[compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
              "/permissive-","/Zc:__cplusplus","/utf-8","/Inative",
              str(source),"/Fe:"+str(binary)]
    else:
        args=[compiler,"-std=c++23","-Wall","-Wextra","-Werror",
              "-pedantic","-Inative",str(source),"-o",str(binary)]
    result=run(args)
    if result.returncode:
        raise AssertionError(("Generator compilation failed",
                              result.stdout,result.stderr))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=fixture.original_editor()
    examples={
        "real_virtual_calls":(fixture.GOOD_REG,fixture.GOOD_WRITE,True),
        "equivalent_virtual_calls":(fixture.ALT_REG,fixture.ALT_WRITE,True),
        **{name:(body,fixture.GOOD_WRITE,False)
           for name,body in fixture.BAD_REG.items()},
        **{name:(fixture.GOOD_REG,body,False)
           for name,body in fixture.BAD_WRITE.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l6-compiler-") as folder:
        temp=Path(folder)
        suffix=".exe" if args.windows else ""
        generator=temp/("probe_generator"+suffix)
        compile_generator(args.compiler,GENERATOR,generator,args.windows)
        published=temp/"original.cpp"
        published.write_text(original,encoding="utf-8")
        for name,(reg,write,expected) in examples.items():
            code=temp/(name+".cpp")
            code.write_text(fixture.compose(original,reg,write),encoding="utf-8")
            proof=temp/(name+".private.cpp")
            result=run([str(generator),str(published),str(code),str(proof)])
            if result.returncode or not proof.is_file():
                raise AssertionError((name,"Original source gate rejected editable bodies",
                                      result.stdout,result.stderr))
            exe=temp/(name+suffix)
            built=fixture.compile_cpp(args.compiler,proof,exe,args.windows)
            if built.returncode:
                raise AssertionError((name,"Private source did not compile",
                                      built.stdout,built.stderr))
            observed=run([str(exe)],timeout=16)
            accepted=(observed.returncode==0 and observed.stdout=="" and
                      observed.stderr==fixture.MARKER)
            if accepted!=expected:
                raise AssertionError((name,"Unexpected U8L6 semantic result",
                                      observed.returncode,
                                      observed.stdout,observed.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if accepted else 'REJECT'}",flush=True)
        good=fixture.compose(
            original,fixture.GOOD_REG,fixture.GOOD_WRITE)
        forged={
            "changed_published_main":good.replace(
                "int main() {","int main(int unused) {",1),
            "changed_construction_phase":good.replace(
                'registrar("ConstruirBase", historia_);',
                'registrar("Uso", historia_);',1),
            "changed_virtual_signature":good.replace(
                "virtual void escribir(std::ostream& salida) const",
                "void escribir(std::ostream& salida) const",1),
            "changed_derived_signature":good.replace(
                "void MedidaIdentificada::escribir(std::ostream& salida) const {",
                "void MedidaIdentificada::escribir(std::ostream& otro) const {",1),
            "additional_file_suffix":good+"\nint invented() { return 1; }\n",
            "unexpected_directive":good.replace(
                "    salida << etapa << ':';",
                "    #define OVERRIDE_PHASE 1\n    salida << etapa << ':';",1),
            "escaped_function_body":good.replace(
                "    salida << etapa << ':';",
                "}\nvoid hijack() {}\nvoid proxy() {\n    salida << etapa << ':';",1),
        }
        for name,code in forged.items():
            candidate=temp/(name+".cpp")
            candidate.write_text(code,encoding="utf-8")
            result=run([str(generator),str(published),str(candidate),
                        str(temp/(name+".private.cpp"))])
            if result.returncode==0:
                raise AssertionError((name,"Forged source escaped native boundary"))
            print(f"{args.compiler}: {name}: REJECT",flush=True)
    print("PASS U8L6 independent native source gate, two correct implementations "
          "and twelve semantic counterexamples, protected class and main.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L6 independent core: {error}",file=sys.stderr)
        sys.exit(1)

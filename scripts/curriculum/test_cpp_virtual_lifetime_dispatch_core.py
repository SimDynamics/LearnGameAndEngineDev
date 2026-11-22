"""Compile and run the genuine source-bound U8L6 private dispatch probe.

The generated driver must accept different correct function bodies and
reject semantic counterexamples, while refusing changes to published
declarations, constructors, destructors, main or saved output cases.
"""
from __future__ import annotations

import argparse
import json
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
    lesson=json.loads(fixture.LESSON.read_text(encoding="utf-8"))
    owners=[(p,b) for p,page in enumerate(lesson["stateLayout"]["current"])
            for b,identity in enumerate(page["blocks"])
            if identity==fixture.OWNER]
    if len(owners)!=1:
        raise AssertionError("U8L6 authentic editor owner changed")
    p,b=owners[0]
    editor=lesson["pages"][p]["blocks"][b]
    if editor["type"]!="cpp_compiler" or editor["initialCode"]!=original:
        raise AssertionError("U8L6 original source differs from published editor")
    public_cases=editor["outputCheck"]["cases"]
    if len(public_cases)!=5:
        raise AssertionError("U8L6 published public cases changed")
    for number,case in enumerate(public_cases,1):
        if (not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or not case["outputs"] or
            not all(isinstance(value,str) for value in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise AssertionError(f"Invalid authentic U8L6 public case {number}")
    examples={
        "real_virtual_calls":(fixture.GOOD_REG,fixture.GOOD_WRITE,True),
        "equivalent_virtual_calls":(fixture.ALT_REG,fixture.ALT_WRITE,True),
        "documented_class_contract":(fixture.GOOD_REG,fixture.GOOD_WRITE,True),
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
        published.write_bytes(original.encode("utf-8"))
        # This bad registrar guesses virtual dispatch from the phase label.
        # It reproduces every public case, but fails when an ordinary live
        # derived object is queried using a construction/destruction label.
        public_variants={
            "real_virtual_calls","equivalent_virtual_calls",
            "documented_class_contract","guesses_virtual_dispatch_from_phase_label"
        }
        public_runs=0
        for name,(reg,write,expected) in examples.items():
            code=temp/(name+".cpp")
            source_for_case=published
            candidate=fixture.compose(original,reg,write)
            if name=="documented_class_contract":
                note=("// class Medida {\n"
                      "// class MedidaIdentificada : public Medida {\n")
                source_for_case=temp/"documented-original.cpp"
                source_for_case.write_bytes((note+original).encode("utf-8"))
                candidate=note+candidate
            code.write_bytes(candidate.encode("utf-8"))
            proof=temp/(name+".private.cpp")
            result=run([str(generator),str(source_for_case),str(code),str(proof)])
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
            # Run the untouched original main in QA, distinct from the
            # instrumented private driver. Only the first case is compulsory
            # for the student; all five are internal regression evidence.
            if name in public_variants:
                public_binary=temp/(name+"-public"+suffix)
                public_compiled=fixture.compile_cpp(
                    args.compiler,code,public_binary,args.windows)
                if public_compiled.returncode:
                    raise AssertionError(
                        (name,"Published U8L6 main failed to compile",
                         public_compiled.stdout,public_compiled.stderr))
                for number,case in enumerate(public_cases,1):
                    visible=subprocess.run(
                        [str(public_binary)],cwd=ROOT,
                        input=case["standardInput"],text=True,
                        encoding="utf-8",errors="replace",
                        capture_output=True,check=False,timeout=15)
                    if (visible.returncode!=case.get("exitCode",0) or
                        visible.stdout not in case["outputs"] or visible.stderr):
                        raise AssertionError(
                            (name,number,"Genuine public U8L6 oracle changed",
                             visible.returncode,
                             visible.stdout[-500:],visible.stderr[-500:]))
                    public_runs+=1
                print(f"{args.compiler}: public {name}: "
                      f"{len(public_cases)} authored cases PASS; "
                      f"private dispatch {'PASS' if accepted else 'REJECT'}",
                      flush=True)
        if public_runs!=len(public_cases)*len(public_variants):
            raise AssertionError("U8L6 public QA omitted an expected variant")
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
    print("PASS U8L6 native source gate, three correct implementations and "
          "twelve counterexamples, authentic source and unchanged main; "
          "all five public cases distinguish neither correct dispatch "
          "from the phase-label shortcut nor replace private semantics.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L6 independent core: {error}",file=sys.stderr)
        sys.exit(1)

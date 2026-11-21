"""Source-bound private U8L8 checker, built with GCC, Clang and MSVC.

Accepted implementations may use either factory-return idiom and virtual
consumer logic. Forged interfaces or changes to uneditable code are denied.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import test_cpp_abstract_interface as fixture

ROOT=Path(__file__).resolve().parents[2]
GENERATOR=ROOT/"native/core/cpp_abstract_interface_check_tests.cpp"


def run(command:list[str],timeout=90)->subprocess.CompletedProcess[str]:
    return subprocess.run(command,cwd=ROOT,check=False,text=True,
                          encoding="utf-8",errors="replace",
                          capture_output=True,timeout=timeout)


def compile_generator(compiler:str,source:Path,binary:Path,windows:bool):
    command=(
        [compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
         "/permissive-","/Zc:__cplusplus","/utf-8","/Inative",
         str(source),"/Fe:"+str(binary)]
        if windows else
        [compiler,"-std=c++23","-Wall","-Wextra","-Werror",
         "-pedantic","-Inative",str(source),"-o",str(binary)]
    )
    output=run(command)
    if output.returncode:
        raise AssertionError(("Original U8L8 generator does not compile",
                              output.stdout,output.stderr))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    arguments=parser.parse_args()
    original=fixture.original_editor()
    # Compare both kinds of evidence against the historical stable owner.
    lesson=json.loads(fixture.LESSON.read_text(encoding="utf-8"))
    found=[(p,b) for p,page in enumerate(lesson["stateLayout"]["current"])
           for b,owner in enumerate(page["blocks"]) if owner==fixture.OWNER]
    if len(found)!=1:
        raise AssertionError("U8L8 published editor ID is absent or duplicated")
    p,b=found[0]
    editor=lesson["pages"][p]["blocks"][b]
    if editor["type"]!="cpp_compiler" or editor["initialCode"]!=original:
        raise AssertionError("U8L8 source changed between authentic readers")
    public_cases=editor["outputCheck"]["cases"]
    if len(public_cases)!=5:
        raise AssertionError("The authentic U8L8 public oracle count changed")
    for number,case in enumerate(public_cases,1):
        if (not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or
            not case["outputs"] or
            not all(isinstance(text,str) for text in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise AssertionError(f"Invalid authentic U8L8 case {number}")
    inert_simple=r"""
    // std::fputs("LGAED_ABSTRACT_INTERFACE_COMPLETE_V1",stderr);
    const char* note=R"qa(MuestraAdicional std::longjmp(0,1) stderr)qa";
    (void)note;
    salida << "Numero:" << numero();
"""
    solutions={
        "correct_direct":(fixture.SIMPLE_WRITE,fixture.IDENTIFIED_WRITE,
                          fixture.CREATE,fixture.PRESENT,True),
        "correct_local_owner":(fixture.ALT_SIMPLE,fixture.ALT_IDENTIFIED,
                               fixture.ALT_CREATE,fixture.ALT_PRESENT,True),
        "correct_inert_qa_words":(inert_simple,fixture.IDENTIFIED_WRITE,
                                  fixture.CREATE,fixture.PRESENT,True),
        "unrelated_namespaced_base":(fixture.SIMPLE_WRITE,fixture.IDENTIFIED_WRITE,
                                    fixture.CREATE,fixture.PRESENT,True),
        **{name:(*bodies,False) for name,bodies in fixture.BAD.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l8-private-") as work:
        folder=Path(work)
        suffix=".exe" if arguments.windows else ""
        generator=folder/("generator"+suffix)
        compile_generator(arguments.compiler,GENERATOR,generator,arguments.windows)
        published=folder/"original.cpp"
        published.write_bytes(original.encode("utf-8"))
        # A caller that recognizes only the two published classes prints
        # identical stdout for every authored case, but is not polymorphic
        # for a third valid implementation. The private driver must reject it.
        public_variants={
            "correct_direct","correct_local_owner","unrelated_namespaced_base",
            "presenter_known_types_only"
        }
        public_runs=0
        for name,record in solutions.items():
            *bodies,expected=record
            candidate=folder/(name+".cpp")
            source_for_case=published
            code=fixture.compose(original,tuple(bodies))
            if name=="unrelated_namespaced_base":
                note="namespace LgaedOther { class Muestra {}; }\n"
                source_for_case=folder/"namespaced-original.cpp"
                source_for_case.write_bytes((note+original).encode("utf-8"))
                code=note+code
            candidate.write_bytes(code.encode("utf-8"))
            private=folder/(name+".private.cpp")
            generated=run([str(generator),str(source_for_case),str(candidate),str(private)])
            if name=="presenter_writes_to_stdout":
                # A diagnostic printed to stdout used to fail only after
                # executing the independent witness. The private-source gate
                # must now reject that exact invalid edit before compilation.
                if generated.returncode==0 or private.is_file():
                    raise AssertionError((name,"A direct stdout write bypassed the private gate"))
                print(f"{arguments.compiler}: {name}: REJECT (source gate)",flush=True)
                continue
            if generated.returncode or not private.is_file():
                raise AssertionError((name,"The source gate rejected editable code",
                                      generated.stdout,generated.stderr))
            executable=folder/(name+suffix)
            built=fixture.compile_cpp(arguments.compiler,private,executable,
                                      arguments.windows)
            if built.returncode:
                raise AssertionError((name,"Generated private probe failed compilation",
                                      built.stdout,built.stderr))
            observed=run([str(executable)],timeout=15)
            passed=observed.returncode==0 and observed.stdout=="" and (
                observed.stderr==fixture.MARKER)
            if passed!=expected:
                raise AssertionError((name,"Incorrect private interface verdict",
                                      observed.returncode,
                                      observed.stdout,observed.stderr))
            print(f"{arguments.compiler}: {name}: "
                  f"{'PASS' if passed else 'REJECT'}",flush=True)
            # The student still owes only the first public case. Cases 2-5
            # remain optional; all five are executed here as isolated QA.
            if name in public_variants:
                public_binary=folder/(name+"-public"+suffix)
                built=fixture.compile_cpp(
                    arguments.compiler,candidate,public_binary,arguments.windows)
                if built.returncode:
                    raise AssertionError(
                        (name,"Original U8L8 program no longer compiles",
                         built.stdout,built.stderr))
                for number,case in enumerate(public_cases,1):
                    visible=subprocess.run(
                        [str(public_binary)],cwd=ROOT,
                        input=case["standardInput"],text=True,
                        encoding="utf-8",errors="replace",
                        capture_output=True,check=False,timeout=15)
                    if (visible.returncode!=case.get("exitCode",0) or
                        visible.stdout not in case["outputs"] or visible.stderr):
                        raise AssertionError(
                            (name,number,"Original U8L8 public output changed",
                             visible.returncode,visible.stdout[-500:],
                             visible.stderr[-500:]))
                    public_runs+=1
                print(f"{arguments.compiler}: public {name}: "
                      f"{len(public_cases)} authored cases PASS; "
                      f"private polymorphism {'PASS' if passed else 'REJECT'}",
                      flush=True)
        if public_runs!=len(public_variants)*len(public_cases):
            raise AssertionError("U8L8 original-public regression lost a case")

        valid=fixture.compose(original,(
            fixture.SIMPLE_WRITE,fixture.IDENTIFIED_WRITE,
            fixture.CREATE,fixture.PRESENT))
        attacks={
            "remove_pure_number":valid.replace(
                "virtual int numero() const noexcept = 0;",
                "virtual int numero() const noexcept;",1),
            "remove_virtual_base_destructor":valid.replace(
                "virtual ~Muestra() = default;","~Muestra() = default;",1),
            "change_inheritance":valid.replace(
                "class MuestraSimple : public Muestra {",
                "class MuestraSimple : private Muestra {",1),
            "change_number_getter":valid.replace(
                "int numero() const noexcept override { return numero_; }",
                "int numero() const noexcept override { return 9; }",1),
            "change_concrete_constructor":valid.replace(
                "explicit MuestraSimple(int numero) : numero_{numero} {}",
                "explicit MuestraSimple(int numero) : numero_{numero+1} {}",1),
            "change_main":valid.replace("int main() {","int main(int unused) {",1),
            "add_trailing_function":valid+"\nint hidden() { return 1; }\n",
            "inject_directive":valid.replace(
                "    salida << \"Numero:\" << numero();",
                "    #define NO_VIRTUAL_DISPATCH 1\n"
                "    salida << \"Numero:\" << numero();",1),
            "escape_inline_body":valid.replace(
                "    salida << \"Numero:\" << numero();",
                "}\nvoid injected() {}\nvoid another() {\n"
                "    salida << \"Numero:\" << numero();",1),
            "change_factory_signature":valid.replace(
                "std::unique_ptr<Muestra> crear(int tipo, int numero, int codigo) {",
                "std::unique_ptr<Muestra> crear(int tipo, int number, int codigo) {",1),
            "forged_private_completion":valid.replace(
                '    salida << "Numero:" << numero();',
                '    std::fputs("LGAED_ABSTRACT_INTERFACE_COMPLETE_V1",stderr);\n'
                '    salida << "Numero:" << numero();',1),
            "private_derived_type":valid.replace(
                '    salida << "Numero:" << numero();',
                '    (void)sizeof(MuestraAdicional);\n'
                '    salida << "Numero:" << numero();',1),
            "private_main_name":valid.replace(
                '    salida << "Numero:" << numero();',
                '    (void)sizeof(lgaed_published_original_main);\n'
                '    salida << "Numero:" << numero();',1),
            "nonlocal_jump":valid.replace(
                '    salida << "Numero:" << numero();',
                '    std::longjmp(0,1);\n'
                '    salida << "Numero:" << numero();',1),
            "direct_console_output":valid.replace(
                '    salida << "Numero:" << numero();',
                '    std::cerr << "Numero:" << numero();\n'
                '    salida << "Numero:" << numero();',1),
        }
        for name,source in attacks.items():
            candidate=folder/(name+".cpp")
            candidate.write_text(source,encoding="utf-8")
            private=folder/(name+".private.cpp")
            observed=run([str(generator),str(published),str(candidate),str(private)])
            if observed.returncode==0:
                raise AssertionError((name,"Protected original source was editable"))
            print(f"{arguments.compiler}: {name}: REJECT",flush=True)
    print("PASS U8L8 real source-bound polymorphism: four correct solutions, "
          "fifteen semantic defects and protected source-probe attacks; "
          "all five authentic public cases match even for the known-types-only "
          "counterexample, which the private third-type probe rejects.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L8 independent abstract-interface proof: {error}",
              file=sys.stderr)
        sys.exit(1)

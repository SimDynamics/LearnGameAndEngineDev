"""Source-bound private U8L8 checker, built with GCC, Clang and MSVC.

Accepted implementations may use either factory-return idiom and virtual
consumer logic. Forged interfaces or changes to uneditable code are denied.
"""
from __future__ import annotations

import argparse
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
        **{name:(*bodies,False) for name,bodies in fixture.BAD.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l8-private-") as work:
        folder=Path(work)
        suffix=".exe" if arguments.windows else ""
        generator=folder/("generator"+suffix)
        compile_generator(arguments.compiler,GENERATOR,generator,arguments.windows)
        published=folder/"original.cpp"
        published.write_text(original,encoding="utf-8")
        for name,record in solutions.items():
            *bodies,expected=record
            candidate=folder/(name+".cpp")
            candidate.write_text(fixture.compose(original,tuple(bodies)),
                                 encoding="utf-8")
            private=folder/(name+".private.cpp")
            generated=run([str(generator),str(published),str(candidate),str(private)])
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
    print("PASS U8L8 real source-bound polymorphism: three correct solutions, "
          "fifteen semantic defects and fifteen immutable/source-probe attacks checked.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L8 independent abstract-interface proof: {error}",
              file=sys.stderr)
        sys.exit(1)

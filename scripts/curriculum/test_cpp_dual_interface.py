"""Compile authentic dual-base U10L1 programs and safely reject false stdout.

Only the two original Muestra member bodies are editable. Both interface
declarations, the concrete object, consumer, published main and three visible
inputs are read-only. The generated private controller observes two base views
over one live object and independent streams before/after every mutation.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/"
             "cpp_language_memory_oop/clases_con_varias_bases/"
             "lesson_reunir_dos_bases_independientes_en_un_objeto.json")
GENERATOR=ROOT/"native/core/cpp_dual_interface_check_tests.cpp"
OWNER="block_0007_0011"
MARKER="LGAED_DUAL_INTERFACE_COMPLETE_V1"

VALUE=" return valor_; "
WRITE=r"""
        salida << codigo_ << '=' << valor_;
"""
ALT_VALUE=" const int actual=this->valor_; return actual; "
ALT_WRITE=r"""
        salida << codigo_;
        salida.put('=');
        salida << valor();
"""
BAD={
    "value_zero":(" return 0; ",WRITE),
    "value_code":(" return codigo_; ",WRITE),
    "value_fixed_first_input":(" return 3; ",WRITE),
    "value_loses_negative":(" return valor_ < 0 ? 0 : valor_; ",WRITE),
    "value_replaces_zero":(" return valor_ == 0 ? 1 : valor_; ",WRITE),
    "report_constant":(VALUE,r' salida << "7=3"; '),
    "report_code_only":(VALUE,r" salida << codigo_; "),
    "report_value_only":(VALUE,r" salida << valor_; "),
    "report_swapped":(VALUE,r" salida << valor_ << '=' << codigo_; "),
    "report_wrong_delimiter":(VALUE,r" salida << codigo_ << ':' << valor_; "),
    "report_fixed_code":(VALUE,r" salida << 7 << '=' << valor_; "),
    "report_ignores_negative":(VALUE,
                               r" salida << codigo_ << '=' << (valor_ < 0 ? 0 : valor_); "),
    "report_extra_newline":(VALUE,r" salida << codigo_ << '=' << valor_ << '\n'; "),
    "report_stdout_instead":(VALUE,r" (void)salida; std::cout << codigo_ << '=' << valor_; "),
    "report_no_write":(VALUE,r" (void)salida; "),
}

def published()->str:
    document=json.loads(LESSON.read_text(encoding="utf-8"))
    loc=[(p,b) for p,page in enumerate(document["stateLayout"]["current"])
         for b,name in enumerate(page["blocks"]) if name==OWNER]
    assert len(loc)==1,"Published dual-interface owner not unique"
    pi,bi=loc[0]
    block=document["pages"][pi]["blocks"][bi]
    assert block["type"]=="cpp_compiler"
    assert len(block["outputCheck"]["cases"])==3
    assert block["outputCheck"]["cases"][0]["standardInput"]=="7 3 -2\n"
    code=block["initialCode"]
    assert code.count("class Muestra : public ConValor, public ConInforme {")==1
    return code

def spans(original:str)->list[tuple[int,int]]:
    first="int valor() const override {"
    second="void escribir(std::ostream& salida) const override {"
    i=original.index(first)+len(first)
    e=original.index("}",i)
    j=original.index(second,e)+len(second)
    z=original.index("\n    }\n\n    void cambiar",j)+5
    assert original[e]=="}" and original[z]=="}" and i<e<j<z
    return [(i,e),(j,z)]

def compose(original:str,bodies:tuple[str,str])->str:
    output=original
    for (start,end),body in reversed(list(zip(spans(original),bodies))):
        output=output[:start]+body+output[end:]
    return output

def run(cmd:list[str],timeout:int=80)->subprocess.CompletedProcess[str]:
    return subprocess.run(cmd,cwd=ROOT,check=False,text=True,
                          encoding="utf-8",errors="replace",
                          capture_output=True,timeout=timeout)

def compile_source(compiler:str,source:Path,target:Path,
                   windows:bool,include_native:bool=False)->None:
    cmd=([compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
          "/permissive-","/Zc:__cplusplus","/utf-8",
          *(["/Inative"] if include_native else []),
          str(source),"/Fe:"+str(target)]
         if windows else
         [compiler,"-std=c++23","-Wall","-Wextra","-Werror",
          "-pedantic",
          *(["-Wno-unused-private-field"] if "clang" in compiler else []),
          *(["-Inative"] if include_native else []),
          str(source),"-o",str(target)])
    r=run(cmd)
    if r.returncode:
        raise AssertionError((source.name,"C++ fixture compilation",
                              r.stdout,r.stderr))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=published()
    # Keep the public stdout oracle separate from the private interface
    # contract. Every original case prints through std::cout, so a
    # hardcoded stream can mimic those cases while breaking other callers.
    document=json.loads(LESSON.read_text(encoding="utf-8"))
    positions=[(p,b) for p,page in enumerate(document["stateLayout"]["current"])
               for b,identity in enumerate(page["blocks"]) if identity==OWNER]
    if len(positions)!=1:
        raise AssertionError("U10L1 stable editor owner changed")
    p,b=positions[0]
    block=document["pages"][p]["blocks"][b]
    if block["type"]!="cpp_compiler" or block["initialCode"]!=original:
        raise AssertionError("Original U10L1 C++ code differs from public editor")
    public_cases=block["outputCheck"]["cases"]
    if len(public_cases)!=3 or public_cases[0]["standardInput"]!="7 3 -2\n":
        raise AssertionError("Original U10L1 public cases changed")
    for number,case in enumerate(public_cases,1):
        if (not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or not case["outputs"] or
            not all(isinstance(out,str) for out in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise AssertionError(f"Invalid U10L1 original public case {number}")
    candidates={
        "correct_direct":(VALUE,WRITE,True),
        "correct_method_and_put":(ALT_VALUE,ALT_WRITE,True),
        "documented_dual_contract":(VALUE,WRITE,True),
        **{name:(a,b,False) for name,(a,b) in BAD.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u10l1-dual-") as d:
        folder=Path(d)
        suffix=".exe" if args.windows else ""
        executable=folder/("generator"+suffix)
        compile_source(args.compiler,GENERATOR,executable,args.windows,True)
        original_path=folder/"published.cpp"
        original_path.write_bytes(original.encode("utf-8"))
        # Two correct solutions and a deliberately incorrect output-stream
        # shortcut. All three must pass the original public stdout, while
        # only the two valid ones pass the separately compiled private check.
        public_variants={"correct_direct","correct_method_and_put",
                         "documented_dual_contract","report_stdout_instead"}
        public_runs=0
        for name,(value,report,should_pass) in candidates.items():
            candidate=folder/(name+".cpp")
            source_for_case=original_path
            edited=compose(original,(value,report))
            if name=="documented_dual_contract":
                note=("// class Muestra : public ConValor, public ConInforme {\n"
                      "// virtual int valor() const = 0;\n")
                source_for_case=folder/"documented-original.cpp"
                source_for_case.write_bytes((note+original).encode("utf-8"))
                edited=note+edited
            candidate.write_bytes(edited.encode("utf-8"))
            probe_source=folder/(name+".private.cpp")
            gen=run([str(executable),str(source_for_case),
                     str(candidate),str(probe_source)])
            if gen.returncode or not probe_source.exists():
                raise AssertionError((name,"Source gate rejected bodies",
                                      gen.stdout,gen.stderr))
            private=folder/(name+suffix)
            compile_source(args.compiler,probe_source,private,args.windows)
            observed=run([str(private)],timeout=15)
            success=(observed.returncode==0 and observed.stdout=="" and
                     observed.stderr==MARKER)
            if success!=should_pass:
                raise AssertionError((name,"Wrong dual-interface verdict",
                    observed.returncode,observed.stdout,observed.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if success else 'REJECT'}",flush=True)
            # Cases 2-3 remain optional for the learner; all three run only
            # in disposable QA and never grant a curriculum completion.
            if name in public_variants:
                public_binary=folder/(name+"-public"+suffix)
                compile_source(args.compiler,candidate,public_binary,args.windows)
                for number,case in enumerate(public_cases,1):
                    visible=subprocess.run(
                        [str(public_binary)],cwd=ROOT,
                        input=case["standardInput"],text=True,
                        encoding="utf-8",errors="replace",
                        capture_output=True,check=False,timeout=15)
                    if (visible.returncode!=case.get("exitCode",0) or
                        visible.stdout not in case["outputs"] or visible.stderr):
                        raise AssertionError(
                            (name,number,"Authentic U10L1 public output changed",
                             visible.returncode,visible.stdout[-500:],
                             visible.stderr[-500:]))
                    public_runs+=1
                print(f"{args.compiler}: public {name}: "
                      f"{len(public_cases)} authored cases PASS; "
                      f"private interfaces {'PASS' if success else 'REJECT'}",
                      flush=True)
        if public_runs!=len(public_variants)*len(public_cases):
            raise AssertionError("U10L1 public/private QA omitted a variant")

        valid=compose(original,(VALUE,WRITE))
        attacks={
            "remove_pure_value":valid.replace(
                "virtual int valor() const = 0;",
                "virtual int valor() const;",1),
            "remove_virtual_report_destructor":valid.replace(
                "virtual ~ConInforme() = default;",
                "~ConInforme() = default;",1),
            "inherit_privately":valid.replace(
                "class Muestra : public ConValor, public ConInforme {",
                "class Muestra : private ConValor, public ConInforme {",1),
            "swap_base_order":valid.replace(
                "class Muestra : public ConValor, public ConInforme {",
                "class Muestra : public ConInforme, public ConValor {",1),
            "change_constructor":valid.replace(
                "valor_{valor} {}","valor_{codigo} {}",1),
            "change_change_method":valid.replace(
                "void cambiar(int nuevo) { valor_ = nuevo; }",
                "void cambiar(int nuevo) { codigo_ = nuevo; }",1),
            "change_code_field":valid.replace(
                "int codigo_;","long codigo_;",1),
            "change_consumer":valid.replace(
                'std::cout << "Valor: " << valor.valor()',
                'std::cout << "Valor: " << 0',1),
            "change_main":valid.replace(
                "int main() {","int main(int argc) {",1),
            "append_function":valid+"\nint injected() { return 0; }\n",
            "directive_in_value_body":valid.replace(
                " return valor_; ",
                "\n#define ESCAPE_POLICY 1\n return valor_; ",1),
            "escape_report_body":valid.replace(
                "        salida << codigo_ << '=' << valor_;",
                "}\nvoid injected() {}\nvoid again() {\n"
                "        salida << codigo_ << '=' << valor_;",1),
        }
        for name,source in attacks.items():
            path=folder/(name+".cpp")
            path.write_text(source,encoding="utf-8")
            witness=folder/(name+".private.cpp")
            result=run([str(executable),str(original_path),str(path),str(witness)])
            if result.returncode==0:
                raise AssertionError((name,"Protected source mutation accepted"))
            print(f"{args.compiler}: {name}: REJECT",flush=True)
    print(f"PASS U10L1 two-base contract: three valid, "
          f"{len(BAD)} semantic negatives and {len(attacks)} source attacks; "
          f"three authentic stdout cases also pass for a defective "
          f"std::cout-only reporter rejected by the private stream probe.")

if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U10L1 independent dual-interface QA: {error}",
              file=sys.stderr)
        sys.exit(1)

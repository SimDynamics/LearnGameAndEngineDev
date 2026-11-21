"""Portable semantic QA for a published U8L9 polymorphic-owner collection.

The script does not rewrite any lesson or student state. It compiles a source
gate and the independently generated C++ program for every accepted and
rejected implementation, using GCC/Clang or MSVC as requested.
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
             "cpp_language_memory_oop/composicion_y_herencia_publica/"
             "lesson_reunir_objetos_que_comparten_una_interfaz.json")
GENERATOR=ROOT/"native/core/cpp_interface_collection_check_tests.cpp"
OWNER="block_0015_0002"
MARKER="LGAED_INTERFACE_COLLECTION_COMPLETE_V1"

ADD=("""
    if (!fuente) return false;
    destino.push_back(std::move(fuente));
    return true;
""")
REPORT=(r"""
    salida << "Cantidad:" << datos.size() << '\n';
    for (std::size_t i = 0; i < datos.size(); ++i) {
        salida << i << ':';
        datos[i]->escribir(salida);
        salida << '\n';
    }
""")
ALT_ADD=("""
    if (fuente == nullptr) return false;
    destino.emplace_back(std::move(fuente));
    return true;
""")
ALT_REPORT=(r"""
    salida << "Cantidad:" << datos.size() << '\n';
    std::size_t indice = 0;
    for (const auto& propietario : datos) {
        salida << indice++ << ':';
        propietario->escribir(salida);
        salida.put('\n');
    }
""")
# Every defective implementation is valid C++ and avoids deliberately
# executing undefined behavior. Identity probes, not pointer-use-after-free,
# reveal substitutions and misplaced ownership.
BAD={
    "always_reject":("    (void)destino; (void)fuente; return false;",REPORT),
    "accept_empty":(r"""
    destino.push_back(std::move(fuente));
    return true;
""",REPORT),
    "return_false_after_move":(r"""
    if (!fuente) return false;
    destino.push_back(std::move(fuente));
    return false;
""",REPORT),
    "prepend_reorders":(r"""
    if (!fuente) return false;
    destino.insert(destino.begin(),std::move(fuente));
    return true;
""",REPORT),
    "fake_equal_value":(r"""
    if (!fuente) return false;
    destino.push_back(std::make_unique<MuestraSimple>(fuente->numero()));
    fuente.reset();
    return true;
""",REPORT),
    "retains_old_owner":(r"""
    if (!fuente) return false;
    if (!destino.empty()) {
        std::swap(fuente,destino.back());
    } else destino.push_back(std::move(fuente));
    return true;
""",REPORT),
    "leaves_empty_slot":(r"""
    if (!fuente) return false;
    destino.push_back(std::move(fuente));
    destino.emplace_back();
    return true;
""",REPORT),
    "constant_report":(ADD,'    (void)datos; salida << "Cantidad:0\\n";'),
    "wrong_count":(ADD,REPORT.replace('salida << "Cantidad:" << datos.size()', 'salida << "Cantidad:" << (datos.size()+1)')),
    "reverse_reporting":(ADD,r"""
    salida << "Cantidad:" << datos.size() << '\n';
    for(std::size_t i=datos.size();i>0;--i) {
        salida << i-1 << ':';
        datos[i-1]->escribir(salida);
        salida << '\n';
    }
"""),
    "wrong_output_stream":(ADD,"    (void)salida;\n"+REPORT.replace("salida", "std::cout")),
    "known_types_only":(ADD,r"""
    salida << "Cantidad:" << datos.size() << '\n';
    for(std::size_t i=0;i<datos.size();++i) {
        salida << i << ':';
        if(const auto* a=dynamic_cast<const MuestraSimple*>(datos[i].get()))
            salida << "Numero:" << a->numero();
        else if(const auto* b=dynamic_cast<const MuestraIdentificada*>(datos[i].get()))
            salida << "Numero:" << b->numero() << ";Codigo:" << b->codigo();
        else salida << "No soportado";
        salida << '\n';
    }
"""),
    "report_without_virtual":(ADD,r"""
    salida << "Cantidad:" << datos.size() << '\n';
    for(std::size_t i=0;i<datos.size();++i)
        salida << i << ":Numero:" << datos[i]->numero() << '\n';
"""),
}


def original_editor()->str:
    document=json.loads(LESSON.read_text(encoding="utf-8"))
    locations=[(page,block) for page,entry in enumerate(
        document["stateLayout"]["current"])
        for block,name in enumerate(entry["blocks"]) if name==OWNER]
    assert len(locations)==1,"U8L9 stable owner not found exactly once"
    page,index=locations[0]
    editor=document["pages"][page]["blocks"][index]
    assert editor["type"]=="cpp_compiler" and len(
        editor["outputCheck"]["cases"])==5
    source=editor["initialCode"]
    assert source.count("bool agregar(Muestras& destino, std::unique_ptr<Muestra>& fuente) {")==1
    assert source.count("void informar(const Muestras& datos, std::ostream& salida) {")==1
    assert "using Muestras = std::vector<std::unique_ptr<Muestra>>;" in source
    return source


def spans(source:str)->list[tuple[int,int]]:
    first="bool agregar(Muestras& destino, std::unique_ptr<Muestra>& fuente) {"
    second="void informar(const Muestras& datos, std::ostream& salida) {"
    first_begin=source.index(first)+len(first)
    first_end=source.index("\n}\nvoid informar",first_begin)+1
    second_begin=source.index(second)+len(second)
    second_end=source.index("\n}\nint main",second_begin)+1
    assert source[first_end]=="}" and source[second_end]=="}"
    assert first_begin<first_end<second_begin<second_end
    return [(first_begin,first_end),(second_begin,second_end)]


def compose(original:str,add:str,report:str)->str:
    source=original
    for (begin,end),body in reversed(list(zip(spans(original),(add,report)))):
        source=source[:begin]+body+source[end:]
    return source


def command(compiler:str,source:Path,output:Path,windows:bool,
            include_native:bool=False)->list[str]:
    return ([compiler,"/nologo","/std:c++latest","/EHsc",
             "/W4","/WX","/permissive-","/Zc:__cplusplus","/utf-8",
             *(["/Inative"] if include_native else []),
             str(source),"/Fe:"+str(output)]
            if windows else
            [compiler,"-std=c++23","-Wall","-Wextra","-Werror",
             "-pedantic",*(["-Inative"] if include_native else []),
             str(source),"-o",str(output)])


def run(arguments:list[str],timeout=90)->subprocess.CompletedProcess[str]:
    return subprocess.run(arguments,check=False,cwd=ROOT,
                          capture_output=True,text=True,encoding="utf-8",
                          errors="replace",timeout=timeout)


def ensure_compiled(compiler:str,source:Path,output:Path,windows:bool,
                    include_native:bool=False)->None:
    result=run(command(compiler,source,output,windows,include_native))
    if result.returncode:
        raise AssertionError(("Compiler rejected a QA fixture",
                              source.name,result.stdout,result.stderr))


def main()->None:
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=original_editor()
    # Bind QA to the historical editor and its authored output oracles.
    document=json.loads(LESSON.read_text(encoding="utf-8"))
    locations=[(page,index) for page,item in enumerate(
        document["stateLayout"]["current"])
        for index,identity in enumerate(item["blocks"]) if identity==OWNER]
    if len(locations)!=1:
        raise AssertionError("U8L9 public editor owner is missing or repeated")
    page,index=locations[0]
    editor=document["pages"][page]["blocks"][index]
    if editor["type"]!="cpp_compiler" or editor["initialCode"]!=original:
        raise AssertionError("U8L9 original editor source changed between readers")
    public_cases=editor["outputCheck"]["cases"]
    if len(public_cases)!=5:
        raise AssertionError("U8L9 published public case count changed")
    for number,case in enumerate(public_cases,1):
        if (not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or
            not case["outputs"] or
            not all(isinstance(value,str) for value in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise AssertionError(f"Invalid U8L9 original public case {number}")
    solutions={
        "valid_push":(ADD,REPORT,True),
        "valid_emplace":(ALT_ADD,ALT_REPORT,True),
        **{name:(a,b,False) for name,(a,b) in BAD.items()}
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l9-owners-") as d:
        folder=Path(d)
        suffix=".exe" if args.windows else ""
        generator=folder/("generator"+suffix)
        ensure_compiled(args.compiler,GENERATOR,generator,args.windows,True)
        source=folder/"published.cpp"
        source.write_bytes(original.encode("utf-8"))
        # Only the two published subclasses appear in all five stdout cases.
        # A hardcoded dynamic_cast chain can match each of them while failing
        # to consume a third valid Muestra interface implementation.
        public_variants={"valid_push","valid_emplace","known_types_only"}
        public_runs=0
        for name,(add,report,expected) in solutions.items():
            edited=folder/(name+".cpp")
            edited.write_bytes(compose(original,add,report).encode("utf-8"))
            witness=folder/(name+".private.cpp")
            created=run([str(generator),str(source),str(edited),str(witness)])
            if created.returncode or not witness.is_file():
                raise AssertionError((name,"Source gate denied body-only edit",
                                      created.stdout,created.stderr))
            binary=folder/(name+suffix)
            ensure_compiled(args.compiler,witness,binary,args.windows)
            finished=run([str(binary)],15)
            passed=(finished.returncode==0 and finished.stdout=="" and
                    finished.stderr==MARKER)
            if passed!=expected:
                raise AssertionError((name,"Incorrect semantic verdict",
                                      finished.returncode,
                                      finished.stdout,finished.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if passed else 'REJECT'}",flush=True)
            # The later four published cases remain optional for the learner.
            # Run all five here as isolated QA, never as completion receipts.
            if name in public_variants:
                public_binary=folder/(name+"-public"+suffix)
                ensure_compiled(args.compiler,edited,public_binary,args.windows)
                for number,case in enumerate(public_cases,1):
                    observed=subprocess.run(
                        [str(public_binary)],cwd=ROOT,
                        input=case["standardInput"],text=True,
                        encoding="utf-8",errors="replace",
                        capture_output=True,check=False,timeout=15)
                    if (observed.returncode!=case.get("exitCode",0) or
                        observed.stdout not in case["outputs"] or
                        observed.stderr):
                        raise AssertionError(
                            (name,number,"Original U8L9 public output changed",
                             observed.returncode,observed.stdout[-500:],
                             observed.stderr[-500:]))
                    public_runs+=1
                print(f"{args.compiler}: public {name}: "
                      f"{len(public_cases)} authored cases PASS; "
                      f"private ownership/interface {'PASS' if passed else 'REJECT'}",
                      flush=True)
        if public_runs!=len(public_variants)*len(public_cases):
            raise AssertionError("U8L9 public/private QA omitted a variant")

        valid=compose(original,ADD,REPORT)
        attacks={
            "changed_base":valid.replace("virtual ~Muestra() = default;",
                                        "~Muestra() = default;",1),
            "changed_pure_method":valid.replace(
                "virtual int numero() const noexcept = 0;",
                "virtual int numero() const noexcept;",1),
            "changed_alias":valid.replace(
                "using Muestras = std::vector<std::unique_ptr<Muestra>>;",
                "using Muestras = std::vector<std::unique_ptr<MuestraSimple>>;",1),
            "changed_factory":valid.replace(
                "return std::make_unique<MuestraIdentificada>(numero, codigo);",
                "return std::make_unique<MuestraSimple>(numero);",1),
            "changed_main":valid.replace("int main() {","int main(int count) {",1),
            "source_suffix":valid+"\nint extra_injected() { return 1; }\n",
            "changed_type":valid.replace(
                "class MuestraIdentificada : public Muestra {",
                "class MuestraIdentificada : private Muestra {",1),
            "changed_getter":valid.replace(
                "int codigo() const noexcept { return codigo_; }",
                "int codigo() const noexcept { return 0; }",1),
            "body_directive":valid.replace(
                "    if (!fuente) return false;",
                "    #define HIJACKED 1\n    if (!fuente) return false;",1),
            "body_escape":valid.replace(
                "    if (!fuente) return false;",
                "}\nint injected() { return 1; }\nbool another() {\n"
                "    if (!fuente) return false;",1),
        }
        for name,mutated in attacks.items():
            edited=folder/(name+".cpp")
            edited.write_text(mutated,encoding="utf-8")
            witness=folder/(name+".private.cpp")
            result=run([str(generator),str(source),str(edited),str(witness)])
            if not result.returncode:
                raise AssertionError((name,"Immutable source accepted mutation"))
            print(f"{args.compiler}: {name}: REJECT",flush=True)
    print(f"PASS U8L9 independent ownership/interface proof: "
          f"two valid, {len(BAD)} defective and {len(attacks)} tampered sources; "
          f"all five original public cases match even for the "
          f"known-types-only counterexample, which the private third-type "
          f"and owner-identity probe rejects.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L9 interface collection QA: {error}",file=sys.stderr)
        sys.exit(1)

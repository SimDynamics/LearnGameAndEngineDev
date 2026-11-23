"""Compile real U8L10 polymorphic clones, including safe negative cases.

The original curricular source, its five output cases and student data are
read-only. The native generator accepts exactly the three authored bodies.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import stable_state_layout as layout

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/"
             "cpp_language_memory_oop/composicion_y_herencia_publica/"
             "lesson_copiar_objetos_sin_perder_su_clase_concreta.json")
GENERATOR=ROOT/"native/core/cpp_polymorphic_clone_check_tests.cpp"
OWNER="block_0015_0002"
MARKER="LGAED_POLYMORPHIC_CLONE_COMPLETE_V1"

SIMPLE=r"""
    return std::make_unique<MuestraSimple>(numero());
"""
IDENTIFIED=r"""
    return std::make_unique<MuestraIdentificada>(numero(),codigo());
"""
DUPLICATE=r"""
    Muestras copias;
    for(const auto& propietario:origen)
        copias.push_back(propietario->clonar());
    return copias;
"""

ALT_SIMPLE=r"""
    const int guardado=numero_;
    return std::make_unique<MuestraSimple>(guardado);
"""
ALT_IDENTIFIED=r"""
    const int numero_guardado=numero_,codigo_guardado=codigo_;
    return std::make_unique<MuestraIdentificada>(numero_guardado,codigo_guardado);
"""
ALT_DUPLICATE=r"""
    Muestras resultado;
    resultado.reserve(origen.size());
    for(const auto& original:origen) {
        std::unique_ptr<Muestra> nuevo=original->clonar();
        resultado.emplace_back(std::move(nuevo));
    }
    return resultado;
"""

INERT_SIMPLE=r"""
    // std::fputs("LGAED_POLYMORPHIC_CLONE_COMPLETE_V1", stderr);
    const auto* note=R"proof(std::longjmp(0, 1); LgaedNewKind)proof";
    (void)note;
    return std::make_unique<MuestraSimple>(numero());
"""

BAD={
    "simple_null":("    return {};",IDENTIFIED,DUPLICATE),
    "simple_zero":("    return std::make_unique<MuestraSimple>(0);",IDENTIFIED,DUPLICATE),
    "simple_collapses_values":(
        "    return std::make_unique<MuestraSimple>(numero()==0 ? 1 : 0);",
        IDENTIFIED,DUPLICATE),
    "identified_null":(SIMPLE,"    return {};",DUPLICATE),
    "identified_bad_code":(
        SIMPLE,"    return std::make_unique<MuestraIdentificada>(numero(),0);",
        DUPLICATE),
    "identified_bad_number":(
        SIMPLE,"    return std::make_unique<MuestraIdentificada>(codigo(),codigo());",
        DUPLICATE),
    "identified_wrong_concrete":(
        SIMPLE,"    return std::make_unique<MuestraSimple>(numero());",DUPLICATE),
    "duplicate_always_empty":(
        SIMPLE,IDENTIFIED,"    (void)origen;\n    return {};"),
    "duplicate_skip_first":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    for(std::size_t i=1;i<origen.size();++i)
        result.push_back(origen[i]->clonar());
    return result;
"""),
    "duplicate_reverse":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    for(auto iter=origen.rbegin();iter!=origen.rend();++iter)
        result.push_back((*iter)->clonar());
    return result;
"""),
    "duplicate_only_simple":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    for(const auto& item:origen)
        result.push_back(std::make_unique<MuestraSimple>(item->numero()));
    return result;
"""),
    "duplicate_zero_code":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    for(const auto& item:origen) {
        if(const auto* id=dynamic_cast<const MuestraIdentificada*>(item.get()))
            result.push_back(std::make_unique<MuestraIdentificada>(id->numero(),0));
        else result.push_back(std::make_unique<MuestraSimple>(item->numero()));
    }
    return result;
"""),
    "duplicate_repeats_first":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    for(std::size_t i=0;i<origen.size();++i)
        result.push_back(origen.front()->clonar());
    return result;
"""),
    "duplicate_extra_last":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    for(const auto& item:origen)result.push_back(item->clonar());
    if(!origen.empty())result.push_back(origen.back()->clonar());
    return result;
"""),
    "duplicate_modifies_copies":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    for(const auto& item:origen) {
        auto copy=item->clonar();
        copy->cambiar_numero(99);
        result.push_back(std::move(copy));
    }
    return result;
"""),
    "duplicate_swallows_bad_alloc":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    try {
        for(const auto& item:origen) result.push_back(item->clonar());
    } catch(const std::bad_alloc&) {
        return result;
    }
    return result;
"""),
    "duplicate_known_types_only":(SIMPLE,IDENTIFIED,r"""
    Muestras result;
    for(const auto& item:origen) {
        if(auto* simple=dynamic_cast<const MuestraSimple*>(item.get()))
            result.push_back(simple->clonar());
        else if(auto* id=dynamic_cast<const MuestraIdentificada*>(item.get()))
            result.push_back(id->clonar());
    }
    return result;
"""),
}

def original_editor()->str:
    document=json.loads(LESSON.read_text(encoding="utf-8"))
    layout.validate(document)
    locations=[(page,block) for page,entry in enumerate(
        document["stateLayout"]["current"])
        for block,name in enumerate(entry["blocks"]) if name==OWNER]
    assert len(locations)==1,"U8L10 original stable owner must be unique"
    page,index=locations[0]
    editor=document["pages"][page]["blocks"][index]
    assert editor["type"]=="cpp_compiler" and len(
        editor["outputCheck"]["cases"])==5
    code=editor["initialCode"]
    assert code.count("std::unique_ptr<Muestra> clonar() const override {")==2
    assert code.count("Muestras duplicar(const Muestras& origen) {")==1
    return code


def spans(source:str)->list[tuple[int,int]]:
    method="std::unique_ptr<Muestra> clonar() const override {"
    positions=[]
    for class_signature in ("class MuestraSimple : public Muestra",
                            "class MuestraIdentificada : public Muestra"):
        class_begin,class_end=layout._named_rule_body_span(source,class_signature)
        begin,closing=layout._named_rule_body_span(
            source[class_begin:class_end],method[:-1].rstrip())
        begin,closing=class_begin+begin,class_begin+closing
        assert source[begin-1]=="{" and source[closing]=="}"
        positions.append((begin,closing))
    duplicate="Muestras duplicar(const Muestras& origen) {"
    begin,end=layout._named_rule_body_span(source,duplicate[:-1].rstrip())
    assert source[begin-1]=="{" and source[end]=="}"
    positions.append((begin,end))
    assert all(positions[i][1]<positions[i+1][0] for i in range(2))
    return positions


def compose(original:str,bodies:tuple[str,str,str])->str:
    result=original
    for (start,end),body in reversed(list(zip(spans(original),bodies))):
        result=result[:start]+body+result[end:]
    return result


def run(command:list[str],timeout:int=90)->subprocess.CompletedProcess[str]:
    return subprocess.run(command,cwd=ROOT,check=False,encoding="utf-8",
                          errors="replace",capture_output=True,text=True,
                          timeout=timeout)


def compile_cpp(compiler:str,source:Path,output:Path,windows:bool,
                includes:bool=False)->None:
    args=([compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
           "/permissive-","/Zc:__cplusplus","/utf-8",
           *(["/Inative"] if includes else []),
           str(source),"/Fe:"+str(output)]
          if windows else
          [compiler,"-std=c++23","-Wall","-Wextra","-Werror",
           "-pedantic",*(["-Inative"] if includes else []),
           str(source),"-o",str(output)])
    result=run(args)
    if result.returncode:
        raise AssertionError((source.name,"C++ fixture compile failed",
                              result.stdout,result.stderr))


def main()->None:
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=original_editor()
    # Bind the real student-facing main to all five original stdout cases.
    # These cases are not semantic evidence of clone identity or exception
    # propagation; the separately compiled driver proves those properties.
    document=json.loads(LESSON.read_text(encoding="utf-8"))
    owners=[(page,index) for page,item in enumerate(
        document["stateLayout"]["current"])
        for index,identity in enumerate(item["blocks"]) if identity==OWNER]
    if len(owners)!=1:
        raise AssertionError("Original U8L10 editor identity changed")
    page,index=owners[0]
    editor=document["pages"][page]["blocks"][index]
    if editor["type"]!="cpp_compiler" or editor["initialCode"]!=original:
        raise AssertionError("U8L10 public source differs from original")
    public_cases=editor["outputCheck"]["cases"]
    if len(public_cases)!=5:
        raise AssertionError("U8L10 original public oracle count changed")
    for number,case in enumerate(public_cases,1):
        if (not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or not case["outputs"] or
            not all(isinstance(answer,str) for answer in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise AssertionError(f"Invalid U8L10 public case {number}")
    solutions={
        "correct_direct":(SIMPLE,IDENTIFIED,DUPLICATE,True),
        "correct_reserved":(ALT_SIMPLE,ALT_IDENTIFIED,ALT_DUPLICATE,True),
        "correct_with_inert_notes":(INERT_SIMPLE,IDENTIFIED,DUPLICATE,True),
        "unrelated_namespaced_base":(SIMPLE,IDENTIFIED,DUPLICATE,True),
        **{name:(*bodies,False) for name,bodies in BAD.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l10-cloning-") as d:
        folder=Path(d)
        suffix=".exe" if args.windows else ""
        generator=folder/("generator"+suffix)
        compile_cpp(args.compiler,GENERATOR,generator,args.windows,True)
        published=folder/"published.cpp"
        published.write_bytes(original.encode("utf-8"))
        # The first case is mandatory for the learner, and later public
        # cases remain optional. All run here as disposable regression QA.
        # Known-types-only and swallowed allocation failures are deceptive:
        # both can match every published stdout without meeting ownership.
        public_variants={
            "correct_direct","correct_reserved","correct_with_inert_notes",
            "unrelated_namespaced_base","duplicate_known_types_only",
            "duplicate_swallows_bad_alloc"
        }
        public_runs=0
        for name,record in solutions.items():
            *bodies,expected=record
            source=folder/(name+".cpp")
            source_for_case=published
            code=compose(original,tuple(bodies))
            if name=="unrelated_namespaced_base":
                note="namespace LgaedOther { class Muestra {}; }\n"
                source_for_case=folder/"namespaced-original.cpp"
                source_for_case.write_bytes((note+original).encode("utf-8"))
                code=note+code
            source.write_bytes(code.encode("utf-8"))
            private=folder/(name+".private.cpp")
            generated=run([str(generator),str(source_for_case),str(source),str(private)])
            if generated.returncode or not private.is_file():
                raise AssertionError((name,"Protected-body source gate failed",
                                      generated.stdout,generated.stderr))
            binary=folder/(name+suffix)
            compile_cpp(args.compiler,private,binary,args.windows)
            observed=run([str(binary)],timeout=15)
            passed=(observed.returncode==0 and observed.stdout=="" and
                    observed.stderr==MARKER)
            if passed!=expected:
                raise AssertionError((name,"Wrong polymorphic clone verdict",
                                      observed.returncode,observed.stdout,
                                      observed.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if passed else 'REJECT'}",flush=True)
            if name in public_variants:
                public_binary=folder/(name+"-public"+suffix)
                # Compile the ORIGINAL main, not the private instrumented one.
                compile_cpp(args.compiler,source,public_binary,args.windows)
                for number,case in enumerate(public_cases,1):
                    observed_public=subprocess.run(
                        [str(public_binary)],cwd=ROOT,
                        input=case["standardInput"],text=True,
                        encoding="utf-8",errors="replace",
                        capture_output=True,check=False,timeout=15)
                    if (observed_public.returncode!=case.get("exitCode",0) or
                        observed_public.stdout not in case["outputs"] or
                        observed_public.stderr):
                        raise AssertionError(
                            (name,number,"Authentic U8L10 output differs",
                             observed_public.returncode,
                             observed_public.stdout[-500:],
                             observed_public.stderr[-500:]))
                    public_runs+=1
                print(f"{args.compiler}: public {name}: "
                      f"{len(public_cases)} authored cases PASS; "
                      f"private clone semantics {'PASS' if passed else 'REJECT'}",
                      flush=True)
        if public_runs!=len(public_variants)*len(public_cases):
            raise AssertionError("U8L10 public QA omitted an original case")

        correct=compose(original,(SIMPLE,IDENTIFIED,DUPLICATE))
        attacks={
            "remove_virtual_destructor":correct.replace(
                "virtual ~Muestra() = default;","~Muestra() = default;",1),
            "remove_pure_clone":correct.replace(
                "virtual std::unique_ptr<Muestra> clonar() const = 0;",
                "virtual std::unique_ptr<Muestra> clonar() const;",1),
            "remove_copy_delete":correct.replace(
                "Muestra(const Muestra&) = delete;",
                "Muestra(const Muestra&) = default;",1),
            "change_getter":correct.replace(
                "int codigo() const noexcept { return codigo_; }",
                "int codigo() const noexcept { return 0; }",1),
            "change_inheritance":correct.replace(
                "class MuestraSimple : public Muestra {",
                "class MuestraSimple : private Muestra {",1),
            "change_factory":correct.replace(
                "return std::make_unique<MuestraIdentificada>(numero, codigo);",
                "return std::make_unique<MuestraSimple>(numero);",1),
            "change_alias":correct.replace(
                "using Muestras = std::vector<std::unique_ptr<Muestra>>;",
                "using Muestras = std::vector<Muestra*>;",1),
            "change_main":correct.replace(
                "int main() {","int main(int argc) {",1),
            "append_rogue_source":correct+"\nint tres() { return 3; }\n",
            "preprocessor_inside_body":correct.replace(
                "    return std::make_unique<MuestraSimple>(numero());",
                "    #define HIDDEN_OVERRIDE 1\n"
                "    return std::make_unique<MuestraSimple>(numero());",1),
            "escape_body":correct.replace(
                "    return std::make_unique<MuestraSimple>(numero());",
                "}\nvoid injected() {}\nvoid another() {\n"
                "    return std::make_unique<MuestraSimple>(numero());",1),
            "forged_completion_on_stderr":correct.replace(
                "    return std::make_unique<MuestraSimple>(numero());",
                "    std::fputs(\"LGAED_POLYMORPHIC_CLONE_COMPLETE_V1\", stderr);\n"
                "    return std::make_unique<MuestraSimple>(numero());",1),
            "private_probe_type":correct.replace(
                "    return std::make_unique<MuestraSimple>(numero());",
                "    (void)sizeof(LgaedNewKind);\n"
                "    return std::make_unique<MuestraSimple>(numero());",1),
            "private_probe_counter":correct.replace(
                "    return std::make_unique<MuestraSimple>(numero());",
                "    (void)sizeof(LgaedCloneCounts);\n"
                "    return std::make_unique<MuestraSimple>(numero());",1),
            "nonlocal_escape":correct.replace(
                "    return std::make_unique<MuestraSimple>(numero());",
                "    std::longjmp(0, 1);\n"
                "    return std::make_unique<MuestraSimple>(numero());",1),
            "direct_console_output":correct.replace(
                "    Muestras copias;",
                "    std::cout << \"Clones preparados\";\n"
                "    Muestras copias;",1),
        }
        for name,source in attacks.items():
            candidate=folder/(name+".cpp")
            candidate.write_text(source,encoding="utf-8")
            private=folder/(name+".private.cpp")
            result=run([str(generator),str(published),str(candidate),str(private)])
            if not result.returncode:
                raise AssertionError((name,"Protected declarations were editable"))
            print(f"{args.compiler}: {name}: REJECT",flush=True)
    print(f"PASS U8L10 deep polymorphic cloning: 3 accepted, "
          f"{len(BAD)} semantic negatives, {len(attacks)} source attacks; "
          f"all five authentic stdout cases checked for the three valid "
          f"solutions and two deceptive ownership implementations. "
          f"Only the independently compiled private witness grants semantics.")


if __name__=="__main__":
    try: main()
    except Exception as error:
        print(f"FAIL U8L10 C++ independent clone QA: {error}",file=sys.stderr)
        sys.exit(1)

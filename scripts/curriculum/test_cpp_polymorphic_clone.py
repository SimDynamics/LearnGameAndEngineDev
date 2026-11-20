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

BAD={
    "simple_null":("    return {};",IDENTIFIED,DUPLICATE),
    "simple_zero":("    return std::make_unique<MuestraSimple>(0);",IDENTIFIED,DUPLICATE),
    "simple_wrong_concrete":(
        "    return std::make_unique<MuestraIdentificada>(numero(),0);",
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
    next_pos=0
    for _ in range(2):
        opening=source.index(method,next_pos)
        begin=opening+len(method)
        closing=source.index("\n    }\nprivate:",begin)+5
        assert source[closing]=="}"
        positions.append((begin,closing))
        next_pos=closing+1
    duplicate="Muestras duplicar(const Muestras& origen) {"
    begin=source.index(duplicate)+len(duplicate)
    end=source.index("\n}\nvoid informar",begin)+1
    assert source[end]=="}"
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
    solutions={
        "correct_direct":(SIMPLE,IDENTIFIED,DUPLICATE,True),
        "correct_reserved":(ALT_SIMPLE,ALT_IDENTIFIED,ALT_DUPLICATE,True),
        **{name:(*bodies,False) for name,bodies in BAD.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l10-cloning-") as d:
        folder=Path(d)
        suffix=".exe" if args.windows else ""
        generator=folder/("generator"+suffix)
        compile_cpp(args.compiler,GENERATOR,generator,args.windows,True)
        published=folder/"published.cpp"
        published.write_text(original,encoding="utf-8")
        for name,record in solutions.items():
            *bodies,expected=record
            source=folder/(name+".cpp")
            source.write_text(compose(original,tuple(bodies)),encoding="utf-8")
            private=folder/(name+".private.cpp")
            generated=run([str(generator),str(published),str(source),str(private)])
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
        }
        for name,source in attacks.items():
            candidate=folder/(name+".cpp")
            candidate.write_text(source,encoding="utf-8")
            private=folder/(name+".private.cpp")
            result=run([str(generator),str(published),str(candidate),str(private)])
            if not result.returncode:
                raise AssertionError((name,"Protected declarations were editable"))
            print(f"{args.compiler}: {name}: REJECT",flush=True)
    print(f"PASS U8L10 deep polymorphic cloning: 2 accepted, "
          f"{len(BAD)} semantic negatives, {len(attacks)} source attacks")


if __name__=="__main__":
    try: main()
    except Exception as error:
        print(f"FAIL U8L10 C++ independent clone QA: {error}",file=sys.stderr)
        sys.exit(1)

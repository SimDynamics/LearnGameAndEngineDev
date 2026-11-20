"""Programación 3 U8L8: compile actual abstract interfaces and polymorphic owners.

All fixtures use the exact published C++ program. Only the two concrete
escribir bodies and the free functions crear and presentar may change.
The original JSON and saved student state are never modified.
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
             "lesson_definir_una_interfaz_abstracta.json")
OWNER="block_0017_0002"
MARKER="LGAED_ABSTRACT_INTERFACE_COMPLETE_V1"

SIMPLE_WRITE = """
    salida << "Numero:" << numero();
"""
IDENTIFIED_WRITE = """
    salida << "Numero:" << numero() << ";Codigo:" << codigo();
"""
CREATE = """
    if (tipo == 0) return std::make_unique<MuestraSimple>(numero);
    return std::make_unique<MuestraIdentificada>(numero,codigo);
"""
PRESENT = """
    salida << "Numero:" << dato.numero() << '\\n' << "Detalle:";
    dato.escribir(salida);
    salida << '\\n';
"""
ALT_SIMPLE = """
    salida << "Numero:" << numero_;
"""
ALT_IDENTIFIED = """
    Muestra::numero;
    salida << "Numero:" << numero_ << ";Codigo:" << codigo_;
""".replace("    Muestra::numero;\n", "")
ALT_CREATE = """
    std::unique_ptr<Muestra> created;
    if (tipo == 0) created=std::make_unique<MuestraSimple>(numero);
    else created=std::make_unique<MuestraIdentificada>(numero,codigo);
    return created;
"""
ALT_PRESENT = """
    salida << "Numero:";
    salida << dato.numero();
    salida << "\\nDetalle:";
    dato.escribir(salida);
    salida.put('\\n');
""".replace('    salida << "\\\\nDetalle:";', '    salida << "\\nDetalle:";')

BAD = {
    "simple_constant_value":(
        '    salida << "Numero:0";', IDENTIFIED_WRITE,CREATE,PRESENT),
    "simple_hardcoded_first_case":(
        '    salida << "Numero:-4";',IDENTIFIED_WRITE,CREATE,PRESENT),
    "identified_omits_code":(
        SIMPLE_WRITE,'    salida << "Numero:" << numero();',CREATE,PRESENT),
    "identified_ignores_zero":(
        SIMPLE_WRITE,'    salida << "Numero:" << numero();\n'
        '    if (codigo()!=0) salida << ";Codigo:" << codigo();',CREATE,PRESENT),
    "identified_uses_number_as_code":(
        SIMPLE_WRITE,'    salida << "Numero:" << numero() << ";Codigo:" << numero();',CREATE,PRESENT),
    "factory_always_simple":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,
        '    (void)tipo; (void)codigo;\n    return std::make_unique<MuestraSimple>(numero);',PRESENT),
    "factory_always_identified":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,
        '    (void)tipo;\n    return std::make_unique<MuestraIdentificada>(numero,codigo);',PRESENT),
    "factory_forgets_code":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,
        '    (void)codigo;\n    if(tipo==0)return std::make_unique<MuestraSimple>(numero);\n'
        '    return std::make_unique<MuestraIdentificada>(numero,0);',PRESENT),
    "factory_returns_null":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,
        '    (void)tipo; (void)numero; (void)codigo;\n    return {};',PRESENT),
    "presenter_fixed_number":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,CREATE,
        '    salida << "Numero:0\\nDetalle:";\n    dato.escribir(salida);\n    salida << \'\\n\';',),
    "presenter_omits_detail":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,CREATE,
        '    salida << "Numero:" << dato.numero() << "\\n";',),
    "presenter_omits_terminating_line":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,CREATE,
        '    salida << "Numero:" << dato.numero() << "\\nDetalle:";\n'
        '    dato.escribir(salida);',),
    "presenter_writes_to_stdout":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,CREATE,
        '    (void)salida;\n    std::cout << "Numero:" << dato.numero() << "\\nDetalle:";\n'
        '    dato.escribir(std::cout);\n    std::cout << \'\\n\';',),
    "presenter_known_types_only":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,CREATE,
        '    salida << "Numero:" << dato.numero() << "\\nDetalle:";\n'
        '    if(auto simple=dynamic_cast<const MuestraSimple*>(&dato))simple->escribir(salida);\n'
        '    else if(auto identified=dynamic_cast<const MuestraIdentificada*>(&dato))identified->escribir(salida);\n'
        '    else salida << "Tipo desconocido";\n'
        '    salida << \'\\n\';',),
    "presenter_copies_fixed_code":(
        SIMPLE_WRITE,IDENTIFIED_WRITE,CREATE,
        '    salida << "Numero:" << dato.numero() << "\\nDetalle:Numero:"\n'
        '           << dato.numero() << ";Codigo:0\\n";',),
}

PRELUDE=r"""#include <array>
#include <climits>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#define main lgaed_published_original_main
"""
DRIVER=r"""
#undef main
class MuestraAdicional final : public Muestra {
public:
    MuestraAdicional(int n,int marker) : n_{n},marker_{marker} {}
    int numero() const noexcept override {
        ++reads;
        return n_;
    }
    void escribir(std::ostream& stream) const override {
        ++writes;
        stream << "Adicional:" << marker_;
    }
    mutable int reads=0,writes=0;
private:
    int n_,marker_;
};
int main() {
    const auto check=[](const Muestra& object,std::string expected,
                        std::string detail)->bool {
        std::ostringstream report;
        object.escribir(report);
        if(report.str()!=detail)return false;
        std::ostringstream complete;
        presentar(object,complete);
        return complete.str()==expected;
    };
    {
        constexpr std::array<int,5> numbers{0,-9,5,INT_MIN,INT_MAX};
        for(int n:numbers)for(int extra:{0,-3,17}) {
            auto item=crear(0,n,extra);
            if(!item||!dynamic_cast<MuestraSimple*>(item.get())||
               dynamic_cast<MuestraIdentificada*>(item.get())||
               item->numero()!=n)return 1;
            const auto detail="Numero:"+std::to_string(n);
            const auto expected="Numero:"+std::to_string(n)+
                "\nDetalle:"+detail+"\n";
            if(!check(*item,expected,detail))return 2;
            const Muestra* identity=item.get();
            std::unique_ptr<Muestra> transferred=std::move(item);
            if(item||transferred.get()!=identity)return 3;
            transferred.reset();
            if(transferred)return 4;
        }
    }
    {
        constexpr std::array<int,5> numbers{0,-4,12,INT_MIN,INT_MAX};
        constexpr std::array<int,5> codes{0,-3,41,INT_MAX,INT_MIN};
        for(size_t i=0;i<numbers.size();++i) {
            const int n=numbers[i],code=codes[i];
            auto item=crear(1,n,code);
            auto* actual=dynamic_cast<MuestraIdentificada*>(item.get());
            if(!item||!actual||dynamic_cast<MuestraSimple*>(item.get())||
               item->numero()!=n||actual->codigo()!=code)return 5;
            const auto detail="Numero:"+std::to_string(n)+
                ";Codigo:"+std::to_string(code);
            const auto expected="Numero:"+std::to_string(n)+
                "\nDetalle:"+detail+"\n";
            if(!check(*item,expected,detail))return 6;
            std::ostringstream second;
            presentar(*item,second);
            if(second.str()!=expected)return 7;
        }
    }
    {
        // Neither labels nor known subclasses identify this valid third
        // implementation. Correct code must consume both virtual operations.
        MuestraAdicional custom{-7,93};
        std::ostringstream report;
        presentar(custom,report);
        if(report.str()!="Numero:-7\nDetalle:Adicional:93\n"||
           custom.reads==0||custom.writes==0)return 8;
        std::ostringstream second;
        presentar(custom,second);
        if(second.str()!=report.str()||custom.reads<2||
           custom.writes<2)return 9;
    }
    {
        MuestraAdicional extreme{INT_MAX,0};
        std::ostringstream report;
        presentar(extreme,report);
        if(report.str()!="Numero:"+std::to_string(INT_MAX)+
           "\nDetalle:Adicional:0\n"||
           extreme.reads==0||extreme.writes==0)return 10;
    }
    std::fputs("LGAED_ABSTRACT_INTERFACE_COMPLETE_V1",stderr);
    return 0;
}
"""


def original_editor()->str:
    document=json.loads(LESSON.read_text(encoding="utf-8"))
    positions=[(i,j) for i,page in enumerate(document["stateLayout"]["current"])
               for j,identity in enumerate(page["blocks"]) if identity==OWNER]
    assert len(positions)==1,"U8L8 original owner missing or repeated"
    p,b=positions[0]
    editor=document["pages"][p]["blocks"][b]
    assert editor["type"]=="cpp_compiler"
    assert len(editor["outputCheck"]["cases"])==5
    code=editor["initialCode"]
    assert code.count("void escribir(std::ostream& salida) const override {")==2
    for fragment in ("virtual ~Muestra() = default;",
                     "virtual int numero() const noexcept = 0;",
                     "virtual void escribir(std::ostream& salida) const = 0;"):
        assert fragment in code
    return code


def editable_spans(source: str)->list[tuple[int,int]]:
    signature="void escribir(std::ostream& salida) const override {"
    positions=[]
    for i in range(2):
        start=source.index(signature,positions[-1][1] if positions else 0)+len(signature)
        close=source.index("\n    }\nprivate:",start)+5
        assert source[close]=="}"
        positions.append((start,close))
    factory="std::unique_ptr<Muestra> crear(int tipo, int numero, int codigo) {"
    begin=source.index(factory)+len(factory)
    end=source.index("\n}\nvoid presentar",begin)+1
    assert source[end]=="}"
    positions.append((begin,end))
    presentation="void presentar(const Muestra& dato, std::ostream& salida) {"
    begin=source.index(presentation)+len(presentation)
    end=source.index("\n}\nint main()",begin)+1
    assert source[end]=="}"
    positions.append((begin,end))
    assert all(positions[i][1]<positions[i+1][0] for i in range(3))
    return positions


def compose(original: str, bodies: tuple[str,str,str,str])->str:
    spans=editable_spans(original)
    result=original
    for i in reversed(range(4)):
        start,end=spans[i]
        result=result[:start]+bodies[i]+result[end:]
    return result


def run(args:list[str],timeout=80)->subprocess.CompletedProcess[str]:
    return subprocess.run(args,cwd=ROOT,check=False,text=True,
                          encoding="utf-8",errors="replace",
                          capture_output=True,timeout=timeout)


def compile_cpp(compiler:str,source:Path,binary:Path,windows:bool):
    args=([compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
           "/permissive-","/Zc:__cplusplus","/utf-8",
           str(source),"/Fe:"+str(binary)]
          if windows else
          [compiler,"-std=c++23","-Wall","-Wextra","-Werror",
           "-pedantic",str(source),"-o",str(binary)])
    return run(args)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=original_editor()
    tests={
        "correct_direct":((SIMPLE_WRITE,IDENTIFIED_WRITE,CREATE,PRESENT),True),
        "correct_local_owner":((ALT_SIMPLE,ALT_IDENTIFIED,ALT_CREATE,ALT_PRESENT),True),
        **{name:(bodies,False) for name,bodies in BAD.items()}
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l8-") as directory:
        folder=Path(directory)
        suffix=".exe" if args.windows else ""
        for name,(bodies,expected) in tests.items():
            source=folder/(name+".cpp")
            source.write_text(PRELUDE+compose(original,bodies)+DRIVER,
                              encoding="utf-8")
            binary=folder/(name+suffix)
            built=compile_cpp(args.compiler,source,binary,args.windows)
            if built.returncode:
                raise AssertionError((name,"C++ compilation failed",
                                      built.stdout,built.stderr))
            observed=run([str(binary)],timeout=15)
            passed=(observed.returncode==0 and observed.stdout=="" and
                    observed.stderr==MARKER)
            if passed!=expected:
                raise AssertionError((name,"Unexpected interface semantics",
                                      observed.returncode,
                                      observed.stdout,observed.stderr))
            print(f"{args.compiler}: {name}: {'PASS' if passed else 'REJECT'}",
                  flush=True)
    print("PASS U8L8 genuine abstract polymorphism: two correct variants "
          "and fifteen defective variants; dynamic type, base owner, "
          "negative/zero/extreme values, independent streams and third type.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L8 abstract interface: {error}",file=sys.stderr)
        sys.exit(1)

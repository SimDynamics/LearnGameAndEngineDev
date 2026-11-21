"""Real C++ ownership/exception-safety regression for Programación 3 U8L7.

Only the body of the original function sustituir is changed in temporary
programs. The lesson JSON and every published output case stay intact.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/(
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "composicion_y_herencia_publica/"
    "lesson_conservar_recursos_si_falla_la_construccion_derivada.json"
)
OWNER="block_0016_0002"
MARKER="LGAED_DERIVED_PREPARATION_COMPLETE_V1"
SIGNATURE=("void sustituir(std::unique_ptr<Medida>& destino, int numero, "
           "int codigo, int modo, Balance& balance) {")
FOLLOWING="\n}\nint main()"

GOOD={
    "prepare_then_move":r"""
    auto candidate=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
    destino=std::move(candidate);
""",
    "prepare_then_swap":r"""
    std::unique_ptr<Medida> candidate=
        std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
    destino.swap(candidate);
""",
    "direct_rhs_completion":r"""
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
""",
}
BAD={
    "clear_before_preparing":r"""
    destino.reset();
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
""",
    "move_old_owner_out_before_preparing":r"""
    std::unique_ptr<Medida> retired=std::move(destino);
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
""",
    "construct_base_instead_of_derived":r"""
    (void)codigo;
    destino=std::make_unique<Medida>(numero,modo,balance);
""",
    "ignore_requested_failure":r"""
    (void)modo;
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,0,balance);
""",
    "swallow_construction_failure":r"""
    try {
        destino=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
    }catch(const Interrupcion&) {
    }
""",
    "silently_skip_failure_one":r"""
    if(modo==1)return;
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
""",
    "reuse_same_values_without_creating_new_owner":r"""
    if(destino&&destino->numero()==numero) {
        auto identified=dynamic_cast<MedidaIdentificada*>(destino.get());
        if(identified&&identified->codigo()==codigo&&modo==0)return;
    }
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
""",
    "allocate_for_an_unrelated_balance":r"""
    (void)balance;
    static Balance other;
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,modo,other);
""",
    "build_unused_temporary_first":r"""
    auto discarded=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
    (void)discarded;
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
""",
    "discards_completed_candidate_for_empty_destination":r"""
    if (!destino) {
        auto candidate=
            std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
        (void)candidate;
        return;
    }
    auto candidate=
        std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
    destino.swap(candidate);
""",
    "report_success_by_leaving_previous_owner":r"""
    (void)destino;
    auto candidate=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
    (void)candidate;
""",
    "zero_out_accounting_instead_of_relying_on_raii":r"""
    auto candidate=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
    destino=std::move(candidate);
    balance.bases_destruidas=0;
    balance.recursos_destruidos=0;
    balance.derivadas_destruidas=0;
""",
    "delete_old_then_fake_a_failure":r"""
    if(modo!=0) {
        destino.reset();
        throw Interrupcion{modo};
    }
    destino=std::make_unique<MedidaIdentificada>(numero,codigo,0,balance);
""",
}

PRELUDE=r"""#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <utility>
#define main lgaed_published_original_main
"""
DRIVER=r"""
#undef main
int main() {
    const auto state=[](const Balance& b,int live,int base_complete,
            int derived_complete,int resource_complete,int base_destroyed,
            int derived_destroyed,int resource_destroyed)->bool {
        return b.recursos_vivos==live&&
            b.bases_completadas==base_complete&&
            b.derivadas_completadas==derived_complete&&
            b.recursos_completados==resource_complete&&
            b.bases_destruidas==base_destroyed&&
            b.derivadas_destruidas==derived_destroyed&&
            b.recursos_destruidos==resource_destroyed;
    };
    const auto report=[](const std::unique_ptr<Medida>& value,
                         int number,int code)->bool {
        if(!value||!dynamic_cast<MedidaIdentificada*>(value.get()))
            return false;
        std::ostringstream output;
        value->escribir(output);
        return output.str()=="Numero:"+std::to_string(number)+
            ";Codigo:"+std::to_string(code);
    };

    // Each failing constructor stage must leave the same previously owned
    // object alive and let the original Interrupcion propagate unchanged.
    for(bool occupied:{false,true})for(int failure:{1,2,3}) {
        Balance balance;
        std::unique_ptr<Medida> destination;
        if(occupied)
            destination=std::make_unique<MedidaIdentificada>(5,-3,0,balance);
        const auto* identity=destination.get();
        int stage=0;
        try {
            sustituir(destination,-7,0,failure,balance);
        }catch(const Interrupcion& thrown) {stage=thrown.etapa;}
        catch(...) {return 1;}
        if(stage!=failure||destination.get()!=identity||
           (occupied&&!report(destination,5,-3))||
           (!occupied&&destination))return 2;
        const int old=occupied?1:0;
        const int new_base=failure>=2?1:0;
        const int new_resource=failure==3?2:1;
        if(!state(balance,occupied?2:0,old+new_base,old,
                  2*old+new_resource,new_base,0,new_resource))
            return 3;
        destination.reset();
        if(!state(balance,0,old+new_base,old,
                  2*old+new_resource,new_base+old,old,
                  new_resource+2*old))return 4;
    }

    // A successful replacement must install a genuinely different derived
    // object. The prior destructor chain completes exactly once.
    for(bool occupied:{false,true}) {
        Balance balance;
        std::unique_ptr<Medida> destination;
        if(occupied)
            destination=std::make_unique<MedidaIdentificada>(5,-3,0,balance);
        const auto* former=destination.get();
        sustituir(destination,0,-2,0,balance);
        if(!report(destination,0,-2)||destination.get()==former||
           !state(balance,2,(occupied?2:1),(occupied?2:1),
                  (occupied?4:2),(occupied?1:0),
                  (occupied?1:0),(occupied?2:0)))return 5;
        destination.reset();
        const int completed=occupied?2:1;
        if(!state(balance,0,completed,completed,completed*2,
                  completed,completed,completed*2))return 6;
    }

    // Matching values do not authorize reusing the previous object.
    {
        Balance balance;
        std::unique_ptr<Medida> destination=
            std::make_unique<MedidaIdentificada>(-11,23,0,balance);
        const auto* former=destination.get();
        sustituir(destination,-11,23,0,balance);
        if(destination.get()==former||!report(destination,-11,23)||
           !state(balance,2,2,2,4,1,1,2))return 7;
        destination.reset();
        if(!state(balance,0,2,2,4,2,2,4))return 8;
    }

    // The previous and replacement objects may belong to different
    // Balance records. A candidate must use only the supplied one.
    {
        Balance old_balance,new_balance;
        std::unique_ptr<Medida> destination=
            std::make_unique<MedidaIdentificada>(1,2,0,old_balance);
        const auto* former=destination.get();
        sustituir(destination,-8,9,0,new_balance);
        if(!report(destination,-8,9)||destination.get()==former||
           !state(old_balance,0,1,1,2,1,1,2)||
           !state(new_balance,2,1,1,2,0,0,0))return 9;
        destination.reset();
        if(!state(new_balance,0,1,1,2,1,1,2))return 10;
    }

    // An unrelated caller exception must also clean the accepted owner by
    // normal RAII, without user code explicitly calling the destructor.
    {
        Balance balance;
        bool caught=false;
        try {
            std::unique_ptr<Medida> destination;
            sustituir(destination,4,0,0,balance);
            if(!report(destination,4,0))return 11;
            throw 31;
        }catch(int value) {caught=value==31;}
        if(!caught||!state(balance,0,1,1,2,1,1,2))return 12;
    }
    std::fputs("LGAED_DERIVED_PREPARATION_COMPLETE_V1",stderr);
    return 0;
}
"""


def original_editor() -> str:
    lesson=json.loads(LESSON.read_text(encoding="utf-8"))
    positions=[
        (p,b) for p,page in enumerate(lesson["stateLayout"]["current"])
        for b,identity in enumerate(page["blocks"]) if identity==OWNER
    ]
    assert len(positions)==1,"Original U8L7 compiler owner absent"
    p,b=positions[0]
    block=lesson["pages"][p]["blocks"][b]
    assert block["type"]=="cpp_compiler"
    assert len(block["outputCheck"]["cases"])==5
    assert block["initialCode"].count(SIGNATURE)==1
    return block["initialCode"]


def compose(original: str, body: str) -> str:
    begin=original.index(SIGNATURE)+len(SIGNATURE)
    end=original.index(FOLLOWING,begin)+1
    assert original[end]=="}"
    return original[:begin]+body+original[end:]


def run(command: list[str],timeout: int=85) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command,cwd=ROOT,check=False,text=True,
                          encoding="utf-8",errors="replace",
                          capture_output=True,timeout=timeout)


def compile_cpp(compiler: str,source: Path,binary: Path,windows: bool):
    if windows:
        args=[compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
              "/permissive-","/Zc:__cplusplus","/utf-8",
              str(source),"/Fe:"+str(binary)]
    else:
        args=[compiler,"-std=c++23","-Wall","-Wextra","-Werror",
              "-pedantic",str(source),"-o",str(binary)]
    return run(args)


def main() -> None:
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=original_editor()
    candidates={
        **{name:(code,True) for name,code in GOOD.items()},
        **{name:(code,False) for name,code in BAD.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l7-") as folder:
        directory=Path(folder)
        suffix=".exe" if args.windows else ""
        for name,(body,expected) in candidates.items():
            cpp=directory/(name+".cpp")
            cpp.write_text(PRELUDE+compose(original,body)+DRIVER,
                           encoding="utf-8")
            binary=directory/(name+suffix)
            built=compile_cpp(args.compiler,cpp,binary,args.windows)
            if built.returncode:
                raise AssertionError((name,"C++ compilation failed",
                                      built.stdout,built.stderr))
            observed=run([str(binary)],timeout=15)
            accepted=(observed.returncode==0 and observed.stdout=="" and
                      observed.stderr==MARKER)
            if accepted!=expected:
                raise AssertionError((name,"incorrect ownership/exception verdict",
                                      observed.returncode,
                                      observed.stdout,observed.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if accepted else 'REJECT'}",flush=True)
    print("PASS U8L7 real derived construction: base/member/derived failure "
          "cleanup, unchanged destination owner and data, empty destination, "
          "equal-value new owner, distinct balances, destruction accounting "
          "and caller exception. Original source and cases untouched.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L7 derived construction semantics: {error}",file=sys.stderr)
        sys.exit(1)

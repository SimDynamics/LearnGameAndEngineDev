"""Compile the published U6L12 Tramo and independently verify its two functions.

Only the bodies of copia_modificada and tomar_si_cabe change. Source class,
constructors, assignment, helpers, main, and all original outputs are intact.
The private driver measures live array ownership, values, allocation failures,
strong state preservation and safe cleanup. This is not in-app accreditation.
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
             "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
             "lesson_recibir_y_devolver_objetos_propietarios.json")
OWNER="block_0021_0002"
MARKER="LGAED_ARRAY_RETURN_TAKE_COMPLETE_V1"

COPY={
    "copy_then_edit":r"""
    Tramo copia(origen);
    copia.escribir(indice, valor);
    return copia;
""",
    "prepare_using_public_api":r"""
    Tramo resultado;
    if (!resultado.configurar(origen.cantidad())) throw std::bad_alloc{};
    for (int posicion = 0; posicion < origen.cantidad(); ++posicion) {
        int actual = 0;
        origen.leer(posicion, actual);
        resultado.escribir(posicion, posicion == indice ? valor : actual);
    }
    return resultado;
""",
}
TAKE={
    "check_then_assign":r"""
    if (limite < 0 || limite > 4 || origen.cantidad() > limite) return false;
    destino = std::move(origen);
    return true;
""",
    "check_then_swap_with_retired":r"""
    if (limite < 0 || limite > 4 || origen.cantidad() > limite) return false;
    if (&destino != &origen) {
        Tramo retirado;
        retirado.intercambiar(destino);
        destino.intercambiar(origen);
    }
    return true;
""",
}
BAD_COPY={
    "does_not_modify_requested_index":r"""
    (void)indice; (void)valor;
    Tramo resultado(origen);
    return resultado;
""",
    "always_modifies_first_index":r"""
    (void)indice;
    Tramo resultado(origen);
    resultado.escribir(0, valor);
    return resultado;
""",
    "returns_empty_copy":r"""
    (void)origen; (void)indice; (void)valor;
    return Tramo{};
""",
    "changes_the_original_source":r"""
    const_cast<Tramo&>(origen).escribir(indice, valor);
    return Tramo{origen};
""",
    "swallows_allocation_failure":r"""
    try {
        Tramo copia(origen);
        copia.escribir(indice, valor);
        return copia;
    } catch (const std::bad_alloc&) {
        return Tramo{};
    }
""",
    "returns_less_data":r"""
    Tramo resultado;
    resultado.configurar(origen.cantidad()-1);
    for (int i=0; i<resultado.cantidad(); ++i) {
        int n=0;
        origen.leer(i,n);
        resultado.escribir(i,i==indice?valor:n);
    }
    return resultado;
""",
    "copies_by_consuming_source":r"""
    Tramo resultado(std::move(const_cast<Tramo&>(origen)));
    resultado.escribir(indice,valor);
    return resultado;
""",
    "returns_unmodified_for_equal_value":r"""
    int old=0;
    origen.leer(indice,old);
    if (old==valor) return Tramo{};
    Tramo copia(origen);
    copia.escribir(indice,valor);
    return copia;
""",
}
BAD_TAKE={
    "moves_before_checking_limit":r"""
    destino = std::move(origen);
    return limite >= 0 && limite <= 4 && destino.cantidad() <= limite;
""",
    "clones_instead_of_transferring":r"""
    if (limite<0 || limite>4 || origen.cantidad()>limite) return false;
    try {
        Tramo copia(origen);
        destino=std::move(copia);
        origen.vaciar();
        return true;
    } catch (const std::bad_alloc&) { return false; }
""",
    "leaves_old_destination_in_source":r"""
    if (limite<0 || limite>4 || origen.cantidad()>limite) return false;
    destino.intercambiar(origen);
    return true;
""",
    "rejects_empty_source":r"""
    if (limite<0 || limite>4 || origen.cantidad()>limite ||
        origen.cantidad()==0) return false;
    destino=std::move(origen);
    return true;
""",
    "always_accepts_outside_limit":r"""
    if (origen.cantidad()>limite) return false;
    destino=std::move(origen);
    return true;
""",
    "ignores_insufficient_limit":r"""
    if (limite<0 || limite>4) return false;
    destino=std::move(origen);
    return true;
""",
    "destroys_self_transfer":r"""
    if (limite<0 || limite>4 || origen.cantidad()>limite) return false;
    if (&destino==&origen) destino.vaciar();
    else destino=std::move(origen);
    return true;
""",
    "returns_false_after_transfer":r"""
    if (limite<0 || limite>4 || origen.cantidad()>limite) return false;
    destino=std::move(origen);
    return false;
""",
}
PRELUDE=r"""#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
namespace lgaed_return_take_qa {
struct Watch {
    inline static bool active=false,fail_next_array=false;
    inline static unsigned allocated=0,freed=0;
    inline static const void* last_array=nullptr;
    inline static const void* expected_removed=nullptr;
    inline static bool removed=false;
    static void expect_removal(const void* value) noexcept {
        expected_removed=value;removed=false;
    }
    static void on_delete(const void* value) noexcept {
        if(!active||!value)return;
        ++freed;
        if(value==expected_removed) {
            expected_removed=nullptr;removed=true;
        }
    }
};
}
void* operator new(std::size_t count) {
    if(void* ptr=std::malloc(count?count:1))return ptr;
    throw std::bad_alloc{};
}
void operator delete(void* ptr) noexcept {std::free(ptr);}
void operator delete(void* ptr,std::size_t) noexcept {std::free(ptr);}
void* operator new[](std::size_t count) {
    using lgaed_return_take_qa::Watch;
    if(Watch::fail_next_array) {
        Watch::fail_next_array=false;throw std::bad_alloc{};
    }
    if(void* ptr=std::malloc(count?count:1)) {
        if(Watch::active) {
            ++Watch::allocated;
            Watch::last_array=ptr;
        }
        return ptr;
    }
    throw std::bad_alloc{};
}
void operator delete[](void* ptr) noexcept {
    lgaed_return_take_qa::Watch::on_delete(ptr);
    std::free(ptr);
}
void operator delete[](void* ptr,std::size_t) noexcept {
    ::operator delete[](ptr);
}
#define main lgaed_published_original_main
"""
DRIVER=r"""
#undef main
int main() {
    using lgaed_return_take_qa::Watch;
    const auto equal=[](const Tramo& source,std::array<int,4> expected,
                        int count)->bool {
        if(source.cantidad()!=count ||
           (source.acceso()!=nullptr)!=(count!=0))return false;
        for(int i=0;i<count;++i) {
            int value=-100;
            if(!source.leer(i,value)||value!=expected[static_cast<size_t>(i)])
                return false;
        }
        return true;
    };
    Watch::active=true;
    {
        Tramo source,target;
        if(!source.configurar(3)||!target.configurar(1)||
           !source.escribir(0,1)||!source.escribir(1,0)||
           !source.escribir(2,4)||!target.escribir(0,8))
            return 1;
        const int* source_pointer=source.acceso();
        const auto initial_allocs=Watch::allocated;
        Tramo changed=copia_modificada(source,1,7);
        if(!equal(source,{1,0,4,0},3)||source.acceso()!=source_pointer||
           !equal(changed,{1,7,4,0},3)||
           changed.acceso()==source_pointer||
           Watch::allocated<=initial_allocs)return 2;

        // Equal old/new value still requires an independent copied array.
        const auto prior_equal=Watch::allocated;
        Tramo unchanged=copia_modificada(source,1,0);
        if(!equal(unchanged,{1,0,4,0},3)||
           unchanged.acceso()==source_pointer||
           Watch::allocated<=prior_equal||
           source.acceso()!=source_pointer)return 3;

        // Returning a new owner, then assigning it to its own source, must
        // work: the RHS finishes while the original source still exists.
        const int* same_before=source.acceso();
        Watch::expect_removal(same_before);
        const auto prior_self=Watch::allocated;
        source=copia_modificada(source,2,9);
        if(!equal(source,{1,0,9,0},3)||source.acceso()==same_before||
           Watch::allocated<=prior_self||!Watch::removed)
            return 4;

        // Source and destination survive a deliberately failed allocation.
        const int* before_failure_source=source.acceso();
        const int* before_failure_target=target.acceso();
        const auto allocated_before_failure=Watch::allocated;
        const auto freed_before_failure=Watch::freed;
        Watch::fail_next_array=true;
        bool bad_alloc_seen=false;
        try {target=copia_modificada(source,0,6);}
        catch(const std::bad_alloc&) {bad_alloc_seen=true;}
        Watch::fail_next_array=false;
        if(!bad_alloc_seen||source.acceso()!=before_failure_source||
           target.acceso()!=before_failure_target||
           !equal(source,{1,0,9,0},3)||!equal(target,{8,0,0,0},1)||
           Watch::allocated!=allocated_before_failure||
           Watch::freed!=freed_before_failure)return 5;

        // The failure guarantee applies when RHS and destination are aliases.
        const int* self_before_failure=source.acceso();
        const auto old_created=Watch::allocated,old_deleted=Watch::freed;
        Watch::fail_next_array=true;
        bool self_bad_alloc=false;
        try {source=copia_modificada(source,0,6);}
        catch(const std::bad_alloc&) {self_bad_alloc=true;}
        Watch::fail_next_array=false;
        if(!self_bad_alloc||source.acceso()!=self_before_failure||
           !equal(source,{1,0,9,0},3)||
           Watch::allocated!=old_created||Watch::freed!=old_deleted)
            return 6;

        // Insufficient and outside-domain limits never consume the offered
        // rvalue reference, and may not change the occupied target either.
        const int* before_reject_source=source.acceso();
        const int* before_reject_target=target.acceso();
        const auto rejected_allocs=Watch::allocated,rejected_frees=Watch::freed;
        for(int invalid:{-1,2,5}) {
            if(tomar_si_cabe(target,std::move(source),invalid)||
               source.acceso()!=before_reject_source||
               target.acceso()!=before_reject_target||
               !equal(source,{1,0,9,0},3)||!equal(target,{8,0,0,0},1)||
               Watch::allocated!=rejected_allocs||
               Watch::freed!=rejected_frees)return 7;
        }
        // The same index/count may also be rejected when source==destination.
        for(int invalid:{-1,2,5}) {
            if(tomar_si_cabe(source,std::move(source),invalid)||
               source.acceso()!=before_reject_source||
               !equal(source,{1,0,9,0},3)||
               Watch::allocated!=rejected_allocs||
               Watch::freed!=rejected_frees)return 8;
        }

        // The accepted operation transfers the same live array, releases
        // the displaced destination and requests no new allocation.
        Watch::expect_removal(target.acceso());
        const auto before_take_allocs=Watch::allocated,before_take_frees=Watch::freed;
        const int* transferred=source.acceso();
        if(!tomar_si_cabe(target,std::move(source),3)||
           target.acceso()!=transferred||
           !equal(target,{1,0,9,0},3)||!equal(source,{0,0,0,0},0)||
           Watch::allocated!=before_take_allocs||
           Watch::freed!=before_take_frees+1||!Watch::removed)
            return 9;

        // Taking an empty owner clears an occupied target; no allocation.
        Watch::expect_removal(target.acceso());
        const auto empty_allocs=Watch::allocated,empty_frees=Watch::freed;
        if(!tomar_si_cabe(target,std::move(source),0)||
           !equal(target,{0,0,0,0},0)||!equal(source,{0,0,0,0},0)||
           Watch::allocated!=empty_allocs||
           Watch::freed!=empty_frees+1||!Watch::removed)return 10;

        // Self transfer, occupied and empty, preserves pointer/count/values.
        if(!target.configurar(2)||!target.escribir(0,0)||
           !target.escribir(1,7))return 11;
        const int* owner=target.acceso();
        const auto self_allocs=Watch::allocated,self_frees=Watch::freed;
        if(!tomar_si_cabe(target,std::move(target),2)||
           target.acceso()!=owner||!equal(target,{0,7,0,0},2)||
           Watch::allocated!=self_allocs||Watch::freed!=self_frees)return 12;
        if(!target.vaciar())return 13;
        const auto empty_self_allocs=Watch::allocated;
        const auto empty_self_frees=Watch::freed;
        if(!tomar_si_cabe(target,std::move(target),0)||
           !equal(target,{0,0,0,0},0)||
           Watch::allocated!=empty_self_allocs||
           Watch::freed!=empty_self_frees)return 14;

        // Equal values under different owners must still transfer identity.
        Tramo equal_source,equal_target;
        if(!equal_source.configurar(2)||!equal_target.configurar(2)||
           !equal_source.escribir(0,0)||!equal_source.escribir(1,3)||
           !equal_target.escribir(0,0)||!equal_target.escribir(1,3))
            return 15;
        const int* equal_pointer=equal_source.acceso();
        Watch::expect_removal(equal_target.acceso());
        const auto equal_allocs=Watch::allocated,equal_frees=Watch::freed;
        if(!tomar_si_cabe(equal_target,std::move(equal_source),4)||
           equal_target.acceso()!=equal_pointer||
           !equal(equal_target,{0,3,0,0},2)||
           !equal(equal_source,{0,0,0,0},0)||
           Watch::allocated!=equal_allocs||
           Watch::freed!=equal_frees+1||!Watch::removed)return 16;

        // Reuse a previously moved-from object and retain normal cleanup.
        if(!equal_source.configurar(1)||
           !equal_source.escribir(0,6)||
           !equal(equal_source,{6,0,0,0},1)||
           equal_source.acceso()==equal_target.acceso())return 17;

        // Exiting after a caller exception cleans every live array only once.
        const auto prior_throw_new=Watch::allocated,prior_throw_del=Watch::freed;
        bool caught=false;
        try {
            Tramo a,b;
            if(!a.configurar(2)||!b.configurar(1))return 18;
            if(!tomar_si_cabe(b,std::move(a),2)||
               !equal(a,{0,0,0,0},0)||!equal(b,{0,0,0,0},2))
                return 19;
            throw 19;
        } catch(int value) {caught=value==19;}
        if(!caught||Watch::allocated!=prior_throw_new+2||
           Watch::freed!=prior_throw_del+2)return 20;
    }
    Watch::active=false;
    if(Watch::allocated!=Watch::freed||Watch::fail_next_array)return 21;
    std::fputs("LGAED_ARRAY_RETURN_TAKE_COMPLETE_V1",stderr);
    return 0;
}
"""

def run(args: list[str],timeout: int=90)->subprocess.CompletedProcess[str]:
    return subprocess.run(args,cwd=ROOT,check=False,text=True,
                          encoding="utf-8",errors="replace",
                          capture_output=True,timeout=timeout)

def compile_cpp(compiler:str,source:Path,binary:Path,windows:bool):
    if windows:
        args=[compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
              "/permissive-","/Zc:__cplusplus","/utf-8","/Inative",
              str(source),"/Fe:"+str(binary)]
    else:
        args=[compiler,"-std=c++23","-Wall","-Wextra","-Werror",
              "-pedantic","-Inative",str(source),"-o",str(binary)]
    return run(args)

def original_block():
    lesson=json.loads(LESSON.read_text(encoding="utf-8"))
    positions=[(p,b) for p,page in enumerate(lesson["stateLayout"]["current"])
               for b,identity in enumerate(page["blocks"]) if identity==OWNER]
    assert len(positions)==1,"U6L12 original v2 owner lost"
    p,b=positions[0]
    block=lesson["pages"][p]["blocks"][b]
    assert block["type"]=="cpp_compiler" and len(block["outputCheck"]["cases"])==3
    return block

def compose(original:str,copy:str,take:str)->str:
    copy_sig="Tramo copia_modificada(const Tramo& origen, int indice, int valor) {"
    take_sig="bool tomar_si_cabe(Tramo& destino, Tramo&& origen, int limite) noexcept {"
    assert original.count(copy_sig)==1 and original.count(take_sig)==1
    a=original.index(copy_sig)+len(copy_sig)
    b=original.index("\n}\nbool tomar_si_cabe",a)+1
    c=original.index(take_sig)+len(take_sig)
    d=original.index("\n}\nvoid mostrar",c)+1
    assert a<b<c<d
    return original[:a]+copy+original[b:c]+take+original[d:]

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--compiler",required=True)
    p.add_argument("--windows",action="store_true")
    o=p.parse_args()
    original=original_block()["initialCode"]
    cases={
        "copy_and_take":(COPY["copy_then_edit"],TAKE["check_then_assign"],True),
        "copy_public_api_and_swap":(
            COPY["prepare_using_public_api"],TAKE["check_then_swap_with_retired"],True),
        **{name:(body,TAKE["check_then_assign"],False) for name,body in BAD_COPY.items()},
        **{name:(COPY["copy_then_edit"],body,False) for name,body in BAD_TAKE.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l12-") as dirname:
        folder=Path(dirname)
        suffix=".exe" if o.windows else ""
        for name,(copy,take,expected) in cases.items():
            code=folder/(name+".cpp")
            code.write_bytes((PRELUDE+compose(original,copy,take)+DRIVER).encode("utf-8"))
            binary=folder/(name+suffix)
            built=compile_cpp(o.compiler,code,binary,o.windows)
            if built.returncode:
                raise AssertionError((name,"compiled program failed",
                                      built.stdout,built.stderr))
            result=run([str(binary)],timeout=16)
            accepted=result.returncode==0 and result.stdout=="" and result.stderr==MARKER
            if accepted!=expected:
                raise AssertionError((name,"unexpected copy/transfer semantics",
                                      result.returncode,result.stdout,result.stderr))
            print(f"{o.compiler}: {name}: {'PASS' if accepted else 'REJECT'}",flush=True)
    print("PASS U6L12 original Tramo: two valid solutions, sixteen defective "
          "semantic variants rejected; live array identity, bad_alloc, alias "
          "preservation, rejected &&, no allocation on move and RAII cleanup.")

if __name__=="__main__":
    try:main()
    except Exception as e:
        print(f"FAIL U6L12 return/take semantics: {e}",file=sys.stderr)
        sys.exit(1)

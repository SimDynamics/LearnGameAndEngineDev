"""Run real U6L11 copy and move assignment semantics on the published Tramo.

Only the two operator= bodies change. Class fields, constructors, helpers,
main, public answers, and lesson JSON remain untouched. The separate C++
program observes live unique_ptr<int[]> identities, values, allocation
counts, deterministic bad_alloc, and destruction without unsafe reads.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "estado_y_ciclo_de_vida_de_clases/"
    "lesson_copiar_y_mover_arrays_sin_desajustar_la_cantidad.json"
)
OWNER = "block_0020_0002"
MARKER = "LGAED_ARRAY_COPY_MOVE_SEMANTICS_V1"

COPY = {
    "candidate_swap": r"""
    if (this == &otra) return *this;
    std::unique_ptr<int[]> nueva;
    if (otra.cantidad_ != 0) {
        nueva = std::make_unique<int[]>(otra.cantidad_);
        for (int i = 0; i < otra.cantidad_; ++i) nueva[i] = otra.datos_[i];
    }
    datos_.swap(nueva);
    cantidad_ = otra.cantidad_;
    return *this;
""",
    "copy_constructor_exchange": r"""
    if (this == &otra) return *this;
    Tramo candidata(otra);
    intercambiar(candidata);
    return *this;
""",
}

MOVE = {
    "move_and_exchange": r"""
    if (this == &otra) return *this;
    datos_ = std::move(otra.datos_);
    cantidad_ = std::exchange(otra.cantidad_, 0);
    return *this;
""",
    "empty_swap_then_transfer": r"""
    if (this != &otra) {
        Tramo retirada;
        intercambiar(retirada);
        intercambiar(otra);
    }
    return *this;
""",
}

BAD_COPY = {
    "reuses_equal_size_array": r"""
    if (this == &otra) return *this;
    if (cantidad_ == otra.cantidad_ && datos_ && otra.datos_) {
        for (int i=0;i<cantidad_;++i) datos_[i] = otra.datos_[i];
        return *this;
    }
    Tramo candidata(otra);
    intercambiar(candidata);
    return *this;
""",
    "empties_destination_before_allocating": r"""
    if (this == &otra) return *this;
    datos_.reset();
    cantidad_ = 0;
    Tramo candidata(otra);
    intercambiar(candidata);
    return *this;
""",
    "rebuilds_self_copy": r"""
    Tramo candidata(otra);
    intercambiar(candidata);
    return *this;
""",
    "ignores_empty_copy": r"""
    if (this == &otra || otra.cantidad_ == 0) return *this;
    Tramo candidata(otra);
    intercambiar(candidata);
    return *this;
""",
    "copies_first_element_only": r"""
    if (this == &otra) return *this;
    std::unique_ptr<int[]> nueva;
    if (otra.cantidad_ != 0) {
        nueva = std::make_unique<int[]>(otra.cantidad_);
        nueva[0] = otra.datos_[0];
    }
    datos_.swap(nueva);
    cantidad_ = otra.cantidad_;
    return *this;
""",
    "swallows_bad_alloc": r"""
    if (this == &otra) return *this;
    try {
        Tramo candidata(otra);
        intercambiar(candidata);
    } catch (const std::bad_alloc&) {}
    return *this;
""",
    "returns_copy_source": r"""
    if (this == &otra) return *this;
    Tramo candidata(otra);
    intercambiar(candidata);
    return const_cast<Tramo&>(otra);
""",
}

BAD_MOVE = {
    "forgets_origin_count": r"""
    if (this == &otra) return *this;
    datos_ = std::move(otra.datos_);
    cantidad_ = otra.cantidad_;
    return *this;
""",
    "swap_leaves_old_destination_under_source": r"""
    if (this == &otra) return *this;
    intercambiar(otra);
    return *this;
""",
    "clones_array_instead_of_moving": r"""
    if (this == &otra) return *this;
    std::unique_ptr<int[]> nueva;
    if (otra.cantidad_ != 0) {
        nueva = std::make_unique<int[]>(otra.cantidad_);
        for (int i = 0; i < otra.cantidad_; ++i) nueva[i] = otra.datos_[i];
    }
    datos_ = std::move(nueva);
    cantidad_ = otra.cantidad_;
    otra.vaciar();
    return *this;
""",
    "empty_source_keeps_destination": r"""
    if (this != &otra && otra.cantidad_ != 0) {
        datos_ = std::move(otra.datos_);
        cantidad_ = std::exchange(otra.cantidad_, 0);
    }
    return *this;
""",
    "clears_self_move": r"""
    if (this == &otra) {
        vaciar();
        return *this;
    }
    datos_ = std::move(otra.datos_);
    cantidad_ = std::exchange(otra.cantidad_, 0);
    return *this;
""",
    "returns_move_source": r"""
    if (this != &otra) {
        datos_ = std::move(otra.datos_);
        cantidad_ = std::exchange(otra.cantidad_, 0);
    }
    return static_cast<Tramo&>(otra);
""",
}

PRELUDE = r"""#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace lgaed_array_copy_move_qa {
struct Watch {
    inline static bool active = false, fail_next_array = false;
    inline static unsigned array_allocs = 0, array_frees = 0;
    inline static unsigned single_allocs = 0, single_frees = 0;
    inline static const void* last_array = nullptr;
    inline static const void* to_release = nullptr;
    inline static bool released_expected = false;
    static void expect_release(const void* pointer) noexcept {
        to_release = pointer;
        released_expected = false;
    }
    static void on_delete(const void* pointer, bool array) noexcept {
        if (!active || !pointer) return;
        if (array) ++array_frees; else ++single_frees;
        if (array && pointer == to_release) {
            released_expected = true;
            to_release = nullptr;
        }
    }
};
}
void* operator new(std::size_t bytes) {
    if (void* pointer = std::malloc(bytes ? bytes : 1)) {
        if (lgaed_array_copy_move_qa::Watch::active)
            ++lgaed_array_copy_move_qa::Watch::single_allocs;
        return pointer;
    }
    throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept {
    lgaed_array_copy_move_qa::Watch::on_delete(pointer, false);
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}
void* operator new[](std::size_t bytes) {
    using lgaed_array_copy_move_qa::Watch;
    if (Watch::fail_next_array) {
        Watch::fail_next_array = false;
        throw std::bad_alloc{};
    }
    if (void* pointer = std::malloc(bytes ? bytes : 1)) {
        if (Watch::active) {
            ++Watch::array_allocs;
            Watch::last_array = pointer;
        }
        return pointer;
    }
    throw std::bad_alloc{};
}
void operator delete[](void* pointer) noexcept {
    lgaed_array_copy_move_qa::Watch::on_delete(pointer, true);
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    ::operator delete[](pointer);
}
#define main lgaed_original_tramo_main
"""

DRIVER = r"""
#undef main
int main() {
    using lgaed_array_copy_move_qa::Watch;
    const auto equal=[](const Tramo& value, std::array<int,4> expected,
                        int length) noexcept -> bool {
        if (value.cantidad()!=length ||
            (value.acceso()!=nullptr)!=(length!=0)) return false;
        for (int i=0;i<length;++i) {
            int observed=-10;
            if (!value.leer(i,observed) || observed!=expected[i])return false;
        }
        return true;
    };
    Watch::active=true;
    {
        Tramo source,destination;
        if (!source.configurar(3)||!destination.configurar(2)||
            !source.escribir(0,5)||!source.escribir(1,0)||
            !source.escribir(2,8)||!destination.escribir(0,9)||
            !destination.escribir(1,7)||Watch::array_allocs!=2)return 1;
        const int* source_identity=source.acceso();
        Watch::expect_release(destination.acceso());
        const auto made=Watch::array_allocs,freed=Watch::array_frees;
        Tramo& copied=(destination=source);
        if (&copied!=&destination||!equal(source,{5,0,8,0},3)||
            !equal(destination,{5,0,8,0},3)||
            destination.acceso()==source_identity||
            source.acceso()!=source_identity||
            destination.acceso()!=Watch::last_array||
            Watch::array_allocs!=made+1||Watch::array_frees!=freed+1||
            !Watch::released_expected)return 2;

        // Equal size and values still require a freshly allocated array.
        Watch::expect_release(destination.acceso());
        const auto made_again=Watch::array_allocs,freed_again=Watch::array_frees;
        destination=source;
        if (!equal(destination,{5,0,8,0},3)||
            destination.acceso()==source_identity||
            destination.acceso()!=Watch::last_array||
            source.acceso()!=source_identity||
            Watch::array_allocs!=made_again+1||
            Watch::array_frees!=freed_again+1||
            !Watch::released_expected)return 3;

        // A self-copy, including an occupied array containing zero, is inert.
        Tramo& copy_alias=source;
        const auto self_owner=source.acceso();
        const auto self_made=Watch::array_allocs,self_freed=Watch::array_frees;
        Tramo& self_return=(source=copy_alias);
        if (&self_return!=&source||source.acceso()!=self_owner||
            !equal(source,{5,0,8,0},3)||
            Watch::array_allocs!=self_made||Watch::array_frees!=self_freed)
            return 4;

        Tramo empty;
        Tramo& empty_alias=empty;
        const auto empty_self_made=Watch::array_allocs;
        const auto empty_self_freed=Watch::array_frees;
        Tramo& empty_self_return=(empty=empty_alias);
        if (&empty_self_return!=&empty||!equal(empty,{0,0,0,0},0)||
            Watch::array_allocs!=empty_self_made||
            Watch::array_frees!=empty_self_freed)return 5;

        // An empty source clears the target and frees its old array.
        Watch::expect_release(destination.acceso());
        const auto empty_made=Watch::array_allocs,empty_freed=Watch::array_frees;
        Tramo& empty_copy_return=(destination=empty);
        if (&empty_copy_return!=&destination||
            !equal(destination,{0,0,0,0},0)||
            !equal(empty,{0,0,0,0},0)||
            Watch::array_allocs!=empty_made||
            Watch::array_frees!=empty_freed+1||
            !Watch::released_expected)return 6;

        // A deterministic failed array allocation must not consume the old
        // destination or modify either source, even when values differ.
        Tramo occupied;
        if(!occupied.configurar(2)||!occupied.escribir(0,4)||
           !occupied.escribir(1,6))return 7;
        const int* prior_target=occupied.acceso();
        const int* prior_source=source.acceso();
        const auto fail_made=Watch::array_allocs,fail_freed=Watch::array_frees;
        Watch::fail_next_array=true;
        bool caught=false;
        try { occupied=source; }
        catch(const std::bad_alloc&) {caught=true;}
        Watch::fail_next_array=false;
        if(!caught||occupied.acceso()!=prior_target||
           source.acceso()!=prior_source||
           !equal(occupied,{4,6,0,0},2)||!equal(source,{5,0,8,0},3)||
           Watch::array_allocs!=fail_made||Watch::array_frees!=fail_freed)
            return 8;

        // Move into an existing target: transfer the same object, release old
        // destination and leave the source owner empty (including count).
        Watch::expect_release(occupied.acceso());
        const auto move_made=Watch::array_allocs,move_freed=Watch::array_frees;
        const int* transferred=source.acceso();
        Tramo& moved=(occupied=std::move(source));
        if(&moved!=&occupied||!equal(occupied,{5,0,8,0},3)||
           occupied.acceso()!=transferred||
           !equal(source,{0,0,0,0},0)||
           Watch::array_allocs!=move_made||Watch::array_frees!=move_freed+1||
           !Watch::released_expected)return 9;

        // Empty move destroys old occupied destination without allocation.
        Watch::expect_release(occupied.acceso());
        const auto empty_move_made=Watch::array_allocs;
        const auto empty_move_freed=Watch::array_frees;
        Tramo& empty_move_return=(occupied=std::move(source));
        if(&empty_move_return!=&occupied||!equal(occupied,{0,0,0,0},0)||
           !equal(source,{0,0,0,0},0)||
           Watch::array_allocs!=empty_move_made||
           Watch::array_frees!=empty_move_freed+1||
           !Watch::released_expected)return 10;

        // Self move must preserve pointer, count and values.
        if(!source.configurar(2)||!source.escribir(0,0)||
           !source.escribir(1,9))return 11;
        const int* self_move_owner=source.acceso();
        Tramo& move_alias=source;
        const auto self_move_made=Watch::array_allocs;
        const auto self_move_freed=Watch::array_frees;
        Tramo& self_move_return=(source=std::move(move_alias));
        if(&self_move_return!=&source||source.acceso()!=self_move_owner||
           !equal(source,{0,9,0,0},2)||
           Watch::array_allocs!=self_move_made||
           Watch::array_frees!=self_move_freed)return 12;
        source.vaciar();
        Tramo& self_empty_alias=source;
        const auto empty_self_move_made=Watch::array_allocs;
        const auto empty_self_move_freed=Watch::array_frees;
        Tramo& self_empty_move=(source=std::move(self_empty_alias));
        if(&self_empty_move!=&source||!equal(source,{0,0,0,0},0)||
           Watch::array_allocs!=empty_self_move_made||
           Watch::array_frees!=empty_self_move_freed)return 13;

        // Identical contents under distinct owners: moving must preserve the
        // original physical owner rather than manufacture an equivalent one.
        Tramo same_a,same_b;
        if(!same_a.configurar(2)||!same_b.configurar(2)||
           !same_a.escribir(0,0)||!same_a.escribir(1,3)||
           !same_b.escribir(0,0)||!same_b.escribir(1,3))return 14;
        const int* same_identity=same_a.acceso();
        Watch::expect_release(same_b.acceso());
        const auto same_made=Watch::array_allocs,same_freed=Watch::array_frees;
        same_b=std::move(same_a);
        if(same_b.acceso()!=same_identity||!equal(same_b,{0,3,0,0},2)||
           !equal(same_a,{0,0,0,0},0)||
           Watch::array_allocs!=same_made||Watch::array_frees!=same_freed+1||
           !Watch::released_expected)return 15;

        // A moved-from array can be independently used again.
        if(!same_a.configurar(1)||!same_a.escribir(0,7)||
           !equal(same_a,{7,0,0,0},1)||
           same_a.acceso()==same_b.acceso())return 16;

        // Exception unwinding must delete only live arrays once each.
        const auto pre_unwind_made=Watch::array_allocs;
        const auto pre_unwind_freed=Watch::array_frees;
        bool caught_unwind=false;
        try {
            Tramo a,b;
            if(!a.configurar(3)||!b.configurar(1))return 17;
            b=std::move(a);
            if(!equal(a,{0,0,0,0},0)||!equal(b,{0,0,0,0},3))
                return 18;
            throw 37;
        }catch(int value){caught_unwind=value==37;}
        if(!caught_unwind||Watch::array_allocs!=pre_unwind_made+2||
           Watch::array_frees!=pre_unwind_freed+2)return 19;
    }
    Watch::active=false;
    if(Watch::array_allocs!=Watch::array_frees||
       Watch::single_allocs!=Watch::single_frees||
       Watch::fail_next_array)return 20;
    std::fputs("LGAED_ARRAY_COPY_MOVE_SEMANTICS_V1",stderr);
    return 0;
}
"""

def run(command: list[str], *, timeout: int=90) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command,cwd=ROOT,check=False,text=True,encoding="utf-8",
                          errors="replace",capture_output=True,timeout=timeout)

def compile_cpp(compiler: str, source: Path, binary: Path, windows: bool):
    if windows:
        args=[compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
              "/permissive-","/Zc:__cplusplus","/utf-8","/Inative",
              str(source),"/Fe:"+str(binary)]
    else:
        args=[compiler,"-std=c++23","-Wall","-Wextra","-Werror",
              "-pedantic","-Inative",str(source),"-o",str(binary)]
    return run(args)

def original_block():
    document=json.loads(LESSON.read_text(encoding="utf-8"))
    owners=[(p,b) for p,page in enumerate(document["stateLayout"]["current"])
            for b,id in enumerate(page["blocks"]) if id==OWNER]
    assert len(owners)==1,"U6L11 published owner identity changed"
    p,b=owners[0]
    block=document["pages"][p]["blocks"][b]
    assert block["type"]=="cpp_compiler"
    assert len(block["outputCheck"]["cases"])==3
    return block

def compose(original: str, copy_body: str, move_body: str) -> str:
    copy="Tramo& Tramo::operator=(const Tramo& otra) {"
    move="Tramo& Tramo::operator=(Tramo&& otra) noexcept {"
    assert original.count(copy)==1 and original.count(move)==1
    copy_begin=original.index(copy)+len(copy)
    copy_end=original.index("\n}\nTramo& Tramo::operator=(Tramo&&",copy_begin)+1
    move_begin=original.index(move)+len(move)
    move_end=original.index("\n}\nbool Tramo::configurar",move_begin)+1
    assert copy_begin<copy_end<move_begin<move_end
    return (original[:copy_begin]+copy_body+original[copy_end:move_begin]+
            move_body+original[move_end:])

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=original_block()["initialCode"]
    variants={
        "swap_copy_move":(COPY["candidate_swap"],MOVE["move_and_exchange"],True),
        "ctor_copy_swap_move":(COPY["copy_constructor_exchange"],
                               MOVE["empty_swap_then_transfer"],True),
        **{name:(body,MOVE["move_and_exchange"],False)
           for name,body in BAD_COPY.items()},
        **{name:(COPY["candidate_swap"],body,False)
           for name,body in BAD_MOVE.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l11-array-") as directory:
        folder=Path(directory)
        suffix=".exe" if args.windows else ""
        for name,(copied,moved,expected) in variants.items():
            source=compose(original,copied,moved)
            generated=PRELUDE+source+DRIVER
            candidate=folder/(name+".cpp")
            candidate.write_bytes(generated.encode("utf-8"))
            binary=folder/(name+suffix)
            compiled=compile_cpp(args.compiler,candidate,binary,args.windows)
            if compiled.returncode:
                raise AssertionError((name,"failed to compile original+private test",
                                      compiled.stdout,compiled.stderr))
            observed=run([str(binary)],timeout=16)
            correct=(observed.returncode==0 and observed.stdout=="" and
                     observed.stderr==MARKER)
            if correct!=expected:
                raise AssertionError((name,"unexpected copy/move proof",
                                      observed.returncode,observed.stdout,observed.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if correct else 'REJECT'}",flush=True)
    print("PASS original U6L11 Tramo copy/move: 2 valid implementations, "
          "13 defective variants rejected, actual array identities, "
          "deterministic bad_alloc, empty and self operations, RAII cleanup.")

if __name__=="__main__":
    try:main()
    except Exception as e:
        print(f"FAIL real U6L11 array copy/move: {e}",file=sys.stderr)
        sys.exit(1)

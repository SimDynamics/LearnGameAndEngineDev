"""Compile the original U6L8 Muestra with a private, independently verified driver.

Only the body of Muestra::operator=(Muestra&& otra) may change. The test
exercises real resource identity, invariants and cleanup, not printed claims.
The student main and all provided class bodies remain byte-for-byte original.
This is independent QA; it does not yet provide in-app accreditation.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT / (
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "estado_y_ciclo_de_vida_de_clases/"
    "lesson_conservar_invariantes_al_mover_una_clase.json"
)
OWNER="block_0014_0002"
MARKER="LGAED_MOVE_INVARIANT_ASSIGNMENT_COMPLETE_V1"
VALID={
    "exchange_label":r"""
    if (this == &otra) return *this;
    dato_ = std::move(otra.dato_);
    etiqueta_ = std::exchange(otra.etiqueta_, 0);
    return *this;
""",
    "swap_and_clear":r"""
    if (this != &otra) {
        dato_.swap(otra.dato_);
        otra.dato_.reset();
        etiqueta_ = otra.etiqueta_;
        otra.etiqueta_ = 0;
    }
    return *this;
""",
}
REJECT={
    "source_label_not_cleared":r"""
    if (this == &otra) return *this;
    dato_ = std::move(otra.dato_);
    etiqueta_ = otra.etiqueta_;
    return *this;
""",
    "clones_instead_of_moves":r"""
    if (this == &otra) return *this;
    auto copy = otra.dato_ ? std::make_unique<int>(*otra.dato_) : nullptr;
    dato_ = std::move(copy);
    etiqueta_ = otra.etiqueta_;
    otra.vaciar();
    return *this;
""",
    "swap_leaves_target_in_source":r"""
    if (this == &otra) return *this;
    dato_.swap(otra.dato_);
    std::swap(etiqueta_, otra.etiqueta_);
    return *this;
""",
    "self_move_loses_label":r"""
    dato_ = std::move(otra.dato_);
    etiqueta_ = otra.etiqueta_;
    otra.etiqueta_ = 0;
    return *this;
""",
    "empty_source_keeps_destination":r"""
    if (this != &otra && otra.dato_) {
        dato_ = std::move(otra.dato_);
        etiqueta_ = std::exchange(otra.etiqueta_, 0);
    }
    return *this;
""",
    "wrong_return_reference":r"""
    if (this != &otra) {
        dato_ = std::move(otra.dato_);
        etiqueta_ = std::exchange(otra.etiqueta_, 0);
    }
    return static_cast<Muestra&>(otra);
""",
    "forges_tag":r"""
    if (this == &otra) return *this;
    dato_ = std::move(otra.dato_);
    etiqueta_ = dato_ ? 1 : 0;
    otra.etiqueta_ = 0;
    return *this;
""",
}
PRELUDE=r"""#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
namespace lgaed_move_invariant_qa {
struct Watch {
    inline static bool active=false,fail_next=false;
    inline static unsigned created=0,deleted=0;
    inline static const void* replaced=nullptr;
    inline static bool deleted_replaced=false;
    static void expect_delete(const void* pointer) {
        replaced=pointer;deleted_replaced=false;
    }
    static void note_delete(const void* pointer) noexcept {
        if(!active||!pointer)return;
        ++deleted;
        if(pointer==replaced) {
            deleted_replaced=true;replaced=nullptr;
        }
    }
};
}
void* operator new(std::size_t bytes) {
    using lgaed_move_invariant_qa::Watch;
    if(Watch::fail_next) {Watch::fail_next=false;throw std::bad_alloc{};}
    if(void* pointer=std::malloc(bytes?bytes:1)) {
        if(Watch::active)++Watch::created;
        return pointer;
    }
    throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept {
    lgaed_move_invariant_qa::Watch::note_delete(pointer);
    std::free(pointer);
}
void operator delete(void* pointer,std::size_t) noexcept {
    ::operator delete(pointer);
}
void* operator new[](std::size_t bytes) {return ::operator new(bytes);}
void operator delete[](void* pointer) noexcept {::operator delete(pointer);}
void operator delete[](void* pointer,std::size_t) noexcept {::operator delete(pointer);}
#define main lgaed_original_muestra_main
"""

DRIVER=r"""
#undef main
int main() {
    using lgaed_move_invariant_qa::Watch;
    Watch::active=true;
    {
        Muestra source,target;
        if(!source.poner(4,3)||!target.poner(7,8)||
           !source.coherente()||!target.coherente()||
           Watch::created!=2)return 1;
        const int* owned=source.acceso();
        Watch::expect_delete(target.acceso());
        const auto created=Watch::created,deleted=Watch::deleted;
        Muestra& returned=(target=std::move(source));
        if(&returned!=&target||source.hay()||source.etiqueta()!=0||
           !source.coherente()||!target.coherente()||
           target.acceso()!=owned||target.etiqueta()!=3||target.valor()!=4||
           Watch::created!=created||Watch::deleted!=deleted+1||
           !Watch::deleted_replaced)return 2;

        // Same int values do not make independently owned source/dest the same.
        Muestra same_a,same_b;
        if(!same_a.poner(5,6)||!same_b.poner(5,9))return 3;
        const int* same_owner=same_a.acceso();
        Watch::expect_delete(same_b.acceso());
        const auto same_created=Watch::created,same_deleted=Watch::deleted;
        same_b=std::move(same_a);
        if(same_b.acceso()!=same_owner||same_b.valor()!=5||
           same_b.etiqueta()!=6||!same_b.coherente()||
           same_a.hay()||same_a.etiqueta()!=0||!same_a.coherente()||
           Watch::created!=same_created||Watch::deleted!=same_deleted+1||
           !Watch::deleted_replaced)return 4;

        // Empty to occupied ends the prior object, with both empty afterward.
        Muestra empty,nonempty;
        if(!nonempty.poner(8,2))return 5;
        Watch::expect_delete(nonempty.acceso());
        const auto empty_created=Watch::created,empty_deleted=Watch::deleted;
        Muestra& empty_return=(nonempty=std::move(empty));
        if(&empty_return!=&nonempty||nonempty.hay()||empty.hay()||
           nonempty.etiqueta()!=0||empty.etiqueta()!=0||
           !nonempty.coherente()||!empty.coherente()||
           Watch::created!=empty_created||Watch::deleted!=empty_deleted+1||
           !Watch::deleted_replaced)return 6;

        // Self-move, including zero as an occupied value, preserves identity.
        Muestra self;
        if(!self.poner(0,9))return 7;
        const int* self_owner=self.acceso();
        Muestra& alias=self;
        const auto self_created=Watch::created,self_deleted=Watch::deleted;
        Muestra& self_return=(self=std::move(alias));
        if(&self_return!=&self||self.acceso()!=self_owner||self.valor()!=0||
           self.etiqueta()!=9||!self.coherente()||
           Watch::created!=self_created||Watch::deleted!=self_deleted)return 8;
        self.vaciar();
        Muestra& empty_alias=self;
        const auto empty_self_created=Watch::created;
        const auto empty_self_deleted=Watch::deleted;
        Muestra& empty_self_return=(self=std::move(empty_alias));
        if(&empty_self_return!=&self||self.hay()||self.etiqueta()!=0||
           !self.coherente()||Watch::created!=empty_self_created||
           Watch::deleted!=empty_self_deleted)return 9;

        // Deterministically deny the next allocation: a genuine move needs none.
        Muestra noalloc_source,noalloc_target;
        if(!noalloc_source.poner(0,5)||!noalloc_target.poner(3,1))
            return 10;
        const int* expected=noalloc_source.acceso();
        Watch::expect_delete(noalloc_target.acceso());
        const auto noalloc_created=Watch::created,noalloc_deleted=Watch::deleted;
        Watch::fail_next=true;
        bool threw=false;
        try {noalloc_target=std::move(noalloc_source);}
        catch(const std::bad_alloc&) {threw=true;}
        Watch::fail_next=false;
        if(threw||noalloc_target.acceso()!=expected||
           noalloc_target.valor()!=0||noalloc_target.etiqueta()!=5||
           noalloc_source.hay()||noalloc_source.etiqueta()!=0||
           !noalloc_target.coherente()||!noalloc_source.coherente()||
           Watch::created!=noalloc_created||Watch::deleted!=noalloc_deleted+1||
           !Watch::deleted_replaced)return 11;

        // The moved-from object remains reusable and rejected edits preserve it.
        if(!noalloc_source.poner(2,4))return 12;
        const int* reused=noalloc_source.acceso();
        if(noalloc_source.poner(2,0)||noalloc_source.cambiar(10)||
           noalloc_source.reetiquetar(10)||noalloc_source.acceso()!=reused||
           noalloc_source.valor()!=2||noalloc_source.etiqueta()!=4||
           !noalloc_source.coherente())return 13;

        // Normal unwinding must free the source and destination exactly once.
        const auto before_exception_created=Watch::created;
        const auto before_exception_deleted=Watch::deleted;
        bool caught=false;
        try {
            Muestra thrown_source,thrown_target;
            if(!thrown_source.poner(1,2)||!thrown_target.poner(2,3))
                return 14;
            thrown_target=std::move(thrown_source);
            if(thrown_source.hay()||thrown_source.etiqueta()!=0||
               !thrown_target.coherente())return 15;
            throw 21;
        } catch(int number) {caught=number==21;}
        if(!caught||Watch::created!=before_exception_created+2||
           Watch::deleted!=before_exception_deleted+2)return 16;
    }
    Watch::active=false;
    if(Watch::created!=Watch::deleted||Watch::fail_next)return 17;
    std::fputs("LGAED_MOVE_INVARIANT_ASSIGNMENT_COMPLETE_V1",stderr);
    return 0;
}
"""

def execute(argv: list[str],timeout: int=80) -> subprocess.CompletedProcess[str]:
    return subprocess.run(argv,cwd=ROOT,check=False,encoding="utf-8",
                          errors="replace",text=True,capture_output=True,timeout=timeout)

def compile_cpp(compiler: str,path: Path,binary: Path,windows: bool):
    argv=([compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
           "/permissive-","/Zc:__cplusplus","/utf-8","/Inative",str(path),
           "/Fe:"+str(binary)] if windows else
          [compiler,"-std=c++23","-Wall","-Wextra","-Werror",
           "-pedantic","-Inative",str(path),"-o",str(binary)])
    return execute(argv)

def original_block():
    lesson=json.loads(LESSON.read_text(encoding="utf-8"))
    found=[(p,b) for p,entry in enumerate(lesson["stateLayout"]["current"])
           for b,identity in enumerate(entry["blocks"]) if identity==OWNER]
    assert len(found)==1, "Published U6L8 historical block must be unique"
    p,b=found[0]
    block=lesson["pages"][p]["blocks"][b]
    assert block["type"]=="cpp_compiler"
    assert len(block["outputCheck"]["cases"])==3
    return block

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=original_block()["initialCode"]
    signature="Muestra& Muestra::operator=(Muestra&& otra) {"
    opening=original.index(signature)+len(signature)
    end=original.index("\n}\nbool Muestra::poner(int valor, int etiqueta)",opening)+1
    prefix,suffix=original[:opening],original[end:]
    assert original.count(signature)==1 and suffix.startswith("}")
    assert "int etiqueta_ = 0;" in prefix
    assert "otra.etiqueta_ = 0;" in prefix
    assert "int main()" in suffix
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l8-move-") as directory:
        root=Path(directory)
        exe=".exe" if args.windows else ""
        for name,body in [*VALID.items(),*REJECT.items()]:
            student=prefix+body+suffix
            assert student.startswith(prefix) and student.endswith(suffix)
            generated=PRELUDE+student+DRIVER
            path=root/(name+".cpp")
            path.write_bytes(generated.encode("utf-8"))
            binary=root/(name+exe)
            built=compile_cpp(args.compiler,path,binary,args.windows)
            if built.returncode:
                raise AssertionError((name,"compile error",built.stdout,built.stderr))
            ran=execute([str(binary)],timeout=12)
            accepted=(ran.returncode==0 and ran.stdout=="" and ran.stderr==MARKER)
            if accepted!=(name in VALID):
                raise AssertionError((name,"wrong ownership verdict",
                                      ran.returncode,ran.stdout,ran.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if accepted else 'REJECT'}",flush=True)
    print("PASS original U6L8: direct move identity, labels, no allocation, "
          "self and empty moves, changed/matching values, rejection, reuse, "
          "cleanup on normal and exceptional exit; seven decoy variants rejected.")

if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL independent U6L8 move invariant: {error}",file=sys.stderr)
        sys.exit(1)

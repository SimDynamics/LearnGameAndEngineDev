"""Real independent U6L10 array ownership/read semantic QA.

The original class, provided methods and main remain unchanged. Only the
bodies of Lote::configurar and Lote::leer are replaced with candidate
implementations. A private test-only friend controls a physically safe padded
window for invalid logical indices, avoiding out-of-bounds witness reads.
No curriculum/state files are modified.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
             "estado_y_ciclo_de_vida_de_clases/"
             "lesson_gestionar_arrays_con_unique_ptr_y_cantidad.json")
OWNER="block_0017_0002"
MARKER="LGAED_ARRAY_OWNER_READ_COMPLETE_V1"

CONFIGURE={
 "allocate_then_swap":r"""
    if (cantidad < 0 || cantidad > 4) return false;
    if (cantidad == 0) { datos_.reset(); cantidad_ = 0; return true; }
    try {
        auto candidato = std::make_unique<int[]>(cantidad);
        datos_.swap(candidato);
        cantidad_ = cantidad;
        return true;
    } catch(const std::bad_alloc&) { return false; }
""",
 "allocate_then_assign":r"""
    if (cantidad > 4 || cantidad < 0) return false;
    if (!cantidad) {
        datos_ = nullptr;
        cantidad_ = 0;
        return true;
    }
    try {
        std::unique_ptr<int[]> nuevo{new int[cantidad]{}};
        datos_ = std::move(nuevo);
        cantidad_ = cantidad;
        return true;
    } catch(const std::bad_alloc&) { return false; }
"""
}
READ={
 "bounds_first":r"""
    if (indice < 0 || indice >= cantidad_) return false;
    resultado = datos_[indice];
    return true;
""",
 "explicit_valid_range":r"""
    if (indice >= 0 && indice < cantidad_ && datos_) {
        resultado = datos_[indice];
        return true;
    }
    return false;
"""
}
BAD_CONFIGURE={
 "reuse_old_same_size":r"""
    if (cantidad < 0 || cantidad > 4) return false;
    if (cantidad == 0) { datos_.reset(); cantidad_ = 0; return true; }
    if (cantidad == cantidad_ && datos_) {
        for (int i = 0; i < cantidad; ++i) datos_[i] = 0;
        return true;
    }
    try {
        auto next = std::make_unique<int[]>(cantidad);
        datos_.swap(next);
        cantidad_ = cantidad;
        return true;
    } catch(const std::bad_alloc&) { return false; }
""",
 "lose_old_on_failure":r"""
    if (cantidad < 0 || cantidad > 4) return false;
    datos_.reset();
    if (cantidad == 0) { cantidad_ = 0; return true; }
    try {
        datos_ = std::make_unique<int[]>(cantidad);
        cantidad_ = cantidad;
        return true;
    } catch(const std::bad_alloc&) { cantidad_ = 0; return false; }
""",
 "filled_with_ones":r"""
    if (cantidad < 0 || cantidad > 4) return false;
    if (!cantidad) { datos_.reset(); cantidad_ = 0; return true; }
    try {
        auto next = std::make_unique<int[]>(cantidad);
        for (int i = 0; i < cantidad; ++i) next[i] = 1;
        datos_.swap(next);
        cantidad_ = cantidad;
        return true;
    } catch(const std::bad_alloc&) { return false; }
""",
 "zero_keeps_owner":r"""
    if (cantidad < 0 || cantidad > 4) return false;
    if (!cantidad) { cantidad_ = 0; return true; }
    try {
        auto next = std::make_unique<int[]>(cantidad);
        datos_.swap(next);
        cantidad_ = cantidad;
        return true;
    } catch(const std::bad_alloc&) { return false; }
""",
 "rejects_four":r"""
    if (cantidad < 0 || cantidad > 3) return false;
    if (!cantidad) { datos_.reset(); cantidad_ = 0; return true; }
    try {
        auto next = std::make_unique<int[]>(cantidad);
        datos_.swap(next);
        cantidad_ = cantidad;
        return true;
    } catch(const std::bad_alloc&) { return false; }
"""
}
BAD_READ={
 "reject_changes_output":r"""
    if (indice < 0 || indice >= cantidad_) {
        resultado = -1;
        return false;
    }
    resultado = datos_[indice];
    return true;
""",
 "always_first":r"""
    if (indice < 0 || indice >= cantidad_) return false;
    resultado = datos_[0];
    return true;
""",
 "no_upper_bound":r"""
    if (indice < 0) return false;
    resultado = datos_[indice];
    return true;
""",
 "no_lower_bound":r"""
    if (indice >= cantidad_) return false;
    resultado = datos_[indice];
    return true;
"""
}
PRELUDE=r"""#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace lgaed_array_qa {
struct Access;
struct Watch {
    inline static bool active=false,fail_next_array=false;
    inline static unsigned arrays_new=0,arrays_delete=0;
    inline static unsigned singles_new=0,singles_delete=0;
    inline static const void* last_created_array=nullptr;
    inline static const void* expected_deleted=nullptr;
    inline static bool saw_expected_deleted=false;
    static void watch_delete(const void* ptr,bool array) noexcept {
        if(!active||!ptr)return;
        if(array)++arrays_delete; else ++singles_delete;
        if(array && ptr==expected_deleted) {
            saw_expected_deleted=true;
            expected_deleted=nullptr;
        }
    }
    static void expect_delete(const void* ptr) {
        expected_deleted=ptr;saw_expected_deleted=false;
    }
};
}
void* operator new(std::size_t bytes) {
    if(void* p=std::malloc(bytes?bytes:1)) {
        if(lgaed_array_qa::Watch::active)++lgaed_array_qa::Watch::singles_new;
        return p;
    }
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept {
    lgaed_array_qa::Watch::watch_delete(p,false);
    std::free(p);
}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void* operator new[](std::size_t bytes) {
    auto& failure=lgaed_array_qa::Watch::fail_next_array;
    if(failure){failure=false;throw std::bad_alloc{};}
    if(void* p=std::malloc(bytes?bytes:1)) {
        if(lgaed_array_qa::Watch::active) {
            ++lgaed_array_qa::Watch::arrays_new;
            lgaed_array_qa::Watch::last_created_array=p;
        }
        return p;
    }
    throw std::bad_alloc{};
}
void operator delete[](void* p) noexcept {
    lgaed_array_qa::Watch::watch_delete(p,true);
    std::free(p);
}
void operator delete[](void* p,std::size_t) noexcept {::operator delete[](p);}
#define main lgaed_original_lote_main
"""

DRIVER=r"""
#undef main
namespace lgaed_array_qa {
struct Access {
    static void set_window(Lote& lot,int* p,int count) {
        lot.datos_.reset(p);
        lot.cantidad_=count;
    }
    static void restore_window(Lote& lot,int* base) noexcept {
        (void)lot.datos_.release();
        lot.datos_.reset(base);
        lot.cantidad_=4;
    }
};
struct Window {
    Lote lot;
    int* base;
    Window():lot{} {
        auto owned=std::make_unique<int[]>(4);
        base=owned.release();
        base[0]=91;
        base[1]=5;
        base[2]=7;
        base[3]=73;
        Access::set_window(lot,base+1,2);
    }
    ~Window() {Access::restore_window(lot,base);}
    Window(const Window&)=delete;
    Window& operator=(const Window&)=delete;
};
}

int main() {
    using lgaed_array_qa::Watch;
    Watch::active=true;
    {
        Lote lot;
        if(lot.cantidad()!=0||lot.acceso()!=nullptr)return 1;
        if(!lot.configurar(2)||lot.cantidad()!=2||!lot.acceso()||
           lot.acceso()!=Watch::last_created_array||
           Watch::arrays_new!=1||Watch::arrays_delete!=0||
           lot.sumar()!=0)return 2;
        int out=84;
        if(!lot.leer(0,out)||out!=0||!lot.leer(1,out)||out!=0)return 3;
        if(!lot.escribir(0,5)||!lot.escribir(1,7)||lot.sumar()!=12||
           !lot.leer(1,out)||out!=7)return 4;

        // Both logical invalid indices access safe padding in a separate
        // independent window; a faulty unchecked read never needs to touch
        // physically invalid storage in these controlled probes.
        {
            lgaed_array_qa::Window padded;
            int sentinel=88;
            if(padded.lot.leer(-1,sentinel)||sentinel!=88)return 5;
            sentinel=214;
            if(padded.lot.leer(2,sentinel)||sentinel!=214)return 6;
            sentinel=41;
            if(!padded.lot.leer(1,sentinel)||sentinel!=7)return 7;
        }
        const int* owner=lot.acceso();
        const auto created=Watch::arrays_new,deleted=Watch::arrays_delete;
        for(int invalid : {-2147483647-1,-1,5,2147483647}) {
            if(lot.configurar(invalid)||lot.acceso()!=owner||
               lot.cantidad()!=2||lot.sumar()!=12||
               Watch::arrays_new!=created||Watch::arrays_delete!=deleted)
                return 8;
        }
        if(!lot.configurar(2)||lot.cantidad()!=2||lot.sumar()!=0||
           lot.acceso()!=Watch::last_created_array||
           Watch::arrays_new!=created+1||Watch::arrays_delete!=deleted+1)
            return 9;
        if(!lot.escribir(0,9)||!lot.escribir(1,3))return 10;
        const int* before_failure=lot.acceso();
        const auto created_before_failure=Watch::arrays_new;
        const auto deleted_before_failure=Watch::arrays_delete;
        Watch::fail_next_array=true;
        bool accepted=false;
        try {accepted=lot.configurar(3);}
        catch (...) {Watch::fail_next_array=false;return 11;}
        Watch::fail_next_array=false;
        if(accepted||lot.acceso()!=before_failure||lot.cantidad()!=2||
           lot.sumar()!=12||Watch::arrays_new!=created_before_failure||
           Watch::arrays_delete!=deleted_before_failure)return 12;

        // Fresh four-element array, even after a preceding failed request.
        Watch::expect_delete(lot.acceso());
        const auto created_before_four=Watch::arrays_new;
        const auto deleted_before_four=Watch::arrays_delete;
        if(!lot.configurar(4)||lot.cantidad()!=4||lot.sumar()!=0||
           lot.acceso()!=Watch::last_created_array||
           Watch::arrays_new!=created_before_four+1||
           Watch::arrays_delete!=deleted_before_four+1||
           !Watch::saw_expected_deleted)return 13;
        for(int index=0;index<4;++index) {
            int value=9;
            if(!lot.leer(index,value)||value!=0)return 14;
        }
        if(!lot.escribir(3,8)||lot.sumar()!=8)return 15;

        // Zero must release the exact previous array, without allocating.
        Watch::expect_delete(lot.acceso());
        const auto created_before_empty=Watch::arrays_new;
        const auto deleted_before_empty=Watch::arrays_delete;
        if(!lot.configurar(0)||lot.cantidad()!=0||lot.acceso()||
           Watch::arrays_new!=created_before_empty||
           Watch::arrays_delete!=deleted_before_empty+1||
           !Watch::saw_expected_deleted)return 16;
        const auto empty_created=Watch::arrays_new,empty_deleted=Watch::arrays_delete;
        if(!lot.configurar(0)||lot.cantidad()!=0||lot.acceso()||
           Watch::arrays_new!=empty_created||
           Watch::arrays_delete!=empty_deleted)return 17;
        if(!lot.configurar(1)||lot.cantidad()!=1||!lot.acceso()||
           lot.sumar()!=0)return 18;
        int valid=91;
        if(!lot.leer(0,valid)||valid!=0)return 19;

        // A rejected update or invalid write keeps present zero and capacity.
        const int* identity=lot.acceso();
        if(lot.escribir(0,10)||lot.escribir(1,9)||lot.escribir(-1,2)||
           lot.acceso()!=identity||lot.cantidad()!=1||lot.sumar()!=0)
            return 20;

        // Unwinding a caller's exception frees the owned array exactly once.
        const auto created_before_throw=Watch::arrays_new;
        const auto deleted_before_throw=Watch::arrays_delete;
        bool caught=false;
        try {
            Lote temporary;
            if(!temporary.configurar(3))return 21;
            if(!temporary.escribir(2,6))return 22;
            throw 31;
        }catch(int code){caught=(code==31);}
        if(!caught||Watch::arrays_new!=created_before_throw+1||
           Watch::arrays_delete!=deleted_before_throw+1)return 23;
    }
    Watch::active=false;
    if(Watch::arrays_new!=Watch::arrays_delete||
       Watch::singles_new!=Watch::singles_delete||
       Watch::fail_next_array)return 24;
    std::fputs("LGAED_ARRAY_OWNER_READ_COMPLETE_V1",stderr);
    return 0;
}
"""

def run(args:list[str],timeout:int=90):
    return subprocess.run(args,cwd=ROOT,encoding="utf-8",errors="replace",
                          text=True,capture_output=True,check=False,timeout=timeout)

def compile_cpp(compiler:str,src:Path,exe:Path,windows:bool):
    if windows:
        args=[compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
              "/permissive-","/Zc:__cplusplus","/utf-8","/Inative",
              str(src),"/Fe:"+str(exe)]
    else:
        args=[compiler,"-std=c++23","-Wall","-Wextra","-Werror",
              "-pedantic","-Inative",str(src),"-o",str(exe)]
    return run(args)

def initial_code():
    doc=json.loads(LESSON.read_text(encoding="utf-8"))
    positions=[(p,b) for p,page in enumerate(doc["stateLayout"]["current"])
               for b,id in enumerate(page["blocks"]) if id==OWNER]
    assert len(positions)==1,"U6L10 original owner changed"
    p,b=positions[0]
    block=doc["pages"][p]["blocks"][b]
    assert block["type"]=="cpp_compiler" and len(block["outputCheck"]["cases"])==3
    return block["initialCode"]

def compose(original:str,configure:str,read_body:str):
    config="bool Lote::configurar(int cantidad) noexcept {"
    read="bool Lote::leer(int indice, int& resultado) const noexcept {"
    assert original.count(config)==1 and original.count(read)==1
    a=original.index(config)+len(config)
    b=original.index("\n}\nbool Lote::leer",a)+1
    c=original.index(read)+len(read)
    d=original.index("\n}\nbool Lote::escribir",c)+1
    assert a<b<c<d
    return original[:a]+configure+original[b:c]+read_body+original[d:]

def instrument(source:str):
    opening="class Lote {\n"
    assert source.count(opening)==1
    return source.replace(opening,
        "class Lote {\n    friend struct lgaed_array_qa::Access;\n",1)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=initial_code()
    variants={
        "candidate_swap":(CONFIGURE["allocate_then_swap"],READ["bounds_first"],True),
        "new_then_assign":(CONFIGURE["allocate_then_assign"],READ["explicit_valid_range"],True),
        **{name:(body,READ["bounds_first"],False)
           for name,body in BAD_CONFIGURE.items()},
        **{name:(CONFIGURE["allocate_then_swap"],body,False)
           for name,body in BAD_READ.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l10-array-") as folder:
        directory=Path(folder)
        ext=".exe" if args.windows else ""
        for name,(configure,reader,expected) in variants.items():
            source=compose(original,configure,reader)
            generated=PRELUDE+instrument(source)+DRIVER
            code=directory/(name+".cpp")
            code.write_bytes(generated.encode("utf-8"))
            exe=directory/(name+ext)
            compiled=compile_cpp(args.compiler,code,exe,args.windows)
            if compiled.returncode:
                raise AssertionError((name,"compile failed",
                                      compiled.stdout,compiled.stderr))
            tested=run([str(exe)],timeout=15)
            correct=(tested.returncode==0 and
                     tested.stdout=="" and tested.stderr==MARKER)
            if correct!=expected:
                raise AssertionError((name,"wrong independent array verdict",
                                      tested.returncode,tested.stdout,tested.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if correct else 'REJECT'}",flush=True)
    print("PASS U6L10 actual original Lote: two valid implementations, "
          "nine wrong cases rejected, fresh arrays, strong bad_alloc guarantee, "
          "physical padded guard for invalid logical reads and array cleanup.")

if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL independent U6L10 array semantics: {error}",file=sys.stderr)
        sys.exit(1)

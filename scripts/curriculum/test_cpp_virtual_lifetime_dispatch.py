"""Prove genuine virtual dispatch across construction, use and destruction.

Programación 3 U8L6: edit only Medida::registrar and
MedidaIdentificada::escribir in the original single-file lesson.
Independent calls with misleading phase labels expose implementations
that memorize the visible stdout instead of performing virtual dispatch.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "composicion_y_herencia_publica/"
    "lesson_seguir_llamadas_virtuales_durante_construccion_y_destruccion.json"
)
OWNER = "block_0015_0002"
MARKER = "LGAED_VIRTUAL_LIFETIME_DISPATCH_V1"

REG_SIGNATURE = "void Medida::registrar(const char* etapa, std::ostream& salida) const {"
REG_END = "\n}\nvoid MedidaIdentificada::escribir"
WRITE_SIGNATURE = "void MedidaIdentificada::escribir(std::ostream& salida) const {"
WRITE_END = "\n}\nvoid usar"

GOOD_REG = r"""
    salida << etapa << ':';
    escribir(salida);
    salida << '\n';
"""
GOOD_WRITE = r"""
    Medida::escribir(salida);
    salida << ";Codigo:" << codigo_;
"""
ALT_REG = r"""
    salida << etapa << ":";
    this->escribir(salida);
    salida.put('\n');
"""
ALT_WRITE = r"""
    salida << "Numero:" << numero() << ";Codigo:" << codigo();
"""
BAD_REG = {
    "calls_base_even_during_use": r"""
    salida << etapa << ':';
    Medida::escribir(salida);
    salida << '\n';
""",
    "guesses_virtual_dispatch_from_phase_label": r"""
    salida << etapa << ':';
    if (std::string(etapa) == "ConstruirBase" ||
        std::string(etapa) == "DestruirBase")
        Medida::escribir(salida);
    else
        escribir(salida);
    salida << '\n';
""",
    "prints_to_stdout_instead_of_requested_stream": r"""
    (void)salida;
    std::cout << etapa << ':';
    escribir(std::cout);
    std::cout << '\n';
""",
    "omits_the_phase_label": r"""
    (void)etapa;
    escribir(salida);
    salida << '\n';
""",
    "drops_the_line_ending": r"""
    salida << etapa << ':';
    escribir(salida);
""",
    "always_prints_base_and_known_derived_code": r"""
    salida << etapa << ":Numero:" << numero() << ";Codigo:41\n";
""",
}
BAD_WRITE = {
    "does_not_add_derived_code": r"""
    Medida::escribir(salida);
""",
    "prints_a_fixed_code_for_first_visible_case": r"""
    Medida::escribir(salida);
    salida << ";Codigo:41";
""",
    "loses_code_zero": r"""
    Medida::escribir(salida);
    if (codigo_ != 0) salida << ";Codigo:" << codigo_;
""",
    "uses_code_as_number": r"""
    salida << "Numero:" << codigo_ << ";Codigo:" << codigo_;
""",
    "writes_to_stdout_instead_of_supplied_stream": r"""
    (void)salida;
    Medida::escribir(std::cout);
    std::cout << ";Codigo:" << codigo_;
""",
    "changes_a_derived_code": r"""
    Medida::escribir(salida);
    salida << ";Codigo:" << (codigo_ + 1);
""",
}

# The driver sees the original source but calls the supplied two functions
# independently. Markers are emitted only after every real C++ check succeeds.
DRIVER = r"""
#undef main
int main() {
    const auto same=[](const std::ostringstream& output,
                       std::string_view expected)->bool {
        return output.str() == expected;
    };
    {
        std::ostringstream history;
        {
            MedidaIdentificada value{0,-3,history};
            if(!same(history,
                "ConstruirBase:Numero:0\n"
                "ConstruirDerivada:Numero:0;Codigo:-3\n"))return 1;
            const Medida& view=value;

            // These are *ordinary use* calls on a live derived object.
            // Their text resembles the base constructor/destructor phases.
            // A label-dependent dispatch hack must not force base here.
            std::ostringstream during_base_label;
            view.registrar("ConstruirBase",during_base_label);
            if(!same(during_base_label,
                "ConstruirBase:Numero:0;Codigo:-3\n"))return 2;
            std::ostringstream during_death_label;
            view.registrar("DestruirBase",during_death_label);
            if(!same(during_death_label,
                "DestruirBase:Numero:0;Codigo:-3\n"))return 3;
            std::ostringstream unfamiliar;
            view.registrar("Otra fase",unfamiliar);
            if(!same(unfamiliar,"Otra fase:Numero:0;Codigo:-3\n"))return 4;
            std::ostringstream empty_label;
            view.registrar("",empty_label);
            if(!same(empty_label,":Numero:0;Codigo:-3\n"))return 5;

            // A requested stream is not the constructor's history stream.
            std::ostringstream report;
            view.escribir(report);
            if(!same(report,"Numero:0;Codigo:-3"))return 6;
            std::ostringstream explicit_base;
            view.Medida::escribir(explicit_base);
            if(!same(explicit_base,"Numero:0"))return 7;
            if(!same(history,
                "ConstruirBase:Numero:0\n"
                "ConstruirDerivada:Numero:0;Codigo:-3\n"))return 8;
        }
        if(!same(history,
            "ConstruirBase:Numero:0\n"
            "ConstruirDerivada:Numero:0;Codigo:-3\n"
            "DestruirDerivada:Numero:0;Codigo:-3\n"
            "DestruirBase:Numero:0\n"))return 9;
    }
    {
        // A plain base object must *remain* base even if the supplied
        // string resembles a derived phase.
        std::ostringstream history;
        {
            Medida base{-7,history};
            if(!same(history,"ConstruirBase:Numero:-7\n"))return 10;
            std::ostringstream external;
            base.registrar("ConstruirDerivada",external);
            if(!same(external,"ConstruirDerivada:Numero:-7\n"))return 11;
            std::ostringstream other;
            base.registrar("DestruirDerivada",other);
            if(!same(other,"DestruirDerivada:Numero:-7\n"))return 12;
        }
        if(!same(history,
            "ConstruirBase:Numero:-7\n"
            "DestruirBase:Numero:-7\n"))return 13;
    }
    {
        // The original lifetime phases are still genuine for two other
        // codes and for reset through a virtual base owner.
        std::ostringstream history;
        {
            std::unique_ptr<Medida> instance =
                std::make_unique<MedidaIdentificada>(19,0,history);
            std::ostringstream report;
            instance->registrar("Uso",report);
            if(!same(report,"Uso:Numero:19;Codigo:0\n"))return 14;
            instance.reset();
        }
        if(!same(history,
            "ConstruirBase:Numero:19\n"
            "ConstruirDerivada:Numero:19;Codigo:0\n"
            "DestruirDerivada:Numero:19;Codigo:0\n"
            "DestruirBase:Numero:19\n"))return 15;
    }
    {
        std::ostringstream history;
        {
            MedidaIdentificada value{-11,23,history};
            const Medida& view=value;
            std::ostringstream result;
            view.registrar("DestruirDerivada",result);
            if(!same(result,"DestruirDerivada:Numero:-11;Codigo:23\n"))
                return 16;
        }
        if(!same(history,
            "ConstruirBase:Numero:-11\n"
            "ConstruirDerivada:Numero:-11;Codigo:23\n"
            "DestruirDerivada:Numero:-11;Codigo:23\n"
            "DestruirBase:Numero:-11\n"))return 17;
    }
    std::fputs("LGAED_VIRTUAL_LIFETIME_DISPATCH_V1",stderr);
    return 0;
}
"""


def original_editor() -> str:
    lesson=json.loads(LESSON.read_text(encoding="utf-8"))
    layout.validate(lesson)
    positions=[
        (p,b) for p,page in enumerate(lesson["stateLayout"]["current"])
        for b,identity in enumerate(page["blocks"]) if identity==OWNER
    ]
    assert len(positions)==1,"Original U8L6 compiler owner absent"
    p,b=positions[0]
    editor=lesson["pages"][p]["blocks"][b]
    assert editor["type"]=="cpp_compiler"
    assert len(editor["outputCheck"]["cases"])==5
    source=editor["initialCode"]
    for signature in (REG_SIGNATURE,WRITE_SIGNATURE):
        assert source.count(signature)==1,signature
    return source


def body_span(source: str, signature: str, end: str) -> tuple[int,int]:
    assert source.count(signature)==1
    assert signature.endswith("{")
    at,close=layout._named_rule_body_span(source,signature[:-1].rstrip())
    assert source[at-1]=="{" and source[close]=="}"
    return at,close


def compose(original: str, registrar: str, derived_write: str) -> str:
    reg_at,reg_end=body_span(original,REG_SIGNATURE,REG_END)
    write_at,write_end=body_span(original,WRITE_SIGNATURE,WRITE_END)
    assert reg_at<reg_end<write_at<write_end
    return (original[:reg_at]+registrar+original[reg_end:write_at]+
            derived_write+original[write_end:])


def run(args: list[str], timeout: int=75) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args,cwd=ROOT,check=False,text=True,
                          encoding="utf-8",errors="replace",
                          capture_output=True,timeout=timeout)


def compile_cpp(compiler: str, source: Path, executable: Path, windows: bool):
    if windows:
        command=[compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
                 "/permissive-","/Zc:__cplusplus","/utf-8",
                 str(source),"/Fe:"+str(executable)]
    else:
        command=[compiler,"-std=c++23","-Wall","-Wextra","-Werror",
                 "-pedantic",str(source),"-o",str(executable)]
    return run(command)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    args=parser.parse_args()
    original=original_editor()
    scenarios={
        "real_virtual_calls":(GOOD_REG,GOOD_WRITE,True),
        "equivalent_virtual_calls":(ALT_REG,ALT_WRITE,True),
        **{name:(value,GOOD_WRITE,False) for name,value in BAD_REG.items()},
        **{name:(GOOD_REG,value,False) for name,value in BAD_WRITE.items()},
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l6-dispatch-") as folder:
        tmp=Path(folder)
        suffix=".exe" if args.windows else ""
        for name,(reg,write,expected) in scenarios.items():
            source=tmp/(name+".cpp")
            source.write_text(
                "#include <cstdio>\n#include <string_view>\n"+
                "#define main lgaed_published_original_main\n"+
                compose(original,reg,write)+DRIVER,
                encoding="utf-8")
            executable=tmp/(name+suffix)
            built=compile_cpp(args.compiler,source,executable,args.windows)
            if built.returncode:
                raise AssertionError((name,"C++ compilation failed",
                                      built.stdout,built.stderr))
            observed=run([str(executable)],timeout=16)
            accepted=(observed.returncode==0 and observed.stdout=="" and
                      observed.stderr==MARKER)
            if accepted!=expected:
                raise AssertionError((name,"incorrect virtual-lifetime verdict",
                                      observed.returncode,
                                      observed.stdout,observed.stderr))
            print(f"{args.compiler}: {name}: "
                  f"{'PASS' if accepted else 'REJECT'}",flush=True)
    print("PASS U8L6 genuine virtual dispatch through construction, live "
          "derived/base calls, destruction, independent streams, misleading "
          "phase labels, zero/negative values and base-owner reset; "
          "2 legitimate implementations, 12 defective versions rejected.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U8L6 virtual lifetime dispatch: {error}",file=sys.stderr)
        sys.exit(1)

"""Independent native C++ semantic regression for Programación 3 U8L5.

The immutable lesson provides Ciclo, Ficha, Medida, MedidaIdentificada and main.
Only three original function bodies are changed in an isolated temporary source.
These checks do not amend curriculum JSON or award an in-app achievement.
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
    "composicion_y_herencia_publica/lesson_destruir_una_derivada_mediante_la_base.json"
)
OWNER = "block_0020_0002"
MARKER = "LGAED_POLYMORPHIC_DESTRUCTION_V1"

BASE = (
    "Medida::~Medida() noexcept {",
    "\n}\nMedidaIdentificada::~MedidaIdentificada() noexcept {",
)
DERIVED = (
    "MedidaIdentificada::~MedidaIdentificada() noexcept {",
    "\n}\nstd::unique_ptr<Medida> crear_medida",
)
FACTORY = (
    "std::unique_ptr<Medida> crear_medida(int numero, int codigo, Ciclo& ciclo) {",
    "\n}\nint main()",
)

GOOD_BASE = """
    --ciclo_.bases_vivas;
    ++ciclo_.bases_destruidas;
"""
GOOD_DERIVED = """
    --balance_.derivadas_vivas;
    ++balance_.derivadas_destruidas;
"""
GOOD_FACTORY = """
    return std::make_unique<MedidaIdentificada>(numero, codigo, ciclo);
"""

GOOD_BASE_ALT = """
    ciclo_.bases_vivas -= 1;
    ciclo_.bases_destruidas += 1;
"""
GOOD_DERIVED_ALT = """
    balance_.derivadas_vivas -= 1;
    balance_.derivadas_destruidas += 1;
"""
GOOD_FACTORY_ALT = """
    std::unique_ptr<MedidaIdentificada> creada =
        std::make_unique<MedidaIdentificada>(numero, codigo, ciclo);
    std::unique_ptr<Medida> resultado = std::move(creada);
    return resultado;
"""

BAD_BASE = {
    "base_does_not_record_destruction": """
    (void)ciclo_;
""",
    "base_counts_twice": """
    --ciclo_.bases_vivas;
    ciclo_.bases_destruidas += 2;
""",
    "base_resets_all_live_objects": """
    ciclo_.bases_vivas = 0;
    ++ciclo_.bases_destruidas;
""",
}
BAD_DERIVED = {
    "derived_does_not_record_destruction": """
    (void)balance_;
""",
    "derived_counts_twice": """
    --balance_.derivadas_vivas;
    balance_.derivadas_destruidas += 2;
""",
    "derived_forgets_to_remove_live_object": """
    ++balance_.derivadas_destruidas;
""",
}
BAD_FACTORY = {
    "factory_returns_base": """
    (void)codigo;
    return std::make_unique<Medida>(numero, ciclo);
""",
    "factory_forgets_code": """
    (void)codigo;
    return std::make_unique<MedidaIdentificada>(numero, 0, ciclo);
""",
    "factory_forgets_number": """
    (void)numero;
    return std::make_unique<MedidaIdentificada>(0, codigo, ciclo);
""",
    "factory_uses_unrelated_balance": """
    (void)ciclo;
    static Ciclo otro;
    return std::make_unique<MedidaIdentificada>(numero, codigo, otro);
""",
    "factory_creates_and_discards_extra_owner": """
    {
        auto descartada = std::make_unique<MedidaIdentificada>(numero, codigo, ciclo);
        (void)descartada;
    }
    return std::make_unique<MedidaIdentificada>(numero, codigo, ciclo);
""",
    "factory_returns_new_base_after_derived": """
    {
        auto descartada = std::make_unique<MedidaIdentificada>(numero, codigo, ciclo);
        (void)descartada;
    }
    return std::make_unique<Medida>(numero, ciclo);
""",
}

DRIVER = r"""
#undef main
int main() {
    const auto state = [](const Ciclo& value, int alive_base,
            int alive_derived, int alive_ficha, int dead_base,
            int dead_derived, int dead_ficha) -> bool {
        return value.bases_vivas == alive_base &&
               value.derivadas_vivas == alive_derived &&
               value.fichas_vivas == alive_ficha &&
               value.bases_destruidas == dead_base &&
               value.derivadas_destruidas == dead_derived &&
               value.fichas_destruidas == dead_ficha;
    };
    {
        Ciclo cycle;
        auto source = crear_medida(0, -3, cycle);
        if (!source || !dynamic_cast<MedidaIdentificada*>(source.get()) ||
            !state(cycle, 1, 1, 1, 0, 0, 0)) return 1;
        std::ostringstream output;
        source->escribir(output);
        if (output.str() != "Numero:0;Codigo:-3") return 2;
        const Medida* identity = source.get();
        std::unique_ptr<Medida> destination = std::move(source);
        if (source || destination.get() != identity ||
            !state(cycle, 1, 1, 1, 0, 0, 0)) return 3;
        destination.reset();
        if (!state(cycle, 0, 0, 0, 1, 1, 1)) return 4;
        destination.reset();
        if (!state(cycle, 0, 0, 0, 1, 1, 1)) return 5;
    }
    {
        // Nonzero preexisting counters expose implementations that write a
        // constant zero or reset a shared register instead of subtracting.
        Ciclo cycle;
        cycle.bases_vivas = 5;
        cycle.derivadas_vivas = 2;
        cycle.fichas_vivas = 7;
        cycle.bases_destruidas = 11;
        cycle.derivadas_destruidas = 13;
        cycle.fichas_destruidas = 17;
        auto first = crear_medida(7, 0, cycle);
        auto second = crear_medida(-7, 19, cycle);
        if (!first || !second || first.get() == second.get() ||
            !dynamic_cast<MedidaIdentificada*>(first.get()) ||
            !dynamic_cast<MedidaIdentificada*>(second.get()) ||
            !state(cycle, 7, 4, 9, 11, 13, 17)) return 6;
        std::ostringstream a, b;
        first->escribir(a);
        second->escribir(b);
        if (a.str() != "Numero:7;Codigo:0" ||
            b.str() != "Numero:-7;Codigo:19") return 7;
        first.reset();
        if (!state(cycle, 6, 3, 8, 12, 14, 18)) return 8;
        second.reset();
        if (!state(cycle, 5, 2, 7, 13, 15, 19)) return 9;
    }
    {
        // Two independent records must not affect one another.
        Ciclo first, second;
        auto a = crear_medida(-5, 33, first);
        auto b = crear_medida(14, 0, second);
        if (!state(first, 1, 1, 1, 0, 0, 0) ||
            !state(second, 1, 1, 1, 0, 0, 0)) return 10;
        b.reset();
        if (!state(first, 1, 1, 1, 0, 0, 0) ||
            !state(second, 0, 0, 0, 1, 1, 1)) return 11;
        a.reset();
        if (!state(first, 0, 0, 0, 1, 1, 1)) return 12;
    }
    {
        // RAII must run after a caller exception, not just a manual reset.
        Ciclo cycle;
        bool caught = false;
        try {
            auto value = crear_medida(-12, 28, cycle);
            if (!value || !dynamic_cast<MedidaIdentificada*>(value.get()) ||
                !state(cycle, 1, 1, 1, 0, 0, 0)) return 13;
            throw 31;
        } catch (int value) { caught = value == 31; }
        if (!caught || !state(cycle, 0, 0, 0, 1, 1, 1)) return 14;
    }
    std::fputs("LGAED_POLYMORPHIC_DESTRUCTION_V1", stderr);
    return 0;
}
"""


def original_editor() -> str:
    lesson = json.loads(LESSON.read_text(encoding="utf-8"))
    layout.validate(lesson)
    locations = [
        (page_index, block_index)
        for page_index, page in enumerate(lesson["stateLayout"]["current"])
        for block_index, block_id in enumerate(page["blocks"])
        if block_id == OWNER
    ]
    assert len(locations) == 1, "U8L5 original compiler owner missing or duplicated"
    page, block = locations[0]
    editor = lesson["pages"][page]["blocks"][block]
    assert editor["type"] == "cpp_compiler"
    assert len(editor["outputCheck"]["cases"]) >= 3
    source = editor["initialCode"]
    assert "virtual ~Medida() noexcept;" in source
    assert "~MedidaIdentificada() noexcept override;" in source
    assert "std::unique_ptr<Ficha> ficha_;" in source
    return source


def body_span(source: str, signature: str, end_marker: str) -> tuple[int, int]:
    assert source.count(signature) == 1, f"missing or duplicated {signature}"
    assert signature.endswith("{")
    # A later source-format review may separate the following definition by
    # blank lines. Bind the original body itself, preserving every outer byte.
    start, close = layout._named_rule_body_span(source, signature[:-1].rstrip())
    assert source[start - 1] == "{" and source[close] == "}"
    return start, close


def compose(original: str, base: str, derived: str, factory: str) -> str:
    changes = [
        (*body_span(original, *BASE), base),
        (*body_span(original, *DERIVED), derived),
        (*body_span(original, *FACTORY), factory),
    ]
    assert changes[0][1] < changes[1][0] < changes[1][1] < changes[2][0]
    result = original
    for begin, end, body in reversed(changes):
        result = result[:begin] + body + result[end:]
    return result


def run(command: list[str], timeout: int = 70) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, check=False, text=True,
                          encoding="utf-8", errors="replace", capture_output=True,
                          timeout=timeout)


def compile_cpp(compiler: str, source: Path, binary: Path, windows: bool):
    if windows:
        command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4",
                   "/WX", "/permissive-", "/Zc:__cplusplus", "/utf-8",
                   str(source), "/Fe:" + str(binary)]
    else:
        command = [compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror",
                   "-pedantic", str(source), "-o", str(binary)]
    return run(command)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--windows", action="store_true")
    arguments = parser.parse_args()
    original = original_editor()
    candidates = {
        "direct_derived_factory": (
            GOOD_BASE, GOOD_DERIVED, GOOD_FACTORY, True),
        "independent_move_factory": (
            GOOD_BASE_ALT, GOOD_DERIVED_ALT, GOOD_FACTORY_ALT, True),
        **{
            name: (body, GOOD_DERIVED, GOOD_FACTORY, False)
            for name, body in BAD_BASE.items()
        },
        **{
            name: (GOOD_BASE, body, GOOD_FACTORY, False)
            for name, body in BAD_DERIVED.items()
        },
        **{
            name: (GOOD_BASE, GOOD_DERIVED, body, False)
            for name, body in BAD_FACTORY.items()
        },
    }
    with tempfile.TemporaryDirectory(prefix="lgaed-u8l5-") as dirname:
        folder = Path(dirname)
        suffix = ".exe" if arguments.windows else ""
        for label, (base, derived, factory, expected) in candidates.items():
            source = folder / (label + ".cpp")
            source.write_text(
                "#include <cstdio>\n#define main lgaed_original_main\n" +
                compose(original, base, derived, factory) + DRIVER,
                encoding="utf-8",
            )
            binary = folder / (label + suffix)
            build = compile_cpp(arguments.compiler, source, binary, arguments.windows)
            if build.returncode:
                raise AssertionError(
                    (label, "C++ compilation failed", build.stdout, build.stderr))
            outcome = run([str(binary)], timeout=15)
            accepted = (outcome.returncode == 0 and outcome.stdout == "" and
                        outcome.stderr == MARKER)
            if accepted != expected:
                raise AssertionError(
                    (label, "unexpected destruction semantics", outcome.returncode,
                     outcome.stdout, outcome.stderr))
            print(f"{arguments.compiler}: {label}: "
                  f"{'PASS' if accepted else 'REJECT'}", flush=True)
    print("PASS U8L5 source-bound independent C++ regression: "
          "2 valid bodies and 12 incorrect counterexamples; "
          "concrete type, base-owner move, destruction of member/base/derived, "
          "independent records, reset and exception cleanup verified.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL U8L5 independent semantic regression: {error}", file=sys.stderr)
        sys.exit(1)

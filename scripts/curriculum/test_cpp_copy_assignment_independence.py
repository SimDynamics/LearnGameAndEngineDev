"""Independent compiler QA for the published U6L7 copy assignment task.

Only the original operator= body changes. No curriculum content, owner,
historical answer, or student main is modified. A private driver observes
resource identity and exception safety without exhausting process memory.
This is a verification prerequisite, NOT an integrated app validator.
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
    "lesson_definir_copia_independiente_y_conservar_el_destino.json"
)
OWNER = "block_0014_0002"
MARKER = "LGAED_COPY_ASSIGNMENT_INDEPENDENT_V1"

# Different equivalent solutions must pass the same semantic tests.
VARIANTS = {
    "candidate_swap": r"""
    if (this == &otra) return *this;
    std::unique_ptr<int> candidato;
    if (otra.dato_) candidato = std::make_unique<int>(*otra.dato_);
    dato_.swap(candidato);
    return *this;
""",
    "copy_constructor_swap": r"""
    if (this == &otra) {
        return *this;
    }
    Celda candidata(otra);
    dato_.swap(candidata.dato_);
    return *this;
""",
}
REJECTIONS = {
    "overwriting_destination": r"""
    if (this == &otra) return *this;
    if (!otra.dato_) { dato_.reset(); return *this; }
    if (!dato_) dato_ = std::make_unique<int>(*otra.dato_);
    else *dato_ = *otra.dato_;
    return *this;
""",
    "replaces_self": r"""
    Celda candidata(otra);
    dato_.swap(candidata.dato_);
    return *this;
""",
    "loses_destination_on_failure": r"""
    if (this == &otra) return *this;
    dato_.reset();
    if (otra.dato_) dato_ = std::make_unique<int>(*otra.dato_);
    return *this;
""",
    "ignores_empty_source": r"""
    if (this == &otra) return *this;
    if (otra.dato_) {
        auto candidata = std::make_unique<int>(*otra.dato_);
        dato_.swap(candidata);
    }
    return *this;
""",
    "wrong_return_object": r"""
    if (this == &otra) return *this;
    auto candidata = otra.dato_
        ? std::make_unique<int>(*otra.dato_) : std::unique_ptr<int>{};
    dato_.swap(candidata);
    return const_cast<Celda&>(otra);
""",
}

PRELUDE = r"""#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace lgaed_copy_qa {
struct Watch {
    inline static bool active = false;
    inline static bool fail_next = false;
    inline static unsigned created = 0;
    inline static unsigned deleted = 0;
    inline static const void* expected_deleted = nullptr;
    inline static bool saw_expected_deleted = false;

    static void expect_deletion(const void* address) {
        expected_deleted = address;
        saw_expected_deleted = false;
    }
    static void deleting(const void* address) noexcept {
        if (!active || !address) return;
        ++deleted;
        if (address == expected_deleted) {
            saw_expected_deleted = true;
            expected_deleted = nullptr;
        }
    }
};
}
void* operator new(std::size_t bytes) {
    if (lgaed_copy_qa::Watch::fail_next) {
        lgaed_copy_qa::Watch::fail_next = false;
        throw std::bad_alloc{};
    }
    if (void* address = std::malloc(bytes ? bytes : 1)) {
        if (lgaed_copy_qa::Watch::active) ++lgaed_copy_qa::Watch::created;
        return address;
    }
    throw std::bad_alloc{};
}
void operator delete(void* address) noexcept {
    lgaed_copy_qa::Watch::deleting(address);
    std::free(address);
}
void operator delete(void* address, std::size_t) noexcept {
    ::operator delete(address);
}
void* operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}
void operator delete[](void* address) noexcept {
    ::operator delete(address);
}
void operator delete[](void* address, std::size_t) noexcept {
    ::operator delete(address);
}
#define main lgaed_original_student_main
"""

DRIVER = r"""
#undef main
int main() {
    using lgaed_copy_qa::Watch;
    Watch::active = true;
    {
        Celda source;
        Celda destination;
        if (!source.cambiar(8) || !destination.cambiar(2) ||
            Watch::created != 2) return 1;
        const int* source_address = source.acceso();
        Watch::expect_deletion(destination.acceso());
        const auto made = Watch::created;
        const auto freed = Watch::deleted;
        Celda& returned = (destination = source);
        if (&returned != &destination || source.acceso() != source_address ||
            destination.acceso() == source_address ||
            !destination.hay() || destination.valor() != 8 ||
            source.valor() != 8 ||
            Watch::created != made + 1 || Watch::deleted != freed + 1 ||
            !Watch::saw_expected_deleted) return 2;

        // Values equal in both objects still require a fresh independent int.
        Watch::expect_deletion(destination.acceso());
        const auto made_equal = Watch::created;
        const auto freed_equal = Watch::deleted;
        destination = source;
        if (source.acceso() != source_address ||
            destination.acceso() == source_address ||
            Watch::created != made_equal + 1 ||
            Watch::deleted != freed_equal + 1 ||
            !Watch::saw_expected_deleted) return 3;

        // An empty source destroys the previous target, without allocating.
        if (!source.vaciar() || source.hay()) return 4;
        Watch::expect_deletion(destination.acceso());
        const auto made_empty = Watch::created;
        const auto freed_empty = Watch::deleted;
        Celda& from_empty = (destination = source);
        if (&from_empty != &destination || destination.hay() || source.hay() ||
            Watch::created != made_empty || Watch::deleted != freed_empty + 1 ||
            !Watch::saw_expected_deleted) return 5;

        // Self-assignment on both present and absent owners is identity.
        Celda self;
        if (!self.poner(0)) return 6;
        const int* self_address = self.acceso();
        Celda& alias = self;
        const auto made_self = Watch::created;
        const auto freed_self = Watch::deleted;
        Celda& self_return = (self = alias);
        if (&self_return != &self || self.acceso() != self_address ||
            !self.hay() || self.valor() != 0 ||
            Watch::created != made_self || Watch::deleted != freed_self)
            return 7;
        self.vaciar();
        const auto made_no_value = Watch::created;
        const auto freed_no_value = Watch::deleted;
        Celda& empty_alias = self;
        Celda& empty_return = (self = empty_alias);
        if (&empty_return != &self || self.hay() ||
            Watch::created != made_no_value || Watch::deleted != freed_no_value)
            return 8;

        // A deterministic failed reservation preserves both live identities.
        Celda occupied;
        if (!source.poner(9) || !occupied.cambiar(3)) return 9;
        const int* source_before = source.acceso();
        const int* target_before = occupied.acceso();
        const auto made_failure = Watch::created;
        const auto freed_failure = Watch::deleted;
        Watch::fail_next = true;
        bool caught = false;
        try {
            occupied = source;
        } catch (const std::bad_alloc&) {
            caught = true;
        }
        Watch::fail_next = false;
        if (!caught || source.acceso() != source_before ||
            occupied.acceso() != target_before ||
            source.valor() != 9 || occupied.valor() != 3 ||
            Watch::created != made_failure || Watch::deleted != freed_failure)
            return 10;

        // After a move, the emptied source can own a fresh independent copy.
        Celda moved(std::move(source));
        if (source.hay() || moved.acceso() != source_before) return 11;
        const auto made_reuse = Watch::created;
        Celda& reuse_return = (source = moved);
        if (&reuse_return != &source || !source.hay() ||
            source.valor() != moved.valor() ||
            source.acceso() == moved.acceso() ||
            Watch::created != made_reuse + 1) return 12;
    }
    Watch::active = false;
    if (Watch::created != Watch::deleted || Watch::fail_next) return 13;
    std::fputs("LGAED_COPY_ASSIGNMENT_INDEPENDENT_V1", stderr);
    return 0;
}
"""


def run(command: list[str], *, seconds: int = 70) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, check=False, text=True,
                          encoding="utf-8", errors="replace",
                          capture_output=True, timeout=seconds)


def compile_source(compiler: str, source: Path, binary: Path,
                   windows: bool) -> subprocess.CompletedProcess[str]:
    if windows:
        command = [compiler, "/nologo", "/std:c++latest", "/EHsc",
                   "/W4", "/WX", "/permissive-", "/Zc:__cplusplus",
                   "/utf-8", str(source), "/Fe:" + str(binary)]
    else:
        command = [compiler, "-std=c++23", "-Wall", "-Wextra",
                   "-Werror", "-pedantic", str(source), "-o", str(binary)]
    return run(command)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--windows", action="store_true")
    options = parser.parse_args()

    data = json.loads(LESSON.read_text(encoding="utf-8"))
    matches = [(p, b) for p, page in enumerate(data["stateLayout"]["current"])
               for b, identity in enumerate(page["blocks"]) if identity == OWNER]
    assert len(matches) == 1, "The historical U6L7 editor identity changed"
    page, block_index = matches[0]
    block = data["pages"][page]["blocks"][block_index]
    assert block["type"] == "cpp_compiler"
    assert len(block["outputCheck"]["cases"]) == 3
    original = block["initialCode"]

    signature = "Celda& Celda::operator=(const Celda& otra) {"
    next_function = "\n}\nbool Celda::poner(int valor)"
    opening = original.index(signature) + len(signature)
    closing = original.index(next_function, opening) + 1
    prefix, suffix = original[:opening], original[closing:]
    assert original[closing] == "}" and original.count(signature) == 1
    assert "return *this;" in original[opening:closing]
    assert "std::unique_ptr<int> dato_ = std::make_unique<int>(4);" in prefix
    assert "#include <new>" in prefix and "int main()" in suffix

    with tempfile.TemporaryDirectory(prefix="lgaed-u6l7-copy-qa-") as folder:
        directory = Path(folder)
        binary_extension = ".exe" if options.windows else ""
        for label, body in [*VARIANTS.items(), *REJECTIONS.items()]:
            # The generated candidate differs from the published source only
            # inside the ONE method body. All other functions/main are sealed.
            student = prefix + body + suffix
            assert student.startswith(prefix) and student.endswith(suffix)
            generated = PRELUDE + student + DRIVER
            file = directory / (label + ".cpp")
            file.write_bytes(generated.encode("utf-8"))
            binary = directory / (label + binary_extension)
            compiled = compile_source(options.compiler, file, binary, options.windows)
            if compiled.returncode:
                raise RuntimeError(f"{label}: cannot compile independent probe\n" +
                                   compiled.stdout + compiled.stderr)
            result = run([str(binary)], seconds=15)
            passed = (result.returncode == 0 and result.stdout == "" and
                      result.stderr == MARKER)
            correct = label in VARIANTS
            if passed != correct:
                raise AssertionError(
                    (label, "unexpected semantic result", result.returncode,
                     result.stdout, result.stderr)
                )
            print(f"{options.compiler}: {label}: "
                  f"{'PASS' if correct else 'REJECT'}", flush=True)

    print("PASS U6L7 independently compiled original program: copy identity, "
          "equal values, empty/self copies, deterministic bad_alloc, "
          "strong preservation, move reuse, return reference and cleanup.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL U6L7 independent copy assignment: {error}", file=sys.stderr)
        sys.exit(1)

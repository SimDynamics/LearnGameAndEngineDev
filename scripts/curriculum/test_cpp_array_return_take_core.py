"""Compile the reusable original-owner U6L12 source gate with real C++23.

Both free-function bodies are independently editable; original class, main,
signatures, public responses and entire source prefix/suffix are protected.
The generated private driver checks ownership semantics, not string matching.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile

import test_cpp_array_return_take as qa


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    options=parser.parse_args()
    original=qa.original_block()["initialCode"]
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l12-core-") as directory:
        root=Path(directory)
        exe=".exe" if options.windows else ""
        generator=root/("return-take-generator"+exe)
        built=qa.compile_cpp(options.compiler,
            qa.ROOT/"native/core/cpp_array_return_take_check_tests.cpp",
            generator,options.windows)
        if built.returncode:
            raise AssertionError(("Original-owner C++ source gate failed to compile",
                                  built.stdout,built.stderr))
        published=root/"published-u6l12.cpp"
        published.write_bytes(original.encode("utf-8"))
        accepted={
            "copy_then_take":(qa.COPY["copy_then_edit"],qa.TAKE["check_then_assign"]),
            "public_api_and_swap":(
                qa.COPY["prepare_using_public_api"],
                qa.TAKE["check_then_swap_with_retired"]),
            "equivalent_spacing":(
                qa.COPY["copy_then_edit"].replace(
                    "copia.escribir(indice, valor);",
                    "copia . escribir(indice,valor); // The copy owns its own array"),
                qa.TAKE["check_then_assign"].replace(
                    "destino = std::move(origen);",
                    "destino = std::move (origen);")),
        }
        invalid={
            **{name:(body,qa.TAKE["check_then_assign"])
               for name,body in qa.BAD_COPY.items()},
            **{name:(qa.COPY["copy_then_edit"],body)
               for name,body in qa.BAD_TAKE.items()},
        }
        for name,(copy,take) in [*accepted.items(),*invalid.items()]:
            candidate=root/(name+"-student.cpp")
            candidate.write_bytes(qa.compose(original,copy,take).encode("utf-8"))
            generated=root/(name+"-private.cpp")
            gated=qa.run([str(generator),str(published),str(candidate),
                          str(generated)])
            if gated.returncode or not generated.exists():
                raise AssertionError((name,"A legal two-body edit was rejected",
                                      gated.stdout,gated.stderr))
            binary=root/(name+"-runtime"+exe)
            compiled=qa.compile_cpp(options.compiler,generated,binary,options.windows)
            if compiled.returncode:
                raise AssertionError((name,"Private generated driver failed to compile",
                                      compiled.stdout,compiled.stderr))
            result=qa.run([str(binary)],timeout=18)
            passed=result.returncode==0 and result.stdout=="" and result.stderr==qa.MARKER
            if passed!=(name in accepted):
                raise AssertionError((name,"Wrong independent ownership decision",
                                      result.returncode,result.stdout,result.stderr))
            print(f"{options.compiler}: native {name}: "
                  f"{'PASS' if passed else 'REJECT'}",flush=True)
        correct=qa.compose(original,qa.COPY["copy_then_edit"],
                           qa.TAKE["check_then_assign"])
        protected_changes={
            "changed_class":correct.replace("class Tramo {","class Otro {",1),
            "changed_array_member":correct.replace(
                "std::unique_ptr<int[]> datos_{};",
                "std::unique_ptr<int[]> new_owner_{};",1),
            "changed_count_field":correct.replace(
                "int cantidad_ = 0;","int cantidad_ = 1;",1),
            "changed_copy_constructor":correct.replace(
                "Tramo::Tramo(const Tramo& otra) : datos_{}, cantidad_{0} {",
                "Tramo::Tramo(const Tramo& otra) : datos_{}, cantidad_{1} {",1),
            "changed_move_assignment":correct.replace(
                "otra.cantidad_ = 0;", "otra.cantidad_ = 1;",1),
            "changed_main_output":correct.replace(
                'std::cout << "Antes:A=";', 'std::cout << "Fingido:A=";',1),
            "changed_copy_signature":correct.replace(
                "Tramo copia_modificada(const Tramo& origen, int indice, int valor) {",
                "Tramo copia_modificada(Tramo& origen, int indice, int valor) {",1),
            "changed_take_noexcept":correct.replace(
                "bool tomar_si_cabe(Tramo& destino, Tramo&& origen, int limite) noexcept {",
                "bool tomar_si_cabe(Tramo& destino, Tramo&& origen, int limite) {",1),
            "hidden_preprocessor_copy":qa.compose(original,
                "\n#define origen destino\n"+qa.COPY["copy_then_edit"],
                qa.TAKE["check_then_assign"]),
            "hidden_preprocessor_take":qa.compose(original,
                qa.COPY["copy_then_edit"],
                "\n#define limite 4\n"+qa.TAKE["check_then_assign"]),
            "early_closing_brace":qa.compose(original,
                "\n}\n"+qa.COPY["copy_then_edit"],qa.TAKE["check_then_assign"]),
            "rewrite_unrelated_method":correct.replace(
                "int Tramo::sumar() const noexcept {",
                "int Tramo::sumar() const {",1),
        }
        for name,modified in protected_changes.items():
            assert modified!=correct,name
            candidate=root/(name+"-student.cpp")
            candidate.write_bytes(modified.encode("utf-8"))
            output=root/(name+"-private.cpp")
            gated=qa.run([str(generator),str(published),str(candidate),
                          str(output)])
            if gated.returncode==0 or output.exists():
                raise AssertionError((name,"Modified protected source approved",
                                      gated.stdout,gated.stderr))
            print(f"{options.compiler}: protected {name}: REJECT",flush=True)
    print("PASS U6L12 native source gate: 3 valid authoring variants, "
          "16 defective cases, 12 protected source changes rejected, "
          "private Tramo identity and exception witness executed.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U6L12 original-owner source gate: {error}",file=sys.stderr)
        sys.exit(1)

"""Verify genuine U6L11 source preservation and separately compiled identity.

The source gate accepts two distinct correct implementations and equivalent
body formatting, while rejecting changed constructors, signatures and main.
Every private driver is bound to the exact original published Tramo class.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile

import test_cpp_array_copy_move as qa


def main() -> None:
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    options=parser.parse_args()
    original=qa.original_block()["initialCode"]
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l11-native-") as dirname:
        directory=Path(dirname)
        exe=".exe" if options.windows else ""
        generator=directory/("array-copy-move-generator"+exe)
        built=qa.compile_cpp(options.compiler,
            qa.ROOT/"native/core/cpp_array_copy_move_check_tests.cpp",
            generator,options.windows)
        if built.returncode:
            raise AssertionError(("C++ original-source gate did not compile",
                                  built.stdout,built.stderr))
        original_file=directory/"published-u6l11.cpp"
        original_file.write_bytes(original.encode("utf-8"))

        allowed={
            "swap_and_move":(qa.COPY["candidate_swap"],qa.MOVE["move_and_exchange"]),
            "ctor_and_swap":(qa.COPY["copy_constructor_exchange"],
                             qa.MOVE["empty_swap_then_transfer"]),
            "equivalent_format":(
                qa.COPY["candidate_swap"].replace(
                    "datos_.swap(nueva);",
                    "datos_ . swap ( nueva ); // Free the old destination later"),
                qa.MOVE["move_and_exchange"].replace(
                    "cantidad_ = std::exchange(otra.cantidad_, 0);",
                    "cantidad_ = std::exchange (otra.cantidad_, 0);")),
            "quoted_probe_names":(
                '\n    // __FILE__, Watch, std::exit(0) are comments only.\n'
                '    const char* info=R"memo(lgaed_array_copy_move_qa::Watch '
                '__COUNTER__ std::printf())memo";\n    (void)info;\n' +
                qa.COPY["candidate_swap"],
                qa.MOVE["move_and_exchange"]),
        }
        faulty={
            **{name:(bad,qa.MOVE["move_and_exchange"])
               for name,bad in qa.BAD_COPY.items()},
            **{name:(qa.COPY["candidate_swap"],bad)
               for name,bad in qa.BAD_MOVE.items()},
        }
        for name,(copy,move) in [*allowed.items(),*faulty.items()]:
            candidate=directory/(name+"-student.cpp")
            candidate.write_bytes(qa.compose(original,copy,move).encode("utf-8"))
            private=directory/(name+"-private.cpp")
            gate=qa.run([str(generator),str(original_file),str(candidate),
                         str(private)])
            if gate.returncode or not private.is_file():
                raise AssertionError((name,"valid method-body edit rejected",
                                      gate.stdout,gate.stderr))
            result_binary=directory/(name+"-private"+exe)
            compilation=qa.compile_cpp(options.compiler,private,
                                       result_binary,options.windows)
            if compilation.returncode:
                raise AssertionError((name,"independent C++ driver did not compile",
                                      compilation.stdout,compilation.stderr))
            observed=qa.run([str(result_binary)],timeout=18)
            valid=observed.returncode==0 and observed.stdout=="" and (
                observed.stderr==qa.MARKER)
            if valid!=(name in allowed):
                raise AssertionError((name,"wrong private ownership decision",
                                      observed.returncode,observed.stdout,
                                      observed.stderr))
            print(f"{options.compiler}: native {name}: "
                  f"{'PASS' if valid else 'REJECT'}",flush=True)

        correct=qa.compose(original,qa.COPY["candidate_swap"],
                           qa.MOVE["move_and_exchange"])
        changed_sources={
            "changed_original_owner":correct.replace(
                "std::unique_ptr<int[]> datos_{};",
                "std::unique_ptr<int[]> coleccion_{};",1),
            "changed_count_initialization":correct.replace(
                "int cantidad_ = 0;", "int cantidad_ = 1;",1),
            "rewrote_copy_constructor":correct.replace(
                "Tramo::Tramo(const Tramo& otra) : datos_{}, cantidad_{0} {",
                "Tramo::Tramo(const Tramo& otra) : datos_{}, cantidad_{1} {",1),
            "rewrote_move_constructor":correct.replace(
                "Tramo::Tramo(Tramo&& otra) noexcept",
                "Tramo::Tramo(Tramo&& otra)",1),
            "rewrote_swap":correct.replace(
                "std::swap(cantidad_, otra.cantidad_);",
                "cantidad_ = otra.cantidad_;",1),
            "changed_main_stdout":correct.replace(
                'std::cout << "Antes:A=";', 'std::cout << "Inventado:";',1),
            "changed_copy_signature":correct.replace(
                "Tramo& Tramo::operator=(const Tramo& otra) {",
                "Tramo& Tramo::operator=(Tramo& otra) {",1),
            "changed_move_noexcept":correct.replace(
                "Tramo& Tramo::operator=(Tramo&& otra) noexcept {",
                "Tramo& Tramo::operator=(Tramo&& otra) {",1),
            "preprocessor_in_copy":qa.compose(
                original,"\n#define datos_ libre\n"+qa.COPY["candidate_swap"],
                qa.MOVE["move_and_exchange"]),
            "preprocessor_in_move":qa.compose(
                original,qa.COPY["candidate_swap"],
                "\n#define cantidad_ 0\n"+qa.MOVE["move_and_exchange"]),
            "early_closing_brace":qa.compose(
                original,"\n}\n"+qa.COPY["candidate_swap"],
                qa.MOVE["move_and_exchange"]),
        }
        # No malicious candidate is executed. Both operator bodies must
        # preserve the private driver's allocation and completion evidence.
        attacks={
            "exit": "std::exit(0);",
            "exit_alias": "auto stop=&std::exit; stop(0);",
            "fake_marker": 'std::fputs("'+qa.MARKER+'",stderr);',
            "tamper_watch": "lgaed_array_copy_move_qa::Watch::array_allocs=0;",
            "position": "const auto name=__FILE__; (void)name;",
            "count": "const auto n=__COUNTER__; (void)n;",
            "redirect": 'std::freopen("a.txt","w",stderr);',
            "jump": "std::longjmp(context,1);",
            "macro_digraph": "%:define OVERWRITE 1",
        }
        for key,body in attacks.items():
            changed_sources["copy_"+key]=qa.compose(
                original,"\n    "+body+"\n"+qa.COPY["candidate_swap"],
                qa.MOVE["move_and_exchange"])
            changed_sources["move_"+key]=qa.compose(
                original,qa.COPY["candidate_swap"],
                "\n    "+body+"\n"+qa.MOVE["move_and_exchange"])
        for name,modified in changed_sources.items():
            assert modified!=correct,name
            file=directory/(name+"-student.cpp")
            file.write_bytes(modified.encode("utf-8"))
            private=directory/(name+"-private.cpp")
            result=qa.run([str(generator),str(original_file),str(file),
                           str(private)])
            if result.returncode==0 or private.exists():
                raise AssertionError((name,"untrusted source modification accepted",
                                      result.stdout,result.stderr))
            print(f"{options.compiler}: protected {name}: REJECT",flush=True)
    print("PASS U6L11: three valid authoring variants, thirteen faulty copies "
          "and moves rejected; eleven protected edits rejected, private "
          "identity and cleanup checks compiled and executed.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U6L11 native copy/move: {error}",file=sys.stderr)
        sys.exit(1)

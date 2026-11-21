"""Verify the reusable native U6L10 array owner/read source contract.

Runs the *real* core C++ gate over the original lesson and independently
compiles each generated private program. The sentinel window physically pads
both invalid logical indices. This is QA, not a student progress mutation.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import test_cpp_array_owner_read as qa


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--compiler",required=True)
    p.add_argument("--windows",action="store_true")
    options=p.parse_args()
    original=qa.initial_code()
    lesson=json.loads(qa.LESSON.read_text(encoding="utf-8"))
    identities=[(p,b) for p,page in enumerate(lesson["stateLayout"]["current"])
                for b,identity in enumerate(page["blocks"])
                if identity==qa.OWNER]
    if len(identities)!=1:
        raise AssertionError("U6L10 authentic editor owner is not unique")
    p,b=identities[0]
    block=lesson["pages"][p]["blocks"][b]
    if block["initialCode"]!=original:
        raise AssertionError("U6L10 original C++ source changed between reads")
    cases=block["outputCheck"]["cases"]
    if len(cases)!=3:
        raise AssertionError("U6L10 original public cases changed")
    for number,case in enumerate(cases,1):
        if (not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or
            not case["outputs"] or
            not all(isinstance(x,str) for x in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise AssertionError(f"Invalid U6L10 public case {number}")
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l10-array-core-") as name:
        root=Path(name)
        suffix=".exe" if options.windows else ""
        generator=root/("array-owner-generator"+suffix)
        build=qa.compile_cpp(options.compiler,
            qa.ROOT/"native/core/cpp_array_owner_read_check_tests.cpp",
            generator,options.windows)
        if build.returncode:
            raise AssertionError("Real native C++ source gate failed to compile:\n"+
                                 build.stdout+build.stderr)
        original_file=root/"published-source.cpp"
        original_file.write_bytes(original.encode("utf-8"))

        allowed={
            "swap_and_bounds":(
                qa.CONFIGURE["allocate_then_swap"],qa.READ["bounds_first"]),
            "new_and_range":(
                qa.CONFIGURE["allocate_then_assign"],qa.READ["explicit_valid_range"]),
            "equivalent_whitespace":(
                qa.CONFIGURE["allocate_then_swap"].replace(
                    "datos_.swap(candidato);",
                    "datos_.swap(candidato); // The old array is cleaned by RAII"),
                qa.READ["bounds_first"].replace(
                    "resultado = datos_[indice];",
                    "resultado=datos_[indice];")),
            "quoted_probe_names":(
                '\n    // __FILE__, Watch, std::exit(0) are documentation only.\n'
                '    const char* info=R"memo(lgaed_array_qa::Watch __COUNTER__ '
                'std::fputs())memo";\n    (void)info;\n' +
                qa.CONFIGURE["allocate_then_swap"],
                qa.READ["bounds_first"]),
        }
        deficient={
            **{name:(body,qa.READ["bounds_first"])
               for name,body in qa.BAD_CONFIGURE.items()},
            **{name:(qa.CONFIGURE["allocate_then_swap"],body)
               for name,body in qa.BAD_READ.items()},
        }
        public_variants=("swap_and_bounds","new_and_range","reject_changes_output")
        public_runs=0
        for label,(configure,read) in [*allowed.items(),*deficient.items()]:
            candidate=qa.compose(original,configure,read)
            candidate_file=root/(label+"-student.cpp")
            candidate_file.write_bytes(candidate.encode("utf-8"))
            private_file=root/(label+"-private.cpp")
            result=qa.run([str(generator),str(original_file),
                           str(candidate_file),str(private_file)])
            if result.returncode or not private_file.exists():
                raise AssertionError((label,"rejected legitimate body-only edit",
                                      result.stdout,result.stderr))
            executable=root/(label+"-private"+suffix)
            compiled=qa.compile_cpp(options.compiler,private_file,
                                    executable,options.windows)
            if compiled.returncode:
                raise AssertionError((label,"generated driver did not compile",
                                      compiled.stdout,compiled.stderr))
            executed=qa.run([str(executable)],timeout=16)
            valid=executed.returncode==0 and executed.stdout=="" and (
                executed.stderr==qa.MARKER)
            if valid!=(label in allowed):
                raise AssertionError((label,"incorrect native semantic verdict",
                                      executed.returncode,
                                      executed.stdout,executed.stderr))
            print(f"{options.compiler}: native {label}: "
                  f"{'PASS' if valid else 'REJECT'}",flush=True)
            # The public main initializes its output argument to -1.
            # A defective leer() may overwrite a different caller's sentinel
            # when rejecting, yet match ALL original visible stdout cases.
            # The separate private padded-window probe catches that defect.
            # Later cases are automatic QA only, not required student inputs.
            if label in public_variants:
                public_binary=root/(label+"-public"+suffix)
                compiled_public=qa.compile_cpp(
                    options.compiler,candidate_file,public_binary,options.windows)
                if compiled_public.returncode:
                    raise AssertionError(
                        (label,"published C++ main no longer compiles",
                         compiled_public.stdout,compiled_public.stderr))
                for number,case in enumerate(cases,1):
                    output=subprocess.run(
                        [str(public_binary)],cwd=qa.ROOT,
                        input=case["standardInput"],text=True,
                        encoding="utf-8",errors="replace",
                        capture_output=True,check=False,timeout=15)
                    if (output.returncode!=case.get("exitCode",0) or
                        output.stdout not in case["outputs"] or output.stderr):
                        raise AssertionError(
                            (label,number,"authored public result changed",
                             output.returncode,output.stdout[-500:],
                             output.stderr[-500:]))
                    public_runs+=1
                print(f"{options.compiler}: public {label}: "
                      f"{len(cases)} cases PASS; "
                      f"private semantics {'PASS' if valid else 'REJECT'}",
                      flush=True)
        if public_runs!=len(public_variants)*len(cases):
            raise AssertionError("U6L10 genuine public QA lost a variant")

        correct=qa.compose(original,qa.CONFIGURE["allocate_then_swap"],
                           qa.READ["bounds_first"])
        bad_sources={
            "changed_class":correct.replace("class Lote {","class Otro {",1),
            "changed_owner":correct.replace(
                "std::unique_ptr<int[]> datos_{};",
                "std::unique_ptr<int[]> elementos_{};",1),
            "changed_count":correct.replace(
                "int cantidad_ = 0;","int cantidad_ = 1;",1),
            "changed_main":correct.replace(
                'std::cout << "Inicio:";', 'std::cout << "Fingido:";',1),
            "changed_write":correct.replace(
                "datos_[indice] = valor;","datos_[indice] = 8;",1),
            "changed_read_signature":correct.replace(
                "bool Lote::leer(int indice, int& resultado) const noexcept {",
                "bool Lote::leer(int indice, int& resultado) noexcept {",1),
            "changed_configure_signature":correct.replace(
                "bool Lote::configurar(int cantidad) noexcept {",
                "bool Lote::configurar(int cantidad) {",1),
            "hidden_directive":qa.compose(
                original,"\n#define cantidad_ 0\n"+
                qa.CONFIGURE["allocate_then_swap"],qa.READ["bounds_first"]),
            "unexpected_early_close":qa.compose(
                original,"\n}\n"+qa.CONFIGURE["allocate_then_swap"],
                qa.READ["bounds_first"]),
        }
        # Both editable methods must be neutral to independently
        # compiled evidence. Reject these sources before compiling or running.
        spoofing = {
            "exit": "std::exit(0);",
            "finish_alias": "auto stop=&std::exit; stop(0);",
            "tamper_watch": "lgaed_array_qa::Watch::arrays_new=0;",
            "disable_watch": "lgaed_array_qa::Watch::active=false;",
            "fake_marker": 'std::fputs("'+qa.MARKER+'",stderr);',
            "redirect_stderr": 'std::freopen("report.txt","w",stderr);',
            "line_observer": "const auto n=__FILE__; (void)n;",
            "build_counter": "const auto n=__COUNTER__; (void)n;",
            "jump": "std::longjmp(saved,1);",
            "macro_digraph": "%:define OVERWRITE 1",
        }
        for name,body in spoofing.items():
            bad_sources["configure_"+name] = qa.compose(
                original,"\n    "+body+"\n"+qa.CONFIGURE["allocate_then_swap"],
                qa.READ["bounds_first"])
            bad_sources["read_"+name] = qa.compose(
                original,qa.CONFIGURE["allocate_then_swap"],
                "\n    "+body+"\n"+qa.READ["bounds_first"])
        for label,changed in bad_sources.items():
            assert changed!=correct,label
            path=root/(label+"-student.cpp")
            path.write_bytes(changed.encode("utf-8"))
            output=root/(label+"-private.cpp")
            checked=qa.run([str(generator),str(original_file),
                            str(path),str(output)])
            if checked.returncode==0 or output.exists():
                raise AssertionError((label,"protected source was approved",
                                      checked.stdout,checked.stderr))
            print(f"{options.compiler}: protected {label}: REJECT",flush=True)

    print("PASS U6L10 native source and semantic probe, including three "
          "authentic public cases for both valid variants and the invalid "
          "output-overwrite counterexample. Public stdout does not replace "
          "the independent padded read and array-allocation witness.")


if __name__=="__main__":
    try:main()
    except Exception as e:
        print(f"FAIL U6L10 native owner/read gate: {e}",file=sys.stderr)
        sys.exit(1)

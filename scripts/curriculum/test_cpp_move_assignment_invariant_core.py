"""Validate generic U6L8 source preservation and native ownership driver.

Compiles the actual published Muestra source with the student operator body,
never a second handcrafted class. The optional root metadata is not written
to the curriculum; historical answers and profiles are untouched.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile

import test_cpp_move_assignment_invariants as qa


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    options=parser.parse_args()
    original=qa.original_block()["initialCode"]
    sig="Muestra& Muestra::operator=(Muestra&& otra) {"
    first=original.index(sig)+len(sig)
    final=original.index("\n}\nbool Muestra::poner(int valor, int etiqueta)",first)+1
    prefix,suffix=original[:first],original[final:]
    assert suffix.startswith("}") and "int main()" in suffix
    with tempfile.TemporaryDirectory(prefix="lgaed-u6l8-core-") as directory:
        root=Path(directory)
        exe=".exe" if options.windows else ""
        generator=root/("move-invariant-generator"+exe)
        compiled=qa.compile_cpp(
            options.compiler,
            qa.ROOT/"native/core/cpp_move_assignment_invariant_check_tests.cpp",
            generator,options.windows)
        if compiled.returncode:
            raise AssertionError(
                "Cannot build generic U6L8 native source-check:\n"+
                compiled.stdout+compiled.stderr)
        original_path=root/"published-original.cpp"
        original_path.write_bytes(original.encode("utf-8"))
        positive={**qa.VALID,"comment_format_equivalent":
                  qa.VALID["exchange_label"].replace(
                      "dato_ = std::move(otra.dato_);",
                      "dato_   = std::move(otra.dato_); // Transfer exactly once"),
                  "quoted_probe_names": (
                      '    // Watch, __FILE__ and std::exit(0) are explanations.\n'
                      '    const char* note=R"memo(lgaed_move_invariant_qa::Watch '
                      '__COUNTER__ std::exit(0))memo";\n'
                      '    (void)note;\n' + qa.VALID["exchange_label"])}
        variants={**positive,**qa.REJECT}
        for name,body in variants.items():
            candidate=prefix+body+suffix
            student=root/(name+"-student.cpp")
            student.write_bytes(candidate.encode("utf-8"))
            driver=root/(name+"-private.cpp")
            result=qa.execute([str(generator),str(original_path),
                               str(student),str(driver)])
            if result.returncode or not driver.is_file():
                raise AssertionError((name,"rejected valid body-only edit",
                                      result.stdout,result.stderr))
            binary=root/(name+"-private"+exe)
            compiled=qa.compile_cpp(options.compiler,driver,binary,options.windows)
            if compiled.returncode:
                raise AssertionError((name,"generated driver failed to compile",
                                      compiled.stdout,compiled.stderr))
            run=qa.execute([str(binary)],timeout=14)
            good=run.returncode==0 and run.stdout=="" and run.stderr==qa.MARKER
            if good!=(name in positive):
                raise AssertionError((name,"unexpected private semantic verdict",
                                      run.returncode,run.stdout,run.stderr))
            print(f"{options.compiler}: native {name}: "
                  f"{'PASS' if good else 'REJECT'}",flush=True)
        original_correct=prefix+qa.VALID["exchange_label"]+suffix
        tampered={
            "altered_constructor":original_correct.replace(
                "otra.etiqueta_ = 0;", "otra.etiqueta_ = 1;",1),
            "altered_owner_type":original_correct.replace(
                "std::unique_ptr<int> dato_{}", "std::shared_ptr<int> dato_{}",1),
            "changed_tag_member":original_correct.replace(
                "int etiqueta_ = 0;", "int etiqueta_ = 1;",1),
            "changed_main":original_correct.replace(
                'std::cout << "Inicio:";', 'std::cout << "Fingido:";',1),
            "changed_assignment_signature":original_correct.replace(
                sig,"Muestra& Muestra::operator=(const Muestra& otra) {",1),
            "hidden_preprocessor":prefix+"\n#define etiqueta_ dato_\n"+
                qa.VALID["exchange_label"]+suffix,
            "early_closing_brace":prefix+"\n}\n"+
                qa.VALID["exchange_label"]+suffix,
        }
        # Do not execute malicious operations to prove the negative path.
        # The original published operator remains the only editable region,
        # and a private test witness must not be controlled by the student.
        hostile={
            "terminate_test": "std::exit(0);",
            "exit_via_alias": "auto stop=&std::exit; stop(0);",
            "quick_exit_test": "::quick_exit(0);",
            "forge_completion_output":
                'std::fputs("'+qa.MARKER+'",stderr);',
            "forge_stdout":
                'std::printf("'+qa.MARKER+'");',
            "mutate_private_counter":
                "lgaed_move_invariant_qa::Watch::created=0;",
            "disable_private_watch":
                "lgaed_move_invariant_qa::Watch::active=false;",
            "skip_allocation_failure":
                "lgaed_move_invariant_qa::Watch::fail_next=false;",
            "redirect_output":
                'std::freopen("trace.txt","w",stderr);',
            "observe_private_build":
                "const auto position=__FILE__; (void)position;",
            "observe_private_counter":
                "const auto counter=__COUNTER__; (void)counter;",
            "force_jump":
                "std::longjmp(saved,1);",
            "manual_free":
                "std::free(nullptr);",
            "preprocessor_digraph": "%:define OVERRIDE 1",
        }
        tampered.update({
            name:prefix+"\n    "+body+"\n"+qa.VALID["exchange_label"]+suffix
            for name,body in hostile.items()
        })
        for name,code in tampered.items():
            assert code!=original_correct,name
            candidate=root/(name+"-student.cpp")
            candidate.write_bytes(code.encode("utf-8"))
            driver=root/(name+"-private.cpp")
            tested=qa.execute([str(generator),str(original_path),
                               str(candidate),str(driver)])
            if tested.returncode==0 or driver.exists():
                raise AssertionError((name,"forged source passed protected owner",
                                      tested.stdout,tested.stderr))
            print(f"{options.compiler}: protected {name}: REJECT",flush=True)
    print("PASS U6L8 native class/method authoring: equivalent correct bodies, "
          "behavioral decoys, and protected private-witness/source changes; "
          "independent C++ ownership and invariant proof.")


if __name__=="__main__":
    try:main()
    except Exception as error:
        print(f"FAIL U6L8 generic move invariant: {error}",file=sys.stderr)
        sys.exit(1)

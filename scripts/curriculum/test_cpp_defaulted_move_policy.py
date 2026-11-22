"""Compile the actual U6L6 type policy, not a copied sample or student main.

Two equivalent defaulted declarations must compile, move the same unique_ptr
without allocation, release the previous destination and empty the intermedia.
Both forbidden copy operations must produce actual compiler rejections.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT / ("app/src/main/assets/content/es/programming/"
             "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
             "lesson_elegir_copia_y_movimiento_de_una_clase.json")
OWNER="block_0018_0002"
MARKER="LGAED_DEFAULTED_MOVE_POLICY_COMPLETE_V1"
EXPECTED=("correct_defaulted","correct_reordered_named_noexcept",
          "comment_before_class","raw_literal_before_class")


def execute(command,*,timeout=90):
    return subprocess.run(command,cwd=ROOT,text=True,errors="replace",
                          capture_output=True,check=False,timeout=timeout)


def compile_cpp(compiler,source,output,windows):
    if windows:
        args=[compiler,"/nologo","/std:c++latest","/EHsc","/W4","/WX",
              "/permissive-","/Zc:__cplusplus","/utf-8","/Inative",
              str(source),"/Fe:"+str(output)]
    else:
        args=[compiler,"-std=c++23","-Wall","-Wextra","-Werror",
              "-pedantic","-Inative",str(source),"-o",str(output)]
    return execute(args)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",required=True)
    parser.add_argument("--windows",action="store_true")
    options=parser.parse_args()
    lesson=json.loads(PATH.read_text(encoding="utf-8"))
    assert lesson["id"].endswith("__elegir_copia_y_movimiento_de_una_clase")
    positions=[(p,i) for p,page in enumerate(lesson["stateLayout"]["current"])
               for i,identity in enumerate(page["blocks"]) if identity==OWNER]
    assert len(positions)==1,"Current U6L6 owner has lost its original ID"
    p,i=positions[0]
    block=lesson["pages"][p]["blocks"][i]
    assert block["type"]=="cpp_compiler" and len(block["outputCheck"]["cases"])==3
    public_cases=block["outputCheck"]["cases"]
    for index,case in enumerate(public_cases,1):
        if (not isinstance(case,dict) or
            not isinstance(case.get("standardInput"),str) or
            not isinstance(case.get("outputs"),list) or
            not case["outputs"] or
            not all(isinstance(value,str) for value in case["outputs"]) or
            type(case.get("exitCode",0)) is not int):
            raise RuntimeError(f"Invalid original U6L6 public case {index}")
    original=block["initialCode"]
    assert "std::unique_ptr<int> dato_ = std::make_unique<int>(4);" in original
    assert "Ficha(const Ficha&) = delete;" in original
    assert "Ficha& operator=(const Ficha&) = delete;" in original
    with tempfile.TemporaryDirectory(prefix="lgaed-defaulted-move-") as directory:
        workspace=Path(directory)
        filename=workspace/"original-u6l6.cpp"
        # In Windows, write_text() may turn LF into CRLF. Preserve the
        # exact UTF-8 bytes owned by the published original, on every OS.
        filename.write_bytes(original.encode("utf-8"))
        exe=".exe" if options.windows else ""
        generator=workspace/("move-policy-generator"+exe)
        compiled=compile_cpp(options.compiler,
            ROOT/"native/core/cpp_defaulted_move_policy_check_tests.cpp",
            generator,options.windows)
        if compiled.returncode:
            raise RuntimeError("Cannot build independent move source-check:\n"+
                               compiled.stdout+compiled.stderr)
        generated=workspace/"private-probes"
        authoring=execute([str(generator),str(filename),str(generated)])
        if authoring.returncode:
            raise RuntimeError("U6L6 authoring check failed:\n"+
                               authoring.stdout+authoring.stderr)
        for name in EXPECTED:
            assert name+" PASS" in authoring.stdout,(name,authoring.stdout)
            positive=generated/(name+"-runtime.cpp")
            binary=workspace/(name+"-runtime"+exe)
            result=compile_cpp(options.compiler,positive,binary,options.windows)
            if result.returncode:
                raise RuntimeError(name+": private runtime cannot compile\n"+
                                   result.stdout+result.stderr)
            runtime=execute([str(binary)],timeout=15)
            assert runtime.returncode==0 and runtime.stdout=="" and (
                runtime.stderr==MARKER
            ),(name,"failed independent ownership semantics",
               runtime.returncode,runtime.stdout,runtime.stderr)
            print(f"{options.compiler}: {name} runtime: PASS",flush=True)
            for variant in ("copy-ctor","copy-assign"):
                negative=generated/(name+"-"+variant+".cpp")
                attempted=compile_cpp(options.compiler,negative,
                    workspace/(name+"-"+variant+exe),options.windows)
                diagnostics=attempted.stdout+"\n"+attempted.stderr
                if attempted.returncode==0 or not re.search(
                        r"(deleted|C2280|C2248)",diagnostics,re.IGNORECASE):
                    raise AssertionError((name,variant,
                        "An original forbidden copy was not specifically rejected",
                        attempted.returncode,diagnostics))
                print(f"{options.compiler}: {name} {variant}: REJECT",flush=True)
            # The independent owner witness must not hide a regression in
            # the unmodified original main. These three public cases run as
            # internal QA only: the learner still owes just the first case.
            public_source=generated/(name+"-public.cpp")
            if not public_source.is_file():
                raise RuntimeError(name+": missing source-preserved public QA")
            public_binary=workspace/(name+"-public"+exe)
            built=compile_cpp(options.compiler,public_source,public_binary,options.windows)
            if built.returncode:
                raise RuntimeError(name+": original main no longer compiles\n"+
                                   built.stdout+built.stderr)
            for index,case in enumerate(public_cases,1):
                run=subprocess.run(
                    [str(public_binary)],cwd=ROOT,input=case["standardInput"],
                    text=True,errors="replace",capture_output=True,
                    check=False,timeout=15)
                if (run.returncode!=case.get("exitCode",0) or
                    run.stdout not in case["outputs"] or run.stderr):
                    raise RuntimeError(
                        f"{name}: original public case {index} differs; "+
                        f"exit={run.returncode}, stdout={run.stdout[-500:]!r}, "+
                        f"stderr={run.stderr[-500:]!r}")
            print(f"{options.compiler}: {name} all "+
                  f"{len(public_cases)} optional/public cases: PASS",flush=True)
    print("PASS genuine U6L6 move policy: four valid = default variants, "
          "unchanged historical type/main, no allocation during moves, "
          "real forbidden-copy compiler diagnostics, identity and cleanup.")


if __name__=="__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL real defaulted move U6L6: {error}",file=sys.stderr)
        sys.exit(1)

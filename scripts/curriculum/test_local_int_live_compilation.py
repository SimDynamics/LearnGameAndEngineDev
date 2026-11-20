"""Compile and execute the core local-int probe's real generated C++ programs.

The emitter calls the production cpp_local_int_override implementation, then
this fixture runs the ORIGINAL and ALTERED programs on the host compiler. No
curricular JSON, user account, saved profile or application toolchain is edited.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def build_command(compiler: str, source: Path, binary: Path) -> list[str]:
    if compiler == "MSVC":
        return ["cl", "/nologo", "/std:c++latest", "/EHsc", "/utf-8", "/W4",
                "/Fe:" + str(binary), "/Fo:" + str(source.with_suffix(".obj")),
                str(source)]
    if compiler not in {"g++", "clang++"}:
        raise ValueError("Unsupported compiler for local-int integration")
    return [compiler, "-std=c++23", "-Wall", "-Wextra", "-pedantic",
            str(source), "-o", str(binary)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emitter", required=True, type=Path)
    parser.add_argument("--compiler", required=True, choices=["g++", "clang++", "MSVC"])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="lgaed-local-int-live-") as tmp:
        output = Path(tmp)
        emitted = subprocess.run([str(args.emitter.resolve()), str(output)],
                                 capture_output=True, timeout=30, check=True)
        if b"PASS generated 18" not in emitted.stdout:
            raise RuntimeError("The real source-mutation fixture did not finish")
        sources = sorted(output.glob("*.cpp"))
        if len(sources) != 18:
            raise RuntimeError(f"Expected exactly 18 source programs, got {len(sources)}")
        accepted = rejected = 0
        for source in sources:
            evidence = source.with_suffix(".expected").read_bytes()
            kind, separator, expected = evidence.partition(b"\n")
            if not separator or kind not in {b"A", b"R"}:
                raise RuntimeError(f"Missing or invalid expected stdout: {source.name}")
            binary = source.with_suffix(".exe" if sys.platform == "win32" else ".bin")
            command = build_command(args.compiler, source, binary)
            compile_run = subprocess.run(command, cwd=output, capture_output=True,
                                         timeout=90, check=False)
            if compile_run.returncode:
                raise RuntimeError(
                    f"{args.compiler} could not compile {source.name}:\n"
                    + compile_run.stdout.decode(errors="replace")[-4000:]
                    + compile_run.stderr.decode(errors="replace")[-4000:])
            observed = subprocess.run([str(binary)], cwd=output,
                                      capture_output=True, timeout=20, check=False)
            if observed.returncode or observed.stderr:
                raise RuntimeError(
                    f"Student program {source.name} terminated abnormally: "
                    f"exit={observed.returncode}, stderr={observed.stderr!r}")
            actual = observed.stdout.replace(b"\r\n", b"\n")
            should_match = kind == b"A"
            if (actual == expected) != should_match:
                raise RuntimeError(
                    f"{source.name}: {'accepted' if should_match else 'rejected'} "
                    f"local-int case did not match its real compiled behavior; "
                    f"actual={actual!r}, expected={expected!r}")
            if should_match:
                accepted += 1
            else:
                rejected += 1
        if (accepted, rejected) != (16, 2):
            raise RuntimeError(f"Incorrect execution counts: {(accepted, rejected)}")
        print(f"PASS {args.compiler} on {sys.platform}: {accepted} positive "
              f"original/probe executions, {rejected} actual hard-coded-copy "
              "counterexamples rejected; 18 real C++ compile-and-run cases.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.TimeoutExpired, subprocess.CalledProcessError,
            RuntimeError, ValueError) as error:
        print(f"FAIL local-int real compilation: {error}", file=sys.stderr)
        sys.exit(1)

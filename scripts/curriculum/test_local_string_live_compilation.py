"""Compile actual local-string C++23 probes with Windows MSVC only.

This runs only temporary, isolated QA sources. Neither students' drafts nor
the published curriculum is modified. The hosted MSVC run does not certify
the separately pinned full Windows application build.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

from windows_cpp_ci import msvc_cpp23_command


def build_command(compiler: str, source: Path, binary: Path) -> list[str]:
    if compiler != "MSVC":
        raise ValueError("Windows MSVC C++23 is required for local-string evidence")
    return msvc_cpp23_command(binary, source)


def check_program(source: Path, compiler: str) -> bool:
    payload = source.with_suffix(".expected").read_bytes()
    verdict, newline, expected = payload.partition(b"\n")
    if not newline or verdict not in {b"A", b"R"}:
        raise RuntimeError(f"Malformed source-specific expected stdout: {source.name}")
    binary = source.with_suffix(".exe")
    compiled = subprocess.run(build_command(compiler, source, binary),
                              cwd=source.parent, capture_output=True,
                              timeout=90, check=False)
    if compiled.returncode:
        raise RuntimeError(
            f"{compiler} did not compile {source.name}:\n"
            + compiled.stdout.decode(errors="replace")[-3000:]
            + compiled.stderr.decode(errors="replace")[-3000:])
    result = subprocess.run([str(binary)], cwd=source.parent,
                            capture_output=True, timeout=20, check=False)
    if result.returncode or result.stderr:
        raise RuntimeError(
            f"{source.name} did not finish normally: "
            f"exit={result.returncode}, stderr={result.stderr!r}")
    actual = result.stdout.replace(b"\r\n", b"\n")
    if (actual == expected) != (verdict == b"A"):
        raise RuntimeError(
            f"{source.name}: private probe did not expose the true text "
            f"dependency, expected={expected!r}, observed={actual!r}, "
            f"should_match={verdict == b'A'}")
    return verdict == b"A"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emitter", type=Path, required=True)
    parser.add_argument("--compiler", choices=("MSVC",),
                        required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="lgaed-local-string-live-") as folder:
        directory = Path(folder)
        generated = subprocess.run([str(args.emitter.resolve()), str(directory)],
                                   capture_output=True, timeout=30, check=True)
        if b"PASS generated 44" not in generated.stdout:
            raise RuntimeError("Private source fixture emitter did not finish correctly")
        sources = sorted(directory.glob("*.cpp"))
        if len(sources) != 44:
            raise RuntimeError(f"Expected 44 generated C++ programs, got {len(sources)}")
        positives = sum(check_program(source, args.compiler) for source in sources)
        if (positives, len(sources) - positives) != (40, 4):
            raise RuntimeError("Unexpected accepted/rejected C++ proof counts")
        print(f"PASS {args.compiler} on {sys.platform}: 40 real positive "
              "C++ source/variant executions, 4 hard-coded outputs rejected "
              "and one structurally unread etiqueta guarded by the emitter")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.TimeoutExpired, subprocess.CalledProcessError,
            RuntimeError, ValueError) as error:
        print(f"FAIL local string live compilation: {error}", file=sys.stderr)
        sys.exit(1)

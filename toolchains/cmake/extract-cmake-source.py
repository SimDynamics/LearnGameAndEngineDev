"""Extract the pinned CMake source, excluding disabled upstream test trees.

Windows file scanners make the 20,000 unused test fixture files expensive.
Keep top-level Tests configuration files that CMake itself includes. Existing
files permit an interrupted extraction to resume without rewriting them.
"""
import pathlib
import sys
import tarfile

archive, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]).resolve()
with tarfile.open(archive, "r:gz") as source:
    for member in source:
        parts = pathlib.PurePosixPath(member.name).parts
        if len(parts) > 3 and parts[1] == "Tests":
            continue
        destination = (output / member.name).resolve()
        if output not in destination.parents:
            raise ValueError(f"Archive entry escapes output: {member.name}")
        if destination.exists():
            continue
        source.extract(member, output, filter="data")
(output / "cmake-4.1.2/.lgp-source-extracted").write_text("4.1.2\n", encoding="utf-8")

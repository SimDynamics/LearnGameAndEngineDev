"""Execute authentic P3 independent C++ witnesses at one Codex revision.

The fourteen published lessons are read-only. Existing per-goal scripts
compile accepted variants and safe negative variants independently.
A test success never claims native application, audio, or pinned MSVC testing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CURRICULUM = ROOT / "scripts/curriculum"
EXAMPLE = ROOT / "docs/examples/p3_ownership_authoring.json"
PLAN = (
    ("U6L5", "compilerIndexedUniqueTransferChecks", ("test_cpp_indexed_unique_transfer_runtime.py",)),
    ("U6L6", "compilerDefaultedMovePolicyChecks", ("test_cpp_defaulted_move_policy.py",)),
    ("U6L7", "compilerIndependentCopyAssignmentChecks", ("test_cpp_copy_assignment_independence.py", "test_cpp_independent_copy_assignment_core.py")),
    ("U6L8", "compilerMoveAssignmentInvariantChecks", ("test_cpp_move_assignment_invariants.py", "test_cpp_move_assignment_invariant_core.py")),
    ("U6L10", "compilerArrayOwnerReadChecks", ("test_cpp_array_owner_read.py", "test_cpp_array_owner_read_core.py")),
    ("U6L11", "compilerArrayCopyMoveChecks", ("test_cpp_array_copy_move.py", "test_cpp_array_copy_move_core.py")),
    ("U6L12", "compilerArrayReturnTakeChecks", ("test_cpp_array_return_take.py", "test_cpp_array_return_take_core.py")),
    ("U8L5", "compilerPolymorphicDestructionChecks", ("test_cpp_polymorphic_destruction.py", "test_cpp_polymorphic_destruction_core.py")),
    ("U8L6", "compilerVirtualLifetimeDispatchChecks", ("test_cpp_virtual_lifetime_dispatch.py", "test_cpp_virtual_lifetime_dispatch_core.py")),
    ("U8L7", "compilerDerivedPreparationChecks", ("test_cpp_derived_preparation.py", "test_cpp_derived_preparation_core.py")),
    ("U8L8", "compilerAbstractInterfaceChecks", ("test_cpp_abstract_interface.py", "test_cpp_abstract_interface_core.py")),
    ("U8L9", "compilerInterfaceCollectionChecks", ("test_cpp_interface_collection.py",)),
    ("U8L10", "compilerPolymorphicCloneChecks", ("test_cpp_polymorphic_clone.py",)),
    ("U10L1", "compilerDualInterfaceChecks", ("test_cpp_dual_interface.py",)),
)
GROUPS = ("U6", "U8", "U10")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_reference() -> list[dict]:
    document = json.loads(EXAMPLE.read_text(encoding="utf-8"))
    if (document.get("schemaVersion") != 1
            or document.get("goal") !=
                "P3_ACREDITACION_TRANSFERENCIA_DE_PROPIEDAD"):
        raise RuntimeError("P3 reference is not the reviewed version")
    rows = document.get("entries")
    if (type(rows) is not list or len(rows) != len(PLAN)
            or [(item["unit"], item["rootField"]) for item in rows] !=
               [(unit, field) for unit, field, _ in PLAN]):
        raise RuntimeError("P3 reference omitted or reordered a real objective")
    identities = set()
    for item in rows:
        if type(item.get("declaration")) is not dict:
            raise RuntimeError("P3 source authorization is not an object")
        identity = item["declaration"].get("activity")
        relative = Path(item["lesson"])
        base = Path("app/src/main/assets/content/es/programming/cpp_language_memory_oop")
        if (type(identity) is not str or not identity.startswith("block_")
                or not relative.is_relative_to(base) or ".." in relative.parts
                or not (ROOT / relative).is_file()):
            raise RuntimeError("P3 reference does not bind a real original owner")
        unique = (str(relative), identity)
        if unique in identities:
            raise RuntimeError("Duplicate P3 original owner")
        identities.add(unique)
    for _, _, tests in PLAN:
        if not tests or not all((CURRICULUM / test).is_file() for test in tests):
            raise RuntimeError("A genuine P3 private C++ test script is missing")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", required=True, choices=GROUPS)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--windows", action="store_true")
    args = parser.parse_args()
    rows = validate_reference()
    originals = {
        item["lesson"]: sha256(ROOT / item["lesson"])
        for item in rows
    }
    subset = [
        (unit, tests)
        for unit, _, tests in PLAN
        if unit.startswith(args.group + "L")
    ]
    if not subset:
        raise RuntimeError("Selected P3 suite has no owners")
    compiler = args.compiler
    failures = []
    attempted = 0
    for unit, tests in subset:
        for test in tests:
            attempted += 1
            command = [
                sys.executable, "-B", str(CURRICULUM / test),
                "--compiler", compiler,
            ]
            if args.windows:
                command.append("--windows")
            try:
                result = subprocess.run(
                    command, cwd=ROOT, text=True, encoding="utf-8",
                    errors="replace", stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, timeout=300,
                )
                output = result.stdout
                if result.returncode != 0:
                    failures.append((unit, test, result.returncode))
                    print(f"FAIL P3 {unit} {test}: exit {result.returncode}\n"
                          f"{output[-5000:]}", flush=True)
                else:
                    print(f"PASS P3 {unit} {test} [{compiler}]\n"
                          f"{output[-600:]}", flush=True)
            except subprocess.TimeoutExpired:
                failures.append((unit, test, "timeout"))
                print(f"FAIL P3 {unit} {test}: independent test timed out",
                      flush=True)
    for path, expected in originals.items():
        if sha256(ROOT / path) != expected:
            failures.append(("source", path, "modified"))
            print(f"FAIL P3 read-only curriculum changed: {path}", flush=True)
    if failures:
        print(f"FAIL P3 group {args.group}: {len(failures)} failed of "
              f"{attempted} real independent C++ tests "
              f"with compiler {compiler}.", flush=True)
        return 1
    print(f"PASS P3 group {args.group}: {len(subset)} original owners, "
          f"{attempted} independent C++ test scripts, compiler {compiler}, "
          f"all fourteen source lessons unchanged.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

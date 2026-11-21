"""Read-only adoption test for the fourteen P3 ownership objectives.

This test only synthesizes opt-in root fields in memory. It never changes the
published lessons, student profiles, compiler runs, assets or pending registry.
Actual C++ private probes and native Windows receipt tests are separate.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import re
import unittest

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "docs/examples/p3_ownership_authoring.json"
CONTENT = ROOT / "app/src/main/assets/content/es/programming/cpp_language_memory_oop"
EXPECTED = (
    ("U6L5", "compilerIndexedUniqueTransferChecks"),
    ("U6L6", "compilerDefaultedMovePolicyChecks"),
    ("U6L7", "compilerIndependentCopyAssignmentChecks"),
    ("U6L8", "compilerMoveAssignmentInvariantChecks"),
    ("U6L10", "compilerArrayOwnerReadChecks"),
    ("U6L11", "compilerArrayCopyMoveChecks"),
    ("U6L12", "compilerArrayReturnTakeChecks"),
    ("U8L5", "compilerPolymorphicDestructionChecks"),
    ("U8L6", "compilerVirtualLifetimeDispatchChecks"),
    ("U8L7", "compilerDerivedPreparationChecks"),
    ("U8L8", "compilerAbstractInterfaceChecks"),
    ("U8L9", "compilerInterfaceCollectionChecks"),
    ("U8L10", "compilerPolymorphicCloneChecks"),
    ("U10L1", "compilerDualInterfaceChecks"),
)


def reference():
    data = json.loads(EXAMPLE.read_text(encoding="utf-8"))
    if (type(data) is not dict or set(data) !=
            {"schemaVersion", "goal", "note", "entries"} or
            data["schemaVersion"] != 1 or
            data["goal"] != "P3_ACREDITACION_TRANSFERENCIA_DE_PROPIEDAD" or
            type(data["entries"]) is not list):
        raise ValueError("Ownership reference has changed its declared layout")
    return data["entries"]


def source(entry):
    if type(entry) is not dict or set(entry) != {
            "unit", "lesson", "rootField", "declaration"}:
        raise ValueError("Ownership entry contains missing or alien fields")
    relative = Path(entry["lesson"])
    if (not relative.as_posix().startswith(
            CONTENT.relative_to(ROOT).as_posix() + "/") or
            ".." in relative.parts or not relative.is_relative_to(CONTENT.relative_to(ROOT))):
        raise ValueError("An ownership example targets a path outside P3")
    path = ROOT / relative
    if not path.is_file():
        raise ValueError("An ownership example lost its real lesson JSON")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def owner(document, identity):
    positions = [
        (pi, bi)
        for pi, page in enumerate(document["stateLayout"]["current"])
        for bi, item in enumerate(page["blocks"])
        if item == identity
    ]
    if len(positions) != 1:
        raise ValueError("The original ownership editor moved or was duplicated")
    pi, bi = positions[0]
    return document["pages"][pi]["blocks"][bi]


class P3OwnershipAuthoring(unittest.TestCase):
    def test_all_fourteen_declared_owners_are_real_and_state_preserving(self):
        records = reference()
        self.assertEqual([(x["unit"], x["rootField"]) for x in records],
                         list(EXPECTED))
        self.assertEqual(len({x["lesson"] for x in records}), len(EXPECTED))
        for entry in records:
            with self.subTest(unit=entry["unit"]):
                original = source(entry)
                field = entry["rootField"]
                self.assertIn(field, layout.FLOW_FIELDS)
                spec = entry["declaration"]
                self.assertIs(type(spec), dict)
                self.assertIs(type(spec.get("activity")), str)
                block = owner(original, spec["activity"])
                self.assertEqual(block["type"], "cpp_compiler")
                self.assertTrue(block["outputCheck"]["cases"])
                self.assertNotIn("projectFiles", block)
                self.assertNotIn("sourceFiles", block)

                authored = deepcopy(original)
                if field in authored:
                    # When Work eventually publishes this precise contract,
                    # keep verifying the same source and do not overwrite it.
                    self.assertEqual(authored[field], [spec])
                else:
                    authored[field] = [deepcopy(spec)]
                before_mapping = layout.validate(original)
                self.assertEqual(before_mapping, layout.validate(authored))
                layout.validate_transition(original, authored)
                self.assertEqual(authored["pages"], original["pages"])
                self.assertEqual(authored["stateLayout"], original["stateLayout"])
                before_archive = layout.original_projection(original)
                after_archive = layout.original_projection(authored)
                if field not in original:
                    after_archive.pop(field)
                self.assertEqual(before_archive, after_archive)
                print("PASS P3 " + entry["unit"] +
                      ": original owner, first output case, opt-in and history")

    def test_unknown_owners_and_unreviewed_fields_are_rejected_for_every_goal(self):
        for entry in reference():
            for defect in ("missing_owner", "alien_field"):
                with self.subTest(unit=entry["unit"], defect=defect):
                    authored = source(entry)
                    field = entry["rootField"]
                    spec = deepcopy(entry["declaration"])
                    if defect == "missing_owner":
                        spec["activity"] = "block_9999_9999"
                    else:
                        spec["hiddenAnswer"] = "unreviewed"
                    authored[field] = [spec]
                    with self.assertRaises((ValueError, KeyError, TypeError)):
                        layout.validate(authored)

    def test_authoring_does_not_permit_editing_an_existing_cpp_answer(self):
        first = reference()[0]
        before = source(first)
        after = deepcopy(before)
        after[first["rootField"]] = [deepcopy(first["declaration"])]
        target = owner(after, first["declaration"]["activity"])
        target["initialCode"] += "\n// QA-only attempted source change\n"
        with self.assertRaises((ValueError, KeyError, TypeError)):
            layout.validate_transition(before, after)


    def test_shared_windows_proof_is_registered_and_wired_for_all_owners(self):
        """Keep the shared 14-owner native regression executable in Windows QA."""
        cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        native=(ROOT/"native/platform/windows/"
                "p3_ownership_authoring_state_tests.cpp").read_text(
                    encoding="utf-8")
        target="lgaed_p3_ownership_authoring_state_tests"
        function="lgaed_add_p3_ownership_shared_state_target"
        self.assertIn(f"function({function})",cmake)
        self.assertEqual(cmake.count(f"{function}()"),3)
        self.assertEqual(cmake.count(f"add_executable({target}"),1)
        self.assertEqual(cmake.count(
            "native/platform/windows/p3_ownership_authoring_state_tests.cpp"),1)
        self.assertIn(
            f"target_link_libraries({target}\n"
            "        PRIVATE lgaed_platform_windows lgaed_host_options)",cmake)
        self.assertEqual(
            cmake.count("add_test(NAME native.p3_ownership_shared_state"),1)
        self.assertIn(f"COMMAND {target}",cmake)
        self.assertIn("PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE",cmake)

        # Read actual mappings, rather than guessing names. Catch a missing
        # runtime hook or a source owner that silently stops matching the
        # editorial manifest even if its isolated C++ proof still passes.
        plans=re.findall(
            r'\{"(U\d+L\d+)","([^"]+)","(compiler[A-Za-z]+Checks)"\}',
            native)
        declarations=re.findall(
            r'if\(field=="([^"]+)"\)return block\.([a-z_]+)\.has_value\(\);',
            native)
        entries=reference()
        self.assertEqual(len(plans),len(EXPECTED))
        self.assertEqual([(unit,field) for unit,_,field in plans],list(EXPECTED))
        self.assertEqual(
            [(CONTENT.relative_to(ROOT)/relative).as_posix() for _,relative,_ in plans],
            [item["lesson"] for item in entries])
        self.assertEqual(len(declarations),len(EXPECTED))
        self.assertEqual(set(dict(declarations)),{field for _,field in EXPECTED})
        paths={
            "lesson":ROOT/"native/core/lesson.hpp",
            "request":ROOT/"native/platform/windows/cpp_runner.hpp",
            "loader":ROOT/"native/platform/windows/content_loader.cpp",
            "runner":ROOT/"native/platform/windows/cpp_runner.cpp",
            "completion":ROOT/"native/platform/windows/compiler_completion.cpp",
        }
        sources={key:path.read_text(encoding="utf-8")
                 for key,path in paths.items()}
        for field,member in declarations:
            with self.subTest(field=field):
                self.assertIn(" "+member+";",sources["lesson"])
                self.assertIn(" "+member+";",sources["request"])
                self.assertIn("owner->"+member+"=",sources["loader"])
                self.assertIn("if(request."+member+")",sources["runner"])
                self.assertIn("if(block."+member+")",sources["completion"])


if __name__ == "__main__":
    unittest.main(verbosity=2)

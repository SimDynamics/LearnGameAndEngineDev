"""P1 U2L6: genuine code-only rename proposals and real Windows MSVC output.

Proposals remain in memory. Only Work publishes the educational content.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "curriculum"))

import code_format_compat as source_format
import identifier_rename_archive_preview as preview
import stable_state_layout as state

ASSET = ROOT / ("app/src/main/assets/content/es/programming/"
                "cpp_fundamentals/objetos_y_estado/"
                "lesson_conversiones_y_deduccion.json")
CASES = (
    ("block_0001_0002",
     {"a": "positivo_sin_fraccion", "b": "negativo_sin_fraccion"}, "2 -2"),
    ("block_0003_0004",
     {"tarde": "cociente_convertido",
      "antes": "cociente_con_operando_convertido"}, "3 3.25"),
)
PLANS = {identity: {"code": names} for identity, names, _ in CASES}


class PublishedU2L6Rename(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.published_bytes = ASSET.read_bytes()
        cls.before = json.loads(cls.published_bytes)
        if cls.before["id"] != "cpp_fundamentals__objetos_y_estado__conversiones_y_deduccion":
            raise AssertionError("The published U2L6 owner changed")
        state.validate(cls.before)
        cls.original = state._blocks(cls.before)

    @classmethod
    def tearDownClass(cls):
        if cls.published_bytes != ASSET.read_bytes():
            raise AssertionError("The published Work lesson was modified")

    def draft(self):
        return preview.prepare_declared_identifier_rename_draft(
            self.before, PLANS)

    def test_both_predictions_preserve_answers_histories_and_semantics(self):
        after = self.draft()
        report = preview.validate_read_only_rename_draft(self.before, after)
        self.assertTrue(report["runtimeLoadSupported"])
        self.assertTrue(report["publishedStateUnchanged"])
        state.validate_transition(self.before, after)
        self.assertEqual(state.validate(self.before), state.validate(after))
        self.assertEqual(
            state._blocks(state.align_for_previous_validation(self.before, after)),
            self.original)
        old_layout, new_layout = self.before["stateLayout"], after["stateLayout"]
        for key in ("origin", "current", "family", "additions", "retired",
                    "retirementHistory", "sourceFormatRevisions"):
            self.assertEqual(old_layout.get(key), new_layout.get(key))
        self.assertEqual(
            old_layout["editorial"],
            new_layout["editorial"][:-len(CASES)])
        self.assertEqual(self.before.get("textSemantics"),
                         after.get("textSemantics"))
        self.assertEqual(len(new_layout["identifierRenameRevisions"]), 2)
        changed = state._blocks(after)
        for identity, mapping, expected in CASES:
            with self.subTest(owner=identity):
                old, newer = self.original[identity], changed[identity]
                self.assertEqual(old["type"], "output_prediction")
                self.assertEqual(old["answers"], [expected])
                self.assertEqual(newer["answers"], [expected])
                self.assertNotEqual(old["code"], newer["code"])
                self.assertEqual(
                    {k:v for k,v in old.items() if k != "code"},
                    {k:v for k,v in newer.items() if k != "code"})
                with self.assertRaises(source_format.SourceFormatError):
                    source_format.require_source_lexical(old, newer)
                old_tokens = source_format._cpp_tokens(old["code"])
                self.assertEqual(
                    {name: old_tokens.count("id:"+name) for name in mapping},
                    dict.fromkeys(mapping, 2))
                updated_tokens = source_format._cpp_tokens(newer["code"])
                for new_name in mapping.values():
                    self.assertIn("id:"+new_name, updated_tokens)
                packet = next(item for item in new_layout["identifierRenameRevisions"]
                              if item["id"] == identity)
                self.assertEqual(packet["names"], {"code": mapping})
                self.assertEqual(packet["occurrences"],
                                 {"code": dict.fromkeys(mapping, 2)})
                self.assertEqual(packet["confirmed"], packet["occurrences"])

    def test_rejects_forged_answer_source_feedback_and_archive(self):
        valid = self.draft()
        for mutation in ("answer", "output", "explanation",
                         "missing-archive", "storage", "editorial"):
            altered = deepcopy(valid)
            target = state._blocks(altered)[CASES[0][0]]
            if mutation == "answer":
                target["answers"][0] += " inventado"
            elif mutation == "output":
                target["code"] = target["code"].replace("std::cout", "std::cerr")
            elif mutation == "explanation":
                target["explanation"] += " Texto sin autoría."
            elif mutation == "missing-archive":
                altered["stateLayout"]["identifierRenameRevisions"].pop()
            elif mutation == "storage":
                altered["stateLayout"]["identifierRenameRevisions"][0][
                    "stableStorage"]["page"] = 999
            else:
                altered["stateLayout"]["editorial"].pop()
            with self.subTest(mutation=mutation):
                with self.assertRaises((preview.RenamePreviewError,
                                        state.LayoutError, ValueError)):
                    preview.validate_read_only_rename_draft(self.before, altered)
        for bad in (
            {CASES[0][0]: {"code": {"missing_symbol": "nuevo"}}},
            {CASES[0][0]: {"initialCode": {"a": "correcto"}}},
            {CASES[0][0]: {"code": {"a": "std"}}},
            {CASES[0][0]: {"code": {"a": "a"}}},
        ):
            with self.subTest(unsafe_declaration=bad):
                with self.assertRaises((preview.RenamePreviewError,
                                        state.LayoutError)):
                    preview.prepare_declared_identifier_rename_draft(
                        self.before, bad)

    def test_real_msvc_preserves_exact_stdout_for_both_versions(self):
        compiler = shutil.which("cl.exe") or shutil.which("cl")
        self.assertIsNotNone(
            compiler, "Real Windows MSVC is required, not GCC or a skipped test")
        current = state._blocks(self.draft())
        with tempfile.TemporaryDirectory(prefix="lgaed-u2l6-rename-") as tmp:
            folder = Path(tmp)
            for identity, _, expected in CASES:
                for edition, block in (("original", self.original[identity]),
                                       ("renamed", current[identity])):
                    with self.subTest(owner=identity, edition=edition):
                        source = folder / "prediction.cpp"
                        binary = folder / (identity+"-"+edition+".exe")
                        source.write_text(block["code"], encoding="utf-8")
                        compiled = subprocess.run([
                            compiler, "/nologo", "/std:c++latest", "/EHsc",
                            "/W4", "/permissive-", "/utf-8",
                            str(source), "/Fe:"+str(binary),
                        ], cwd=folder, capture_output=True, check=False, timeout=90)
                        self.assertEqual(
                            compiled.returncode, 0,
                            "MSVC rejected C++: "+
                            compiled.stdout.decode(errors="replace")[-2000:]+
                            compiled.stderr.decode(errors="replace")[-2000:])
                        observed = subprocess.run(
                            [str(binary)], cwd=folder, capture_output=True,
                            timeout=20, check=False)
                        self.assertEqual(observed.returncode, 0)
                        self.assertEqual(observed.stderr, b"")
                        self.assertEqual(observed.stdout, expected.encode("utf-8"))


if __name__ == "__main__":
    unittest.main()

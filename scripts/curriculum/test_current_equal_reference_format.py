"""A stable C++ source can have multiple independently published reference owners.

The format revision restores only lexical spacing in the two original owners,
even when the resulting complete blocks become byte-identical to a third,
earlier-formatted reference. All real documents remain read-only.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
RELATIVE = ("programming/cpp_fundamentals/archivos_y_programa_completo/"
            "lesson_cerrar_reemplazar_y_anadir_datos.json")
CONTENT = ROOT / "app/src/main/assets/content"
FIRST = "block_0004_0001"
REFERENCES = ("block_0006_0001", "block_0008_0001")


class CurrentEqualReferenceSourceFormatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.file = CONTENT / "es" / RELATIVE
        cls.original_bytes = cls.file.read_bytes()
        cls.original = json.loads(cls.original_bytes)
        cls.family = {locale: json.loads((CONTENT / locale / RELATIVE).read_bytes())
                      for locale in cls.original["stateLayout"]["family"]}
        cls.original_owners = layout._blocks(cls.original)

    def proposal(self):
        edited = deepcopy(self.original)
        owners = layout._blocks(edited)
        original_code = self.original_owners[FIRST]["code"]
        self.assertTrue(self.original_owners[FIRST]["type"] == "code")
        for identity in REFERENCES:
            before = self.original_owners[identity]
            self.assertEqual(before["type"], "code")
            self.assertNotEqual(before["code"], original_code)
            # Only physical blank lines differ. The authored C++ sequence,
            # program meaning and the initial reference owner are unchanged.
            self.assertEqual(
                [line for line in before["code"].splitlines() if line.strip()],
                [line for line in original_code.splitlines() if line.strip()])
            owners[identity]["code"] = original_code
            self.assertTrue(layout.code_format.require_source_format(
                before, owners[identity]))
        return edited

    def test_two_equal_references_reflow_with_original_distinct_owners(self):
        revised = layout.prepare_v2(
            self.proposal(), "es", self.family, self.original)
        layout.validate_transition(self.original, revised)
        self.assertEqual(layout.validate(self.original), layout.validate(revised))
        owners = layout._blocks(revised)
        expected = self.original_owners[FIRST]["code"]
        for identity in (FIRST, *REFERENCES):
            self.assertEqual(owners[identity]["code"], expected)
        for field in ("origin", "family", "current", "additions"):
            self.assertEqual(self.original["stateLayout"][field],
                             revised["stateLayout"][field])
        for field in ("referenceAfter", "activityAfter", "textSemantics"):
            self.assertEqual(self.original.get(field), revised.get(field))
        previous = self.original["stateLayout"]
        current = revised["stateLayout"]
        formats = previous.get("sourceFormatRevisions", [])
        self.assertEqual(current["sourceFormatRevisions"][:len(formats)], formats)
        self.assertEqual(current["sourceFormatRevisions"][len(formats):],
                         [{"id": identity, "block": self.original_owners[identity]}
                          for identity in REFERENCES])
        proofs = previous["editorial"]
        self.assertEqual(current["editorial"][:len(proofs)], proofs)
        self.assertEqual(
            [edge["id"] for edge in current["editorial"][len(proofs):]],
            list(REFERENCES))
        self.assertEqual(self.file.read_bytes(), self.original_bytes)

    def test_no_foreign_identity_move_or_unreviewed_token_change(self):
        raw = self.proposal()
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(self.original, raw)
        changed = deepcopy(raw)
        owners = layout._blocks(changed)
        text = owners[REFERENCES[0]]["code"]
        self.assertIn("std::cout", text)
        owners[REFERENCES[0]]["code"] = text.replace(
            "std::cout", "std::cerr", 1)
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(changed, "es", self.family, self.original)
        relocated = deepcopy(self.original)
        current = relocated["stateLayout"]["current"]
        p, q = [next(page for page, row in enumerate(current)
                     if identity in row["blocks"]) for identity in REFERENCES]
        a = current[p]["blocks"].index(REFERENCES[0])
        b = current[q]["blocks"].index(REFERENCES[1])
        current[p]["blocks"][a], current[q]["blocks"][b] = (
            current[q]["blocks"][b], current[p]["blocks"][a])
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(self.original, relocated)
        self.assertEqual(self.file.read_bytes(), self.original_bytes)


if __name__ == "__main__":
    unittest.main()

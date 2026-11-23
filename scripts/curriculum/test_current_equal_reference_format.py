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
from format_batch_history import Publication, active_owners, publication_bytes

ROOT = Path(__file__).resolve().parents[2]
RELATIVE = ("programming/cpp_fundamentals/archivos_y_programa_completo/"
            "lesson_cerrar_reemplazar_y_anadir_datos.json")
CONTENT = ROOT / "app/src/main/assets/content"
FIRST = "block_0004_0001"
REFERENCES = ("block_0006_0001", "block_0008_0001")
PUBLICATIONS = {
    "first-format": "921e1df9171992ab7a9f1b3803b963b02fd45bbc",
    "coexisting-format": "946faafa37a396b6fc4485ba2b60f9931a7bbdfb",
}
PUBLICATION_PINS = {
    "es": ("a4c53aaac795de6410e137d0db89e0f607f5aad8d1779bd8a64111e8a993ebea",
           "1c906e101da5593c3686184b28ce1ae4c7d33f80a3304f0212c518de95a52f7d"),
    "en": "e1ee5510ea9a7e2451103b298f29c695a23cf6d76cdd95cef245b376988501cd",
    "fr": "d1c22d2c6da13a48bd2310fd72235319d6a3de899bd129444e4853e2e0c426f8",
}


def published_family(publication):
    edition = tuple(PUBLICATIONS).index(publication)
    result = {}
    for locale, pins in PUBLICATION_PINS.items():
        digest = pins[edition] if locale == "es" else pins
        path = f"app/src/main/assets/content/{locale}/{RELATIVE}"
        result[locale] = json.loads(publication_bytes(
            ROOT, path, Publication(PUBLICATIONS[publication], digest)))
    return result


class CurrentEqualReferenceSourceFormatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.file = CONTENT / "es" / RELATIVE
        cls.original_bytes = cls.file.read_bytes()
        cls.original = json.loads(cls.original_bytes)
        cls.family = {locale: json.loads((CONTENT / locale / RELATIVE).read_bytes())
                      for locale in cls.original["stateLayout"]["family"]}
        cls.original_owners = layout._blocks(cls.original)

    def publications(self):
        first = published_family("first-format")
        coexisting = published_family("coexisting-format")
        first_owners = active_owners(first["es"])[0]
        first_archives = {entry["id"] for entry in
                          first["es"]["stateLayout"].get("sourceFormatRevisions", [])}
        archived = {entry["id"]: entry["block"] for entry in
                    coexisting["es"]["stateLayout"]["sourceFormatRevisions"]}
        self.assertTrue(set(REFERENCES).isdisjoint(first_archives))
        for identity in REFERENCES:
            self.assertEqual(archived[identity], first_owners[identity])
        for before, after in ((first["es"], coexisting["es"]),
                              (coexisting["es"], self.original)):
            layout.validate_transition(before, after)
            for field in ("sourceFormatRevisions", "editorial"):
                previous = before["stateLayout"].get(field, [])
                self.assertEqual(after["stateLayout"][field][:len(previous)], previous)
        files = {locale: (CONTENT / locale / RELATIVE).read_bytes() for locale in self.family}
        for name, family in (("first-format", first), ("coexisting-format", coexisting),
                             ("working-tree", self.family)):
            unchanged = deepcopy(family)
            yield name, family["es"], family
            self.assertEqual(family, unchanged)
            for locale, payload in files.items():
                self.assertEqual((CONTENT / locale / RELATIVE).read_bytes(), payload)

    def proposal(self, original):
        original_owners = active_owners(original)[0]
        edited = deepcopy(original)
        owners = layout._blocks(edited)
        original_code = original_owners[FIRST]["code"]
        self.assertTrue(original_owners[FIRST]["type"] == "code")
        for identity in REFERENCES:
            before = original_owners[identity]
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
        for publication, original, family in self.publications():
            with self.subTest(publication=publication):
                original_owners = active_owners(original)[0]
                proposed = self.proposal(original)
                revised = layout.prepare_v2(proposed, "es", family, original)
                layout.validate_transition(original, revised)
                self.assertEqual(layout.validate(original), layout.validate(revised))
                owners = layout._blocks(revised)
                expected = original_owners[FIRST]["code"]
                for identity in (FIRST, *REFERENCES):
                    self.assertEqual(owners[identity]["code"], expected)
                self.assertEqual(owners, layout._blocks(proposed))
                for field in ("referenceAfter", "activityAfter", "textSemantics"):
                    self.assertEqual(original.get(field), revised.get(field))
                previous, current = original["stateLayout"], revised["stateLayout"]
                excluded = {"sourceFormatRevisions", "editorial"}
                self.assertEqual({key: value for key, value in previous.items() if key not in excluded},
                                 {key: value for key, value in current.items() if key not in excluded})
                formats = previous.get("sourceFormatRevisions", [])
                old_archives = {entry["id"]: entry for entry in formats}
                records = current["sourceFormatRevisions"]
                archives = {entry["id"]: entry for entry in records}
                self.assertEqual(len(old_archives), len(formats))
                self.assertEqual(len(archives), len(records))
                self.assertEqual(records[:len(formats)], formats)
                additions = [{"id": identity, "block": original_owners[identity]}
                             for identity in REFERENCES if identity not in old_archives]
                self.assertEqual(records[len(formats):], additions)
                for identity in REFERENCES:
                    self.assertEqual(archives[identity], old_archives.get(
                        identity, {"id": identity, "block": original_owners[identity]}))
                proofs = previous["editorial"]
                self.assertEqual(current["editorial"][:len(proofs)], proofs)
                self.assertEqual(current["editorial"][len(proofs):], [{
                    "id": identity, "beforeSha256": layout.fingerprint(original_owners[identity]),
                    "afterSha256": layout.fingerprint(owners[identity]),
                } for identity in REFERENCES])
                self.assertEqual(self.file.read_bytes(), self.original_bytes)

    def test_no_foreign_identity_move_or_unreviewed_token_change(self):
        for publication, original, family in self.publications():
            with self.subTest(publication=publication):
                raw = self.proposal(original)
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(original, raw)
                changed = deepcopy(raw)
                owners = layout._blocks(changed)
                text = owners[REFERENCES[0]]["code"]
                self.assertIn("std::cout", text)
                owners[REFERENCES[0]]["code"] = text.replace(
                    "std::cout", "std::cerr", 1)
                with self.assertRaises(layout.LayoutError):
                    layout.prepare_v2(changed, "es", family, original)
                relocated = deepcopy(original)
                current = relocated["stateLayout"]["current"]
                positions = active_owners(relocated)[1]
                (p, a), (q, b) = (positions[identity] for identity in REFERENCES)
                current[p]["blocks"][a], current[q]["blocks"][b] = (
                    current[q]["blocks"][b], current[p]["blocks"][a])
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(original, relocated)
                self.assertEqual(self.file.read_bytes(), self.original_bytes)


if __name__ == "__main__":
    unittest.main()

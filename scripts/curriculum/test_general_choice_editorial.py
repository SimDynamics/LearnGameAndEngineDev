"""General choice wording revision: preserved responses and exact provenance.

The real U9L13 published choice is read but never rewritten by these tests.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import unittest

import stable_state_layout as layout


SOURCE = (
    Path(__file__).resolve().parents[2]
    / "app/src/main/assets/content/es/programming/cpp_fundamentals"
    / "datos_propios_y_estado_valido"
    / "lesson_construir_y_comprobar_un_modelo_de_pedidos.json"
)
OWNER = "block_0007_0002"
REVISED = (
    "La operación incumple la condición para aceptar la transición, "
    "aunque conserve una cantidad válida."
)


class GeneralChoiceEditorialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = json.loads(SOURCE.read_text(encoding="utf-8"))
        layout.validate(cls.original)
        cls.block = layout._blocks(cls.original)[OWNER]
        assert cls.block["correctIndex"] == 0
        assert cls.block["options"][0] != REVISED

    def revise(self):
        return layout.revise_choice_editorial(self.original, OWNER, 0, REVISED)

    def test_one_compatible_choice_keeps_complete_response_contract(self):
        changed = self.revise()
        old, new = self.block, layout._blocks(changed)[OWNER]
        self.assertEqual(new["options"][0], REVISED)
        self.assertEqual(new["options"][1:], old["options"][1:])
        self.assertEqual(new["correctIndex"], old["correctIndex"])
        self.assertEqual(new["question"], old["question"])
        self.assertEqual(new["explanation"], old["explanation"])
        self.assertEqual(new["reviewFeedback"], old["reviewFeedback"])
        self.assertEqual(changed["stateLayout"]["origin"], self.original["stateLayout"]["origin"])
        self.assertEqual(changed["stateLayout"]["current"], self.original["stateLayout"]["current"])
        self.assertEqual(layout.original_projection(changed), layout.original_projection(self.original))
        layout.validate_transition(self.original, changed)
        self.assertEqual(len(changed["stateLayout"]["choiceRevisions"]), 1)
        archived = changed["stateLayout"]["choiceRevisions"][0]
        original_semantic = next(field for field in self.original["textSemantics"]["fields"]
                                 if field["block"] == OWNER and field["path"] == "/options/0")
        self.assertEqual(archived["semanticBefore"], original_semantic)

    def test_revised_option_keeps_exact_semantic_spans_and_history(self):
        original_field = next(field for field in self.original["textSemantics"]["fields"]
                              if field["block"] == OWNER and field["path"] == "/options/0")
        changed = self.revise()
        revised_field = next(field for field in changed["textSemantics"]["fields"]
                             if field["block"] == OWNER and field["path"] == "/options/0")
        self.assertEqual(original_field["spans"], revised_field["spans"])
        self.assertEqual(revised_field["sourceSha256"],
                         hashlib.sha256(REVISED.encode("utf-8")).hexdigest())
        self.assertEqual(original_field["sourceSha256"],
                         hashlib.sha256(self.block["options"][0].encode("utf-8")).hexdigest())
        before = deepcopy(self.original["textSemantics"])
        after = deepcopy(changed["textSemantics"])
        for field in after["fields"]:
            if field["block"] == OWNER and field["path"] == "/options/0":
                field["sourceSha256"] = original_field["sourceSha256"]
        self.assertEqual(after, before, "An unrelated text-semantic marker was changed")
        self.assertEqual(layout.original_projection(changed)["textSemantics"], before)

    def test_marked_option_retains_its_reading_spans(self):
        # The published U9 option has an empty spans array. Exercise the
        # GENERAL mechanism with a genuine, nonempty UTF-16 selection too.
        authored = deepcopy(self.original)
        marker = next(field for field in authored["textSemantics"]["fields"]
                      if field["block"] == OWNER and field["path"] == "/options/0")
        selected = layout.text_semantics.select(
            self.block["options"][0], "La operación", "prose", reading="natural")
        marker["spans"] = [selected]
        layout.validate(authored)
        reviewed = layout.revise_choice_editorial(authored, OWNER, 0, REVISED)
        updated = next(field for field in reviewed["textSemantics"]["fields"]
                       if field["block"] == OWNER and field["path"] == "/options/0")
        archive = reviewed["stateLayout"]["choiceRevisions"][0]["semanticBefore"]
        self.assertEqual(updated["spans"], [selected])
        self.assertEqual(archive["spans"], [selected])
        self.assertEqual(layout.original_projection(reviewed)["textSemantics"],
                         authored["textSemantics"])
        layout.validate_transition(authored, reviewed)

    def test_marked_option_rejects_stale_utf16_positions(self):
        authored = deepcopy(self.original)
        marker = next(field for field in authored["textSemantics"]["fields"]
                      if field["block"] == OWNER and field["path"] == "/options/0")
        marker["spans"] = [
            layout.text_semantics.select(self.block["options"][0],
                                         "precondición", "data")]
        layout.validate(authored)
        # The replacement removes this expression. Reusing the old offset
        # would narrate/emphasize different words and must be rejected.
        with self.assertRaises(layout.LayoutError):
            layout.revise_choice_editorial(authored, OWNER, 0, REVISED)

    def test_rejects_stale_or_rewritten_narration_marking(self):
        valid = self.revise()
        changed = deepcopy(valid)
        field = next(entry for entry in changed["textSemantics"]["fields"]
                     if entry["block"] == OWNER and entry["path"] == "/options/0")
        field["sourceSha256"] = next(entry for entry in self.original["textSemantics"]["fields"]
                                    if entry["block"] == OWNER and entry["path"] == "/options/0")["sourceSha256"]
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed)
        changed = deepcopy(valid)
        field = next(entry for entry in changed["textSemantics"]["fields"]
                     if entry["block"] == OWNER and entry["path"] == "/options/0")
        field["spans"].append({"start": 0, "end": 2, "text": "La", "role": "prose"})
        # The archived source already proves the former spans, so even
        # standalone validation must reject this unreviewed new marking.
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed)
        changed = deepcopy(valid)
        changed["textSemantics"]["fields"] = [
            f for f in changed["textSemantics"]["fields"]
            if (f["block"], f["path"]) != (OWNER, "/options/0")]
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed)

    def test_rejects_answer_or_provenance_tampering(self):
        valid = self.revise()
        variants = []
        def amended(modify):
            data = deepcopy(valid)
            modify(data, layout._blocks(data)[OWNER])
            variants.append(data)

        amended(lambda doc, block: block["options"].__setitem__(1, "Cambiada sin permiso"))
        amended(lambda doc, block: block.__setitem__("correctIndex", 1))
        amended(lambda doc, block: block.__setitem__("question", "Pregunta sustituida"))
        amended(lambda doc, block: block["reviewFeedback"].__setitem__(0, "Pista sustituida"))
        amended(lambda doc, block: block["options"].__setitem__(0, "Una respuesta distinta"))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"][0].__setitem__("replacement", "Otra versión"))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"][0].__setitem__("optionIndex", 1))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"][0]["semanticBefore"].__setitem__(
            "sourceSha256", "0" * 64))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"][0]["semanticBefore"]["spans"].append(
            {"start": 0, "end": 2, "text": "La", "role": "prose"}))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"][0]["block"]["options"].__setitem__(0, "Original falsa"))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"].append(
            deepcopy(doc["stateLayout"]["choiceRevisions"][0])))
        amended(lambda doc, block: doc["stateLayout"].__setitem__("choiceRevisions", []))
        amended(lambda doc, block: doc["stateLayout"]["editorial"].pop())
        for n, candidate in enumerate(variants):
            with self.subTest(variant=n), self.assertRaises(layout.LayoutError):
                layout.validate(candidate)

    def test_rejects_unreviewed_semantic_or_empty_text_change(self):
        for value in ("", "   ", self.block["options"][0]):
            with self.subTest(value=value), self.assertRaises(layout.LayoutError):
                layout.revise_choice_editorial(self.original, OWNER, 0, value)


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 3 and sys.argv[1] in {
            "--write-reviewed", "--write-reviewed-marked"}:
        published = json.loads(SOURCE.read_text(encoding="utf-8"))
        if sys.argv[1] == "--write-reviewed-marked":
            marker = next(field for field in published["textSemantics"]["fields"]
                          if field["block"] == OWNER and field["path"] == "/options/0")
            marker["spans"] = [layout.text_semantics.select(
                layout._blocks(published)[OWNER]["options"][0],
                "La operación", "prose", reading="natural")]
        revised = layout.revise_choice_editorial(published, OWNER, 0, REVISED)
        output = Path(sys.argv[2])
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(revised, ensure_ascii=False, indent=2) + "\n",
                          encoding="utf-8")
    else:
        unittest.main()

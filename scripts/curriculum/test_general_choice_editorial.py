"""General choice wording revision: preserved responses and exact provenance.

The real U9L13 published choice is read but never rewritten by these tests.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
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

    def test_general_option_review_has_safe_public_cli(self):
        # A published choice and its Spanish lesson are never rewritten here.
        published_bytes = SOURCE.read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            proposal = Path(directory) / "choice-proposal.json"
            command = [
                sys.executable, "-B", str(Path(layout.__file__)),
                "revise-choice-editorial", str(SOURCE),
                "--previous", str(SOURCE), "--identity", OWNER,
                "--option-index", "0", "--replacement", REVISED,
                "--output", str(proposal),
            ]
            result = subprocess.run(command, capture_output=True, text=True,
                                    encoding="utf-8")
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(self.revise(),
                             json.loads(proposal.read_text(encoding="utf-8")))
            self.assertEqual(published_bytes, SOURCE.read_bytes())

            # Exclusive output creation avoids replacing another author's work.
            repeated = subprocess.run(command, capture_output=True, text=True,
                                      encoding="utf-8")
            self.assertNotEqual(0, repeated.returncode)
            self.assertEqual(self.revise(),
                             json.loads(proposal.read_text(encoding="utf-8")))

            invalid = command.copy()
            invalid[invalid.index("--option-index") + 1] = str(
                len(self.block["options"]))
            rejected = Path(directory) / "rejected.json"
            invalid[invalid.index("--output") + 1] = str(rejected)
            outcome = subprocess.run(invalid, capture_output=True, text=True,
                                     encoding="utf-8")
            self.assertNotEqual(0, outcome.returncode)
            self.assertFalse(rejected.exists())

            no_identity = command.copy()
            no_identity[no_identity.index("--identity") + 1] = "block_9999_9999"
            no_identity[no_identity.index("--output") + 1] = str(rejected)
            outcome = subprocess.run(no_identity, capture_output=True, text=True,
                                     encoding="utf-8")
            self.assertNotEqual(0, outcome.returncode)
            self.assertFalse(rejected.exists())
            self.assertEqual(published_bytes, SOURCE.read_bytes())


class GeneralQuestionEditorialTests(unittest.TestCase):
    """The author may clarify a question without changing its expected answer."""

    @staticmethod
    def lesson(relative: str):
        root = Path(__file__).resolve().parents[2] / "app/src/main/assets/content"
        family = {
            locale: json.loads((root / locale / relative).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")
        }
        return family, "app/src/main/assets/content/es/" + relative

    @staticmethod
    def owner_block(doc, identity):
        for p, record in enumerate(doc["stateLayout"]["current"]):
            for b, block_id in enumerate(record["blocks"]):
                if block_id == identity:
                    return (p, b), doc["pages"][p]["blocks"][b]
        raise AssertionError("Missing published stable activity owner " + identity)

    def test_u12l22_question_only_keeps_output_and_semantic_provenance(self):
        import validar_json_programacion as contract

        relative = ("programming/cpp_fundamentals/archivos_y_programa_completo/"
                    "lesson_leer_un_grupo_con_cierre_propio.json")
        family, path = self.lesson(relative)
        before = family["es"]
        layout.validate(before)
        identity = "block_0001_0002"
        (p, b), original = self.owner_block(before, identity)
        self.assertEqual((p, b), (1, 2))
        self.assertEqual(original["answers"], ["1:}:fin"])
        changed = deepcopy(before)
        updated = changed["pages"][p]["blocks"][b]
        updated["question"] = ("Predice la salida. La cadena mostrada contiene una "
                               "llave de cierre después de dos espacios.")
        marker = next(field for field in changed["textSemantics"]["fields"]
                      if field["block"] == identity and field["path"] == "/question")
        self.assertEqual(marker["spans"], [])
        marker.update(layout.text_semantics.make_field(changed, identity, "/question", []))
        after = layout.prepare_v2(changed, "es", family, before)
        layout.validate_transition(before, after)
        self.assertEqual(layout.validate(before), layout.validate(after))
        self.assertEqual(before["stateLayout"]["origin"], after["stateLayout"]["origin"])
        self.assertEqual(before["stateLayout"]["current"], after["stateLayout"]["current"])
        self.assertEqual(before["stateLayout"]["family"], after["stateLayout"]["family"])
        self.assertEqual(original["code"], after["pages"][p]["blocks"][b]["code"])
        self.assertEqual(original["answers"], after["pages"][p]["blocks"][b]["answers"])
        self.assertEqual(original["explanation"], after["pages"][p]["blocks"][b]["explanation"])
        self.assertTrue(contract.preserves_ordinary_explanation_revision(
            path, 1, 2, original, after["pages"][p]["blocks"][b], before, after))
        self.assertTrue(contract.preserves_previous_exercise(
            path, 1, 2, original, after["pages"][p]["blocks"][b],
            previous_document=before, current_document=after))
        forged = deepcopy(after)
        forged["pages"][p]["blocks"][b]["answers"] = ["otro resultado"]
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged)
        stale = deepcopy(after)
        next(field for field in stale["textSemantics"]["fields"]
             if field["block"] == identity and field["path"] == "/question")[
                 "sourceSha256"] = next(field for field in before["textSemantics"]["fields"]
                                       if field["block"] == identity and
                                       field["path"] == "/question")["sourceSha256"]
        with self.assertRaises(layout.LayoutError):
            layout.validate(stale)

        # A question-only correction may not repaint the unchanged explanation.
        repainted = deepcopy(after)
        descriptor = next(field for field in repainted["textSemantics"]["fields"]
                          if field["block"] == identity and field["path"] == "/explanation")
        self.assertTrue(descriptor["spans"])
        descriptor["spans"][0]["role"] = "prose"
        layout.validate(repainted)
        self.assertFalse(contract.preserves_ordinary_explanation_revision(
            path, p, b, original, repainted["pages"][p]["blocks"][b], before, repainted))
        self.assertFalse(contract.preserves_previous_exercise(
            path, p, b, original, repainted["pages"][p]["blocks"][b],
            previous_document=before, current_document=repainted))

    def test_u12_question_and_explanation_return_to_authentic_previous_text(self):
        import validar_json_programacion as contract

        relative = ("programming/cpp_fundamentals/archivos_y_programa_completo/"
                    "lesson_estado_del_formato_y_flujos_independientes.json")
        family, path = self.lesson(relative)
        before = family["es"]
        layout.validate(before)
        identity = "block_0002_0002"
        (p, b), original = self.owner_block(before, identity)
        self.assertEqual((p, b), (2, 3))
        self.assertEqual(original["correctIndex"], 2)
        original_history = [
            proof for proof in before["stateLayout"]["editorial"]
            if proof["id"] == identity
        ]
        previous_block = original_history[-1]["returnTo"]["previousBlock"]
        self.assertIn("texto << std::hex << 31", previous_block["question"])
        self.assertIn("texto << std::dec", previous_block["explanation"])
        self.assertEqual(previous_block["options"], original["options"])
        self.assertEqual(previous_block["correctIndex"], original["correctIndex"])
        self.assertEqual(previous_block["reviewFeedback"], original["reviewFeedback"])
        revised = deepcopy(before)
        revised["pages"][p]["blocks"][b] = deepcopy(previous_block)

        question = previous_block["question"]
        explanation = previous_block["explanation"]
        selections = {
            "/question": [
                layout.text_semantics.select(question, "texto << std::hex << 31",
                                             "cpp", reading="technical"),
                layout.text_semantics.select(question, "texto << std::dec",
                                             "cpp", reading="technical"),
            ],
            "/explanation": [
                layout.text_semantics.select(explanation, "texto << std::hex << 31",
                                             "cpp", reading="technical"),
                layout.text_semantics.select(explanation, "1f", "output",
                                             occurrence=1, reading="natural"),
                layout.text_semantics.select(explanation, "texto << std::dec",
                                             "cpp", reading="technical"),
                layout.text_semantics.select(explanation, "1f", "output",
                                             occurrence=2, reading="natural"),
            ],
        }
        for path_name, spans in selections.items():
            marker = next(field for field in revised["textSemantics"]["fields"]
                          if field["block"] == identity and field["path"] == path_name)
            marker.update(layout.text_semantics.make_field(
                revised, identity, path_name, spans))
        chain = [original_history[0]["beforeSha256"]] + [
            item["afterSha256"] for item in original_history]
        revision = chain.index(layout.fingerprint(previous_block))
        self.assertLess(revision, len(chain) - 1)
        after = layout.prepare_v2(revised, "es", family, before,
                                  editorial_returns={identity: revision})
        layout.validate_transition(before, after)
        self.assertEqual(layout.validate(before), layout.validate(after))
        self.assertIn("returnTo", after["stateLayout"]["editorial"][-1])
        self.assertEqual(before["stateLayout"]["origin"], after["stateLayout"]["origin"])
        self.assertEqual(before["stateLayout"]["current"], after["stateLayout"]["current"])
        self.assertEqual(before["stateLayout"]["family"], after["stateLayout"]["family"])
        self.assertEqual(original["options"], after["pages"][p]["blocks"][b]["options"])
        self.assertEqual(original["correctIndex"],
                         after["pages"][p]["blocks"][b]["correctIndex"])
        self.assertTrue(contract.preserves_ordinary_explanation_revision(
            path, 2, 2, original, after["pages"][p]["blocks"][b], before, after))
        self.assertTrue(contract.preserves_previous_exercise(
            path, 2, 2, original, after["pages"][p]["blocks"][b],
            previous_document=before, current_document=after))
        # The evaluation never becomes an invented third answer, even if the
        # adversary copies the new question and its accepted semantic spans.
        forged = deepcopy(after)
        forged["pages"][p]["blocks"][b]["correctIndex"] = 1
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged)
        forged = deepcopy(after)
        forged["stateLayout"]["editorial"][-1]["returnTo"]["previousBlock"][
            "options"][0] = "forged"
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged)

        # A two-field clarification may not change an unrelated option's
        # technical reading, even if that altered descriptor is well-formed.
        repainted = deepcopy(after)
        descriptor = next(field for field in repainted["textSemantics"]["fields"]
                          if field["block"] == identity and field["path"] == "/options/1")
        self.assertTrue(descriptor["spans"])
        descriptor["spans"][0]["role"] = "prose"
        layout.validate(repainted)
        self.assertFalse(contract.preserves_ordinary_explanation_revision(
            path, p, b, original, repainted["pages"][p]["blocks"][b], before, repainted))
        self.assertFalse(contract.preserves_previous_exercise(
            path, p, b, original, repainted["pages"][p]["blocks"][b],
            previous_document=before, current_document=repainted))

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

"""General choice wording revision: preserved responses and exact provenance.

Fixtures retain a pinned real U9L13 predecessor after Work updates the live
lesson. The current curriculum is validated separately and never rewritten.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import editorial_fixture_history as history
import stable_state_layout as layout


RELATIVE = ("programming/cpp_fundamentals/datos_propios_y_estado_valido/"
            "lesson_construir_y_comprobar_un_modelo_de_pedidos.json")
SOURCE = history.CONTENT / "es" / RELATIVE
OWNER = "block_0007_0002"
REVISED = (
    "La operación incumple la condición para aceptar la transición, "
    "aunque conserve una cantidad válida."
)
EXPORT_MODES = ("--write-original", "--write-reviewed", "--write-reviewed-marked",
                "--write-reviewed-new-mark-from-unmarked")


def fixture_bytes(mode: str) -> bytes:
    if mode not in EXPORT_MODES:
        raise ValueError(f"Unknown U9 editorial fixture export: {mode}")
    raw = history.historical_bytes("es/" + RELATIVE)
    published = json.loads(raw)
    layout.validate(published)
    if mode == "--write-original":
        return raw
    if mode == "--write-reviewed-marked":
        # The native reader MUST restore the old descriptor, not transfer the
        # new option's UTF-16 positions onto a different historical sentence.
        # Both precise selections come from a real, pinned U9 source.
        marker = next(field for field in published["textSemantics"]["fields"]
                      if field["block"] == OWNER and field["path"] == "/options/0")
        marker["spans"] = [layout.text_semantics.select(
            layout._blocks(published)[OWNER]["options"][0],
            "precondición", "data", reading="technical")]
        new_spans = [layout.text_semantics.select(
            REVISED, "condición para aceptar la transición", "prose", reading="natural")]
        revised = layout.revise_choice_editorial(
            published, OWNER, 0, REVISED, spans=new_spans)
    elif mode == "--write-reviewed-new-mark-from-unmarked":
        # Synthetic QA predecessor, derived from the authenticated pinned U9
        # source: deliberately remove only its prior option marker BEFORE
        # authoring a new option. This is not a published historical lesson.
        published["textSemantics"]["fields"] = [
            field for field in published["textSemantics"]["fields"]
            if (field["block"], field["path"]) != (OWNER, "/options/0")
        ]
        new_spans = [layout.text_semantics.select(
            REVISED, "condición para aceptar la transición", "prose", reading="natural")]
        revised = layout.revise_choice_editorial(
            published, OWNER, 0, REVISED, spans=new_spans)
    else:
        revised = layout.revise_choice_editorial(published, OWNER, 0, REVISED)
    layout.validate_transition(published, revised)
    return (json.dumps(revised, ensure_ascii=False, indent=2, allow_nan=False)
            + "\n").encode("utf-8")


class GeneralChoiceEditorialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = json.loads(fixture_bytes("--write-original"))
        layout.validate(cls.original)
        cls.block = layout._blocks(cls.original)[OWNER]
        assert cls.block["correctIndex"] == 0
        assert cls.block["options"][0] != REVISED

    def revise(self):
        return layout.revise_choice_editorial(self.original, OWNER, 0, REVISED)

    def test_pinned_sources_reject_tampered_bytes_or_missing_history(self):
        for relative in history.SOURCE_SHA256:
            raw = history.historical_bytes(relative)
            with self.subTest(source=relative), self.assertRaises(ValueError):
                history.verify_bytes(relative, raw + b"\n")
        history.historical_bytes.cache_clear()
        missing = subprocess.CalledProcessError(128, ["git", "show"])
        with patch.object(history.subprocess, "check_output", side_effect=missing), \
                patch.object(Path, "read_text", side_effect=AssertionError("Live fallback")), \
                patch.object(Path, "read_bytes", side_effect=AssertionError("Live fallback")):
            with self.assertRaises(subprocess.CalledProcessError):
                history.historical_bytes("es/" + RELATIVE)

    def test_native_fixture_with_new_option_marks_has_genuine_synthetic_predecessor(self):
        authored = json.loads(fixture_bytes("--write-reviewed-new-mark-from-unmarked"))
        active = layout._blocks(authored)[OWNER]
        entry = authored["stateLayout"]["choiceRevisions"][0]
        self.assertEqual(entry["replacement"], REVISED)
        self.assertIsNone(entry["semanticBefore"])
        self.assertTrue(entry["semanticRootBefore"])
        self.assertEqual(
            entry["semanticAfter"],
            layout.text_semantics.make_field(authored, OWNER, "/options/0",
                [layout.text_semantics.select(
                    REVISED, "condición para aceptar la transición", "prose",
                    reading="natural")]))
        self.assertEqual(active["options"][0], REVISED)
        self.assertEqual(active["correctIndex"], self.block["correctIndex"])
        self.assertNotIn(
            (OWNER, "/options/0"),
            {(field["block"], field["path"])
             for field in layout.original_projection(authored)["textSemantics"]["fields"]})
        self.assertEqual(layout.original_projection(authored)["pages"],
                         layout.original_projection(self.original)["pages"])
        marked_before = json.loads(fixture_bytes("--write-reviewed-marked"))
        marked_new = next(field for field in marked_before["textSemantics"]["fields"]
                          if (field["block"], field["path"]) == (OWNER, "/options/0"))
        unmarked_new = next(field for field in authored["textSemantics"]["fields"]
                            if (field["block"], field["path"]) == (OWNER, "/options/0"))
        self.assertEqual(marked_new["spans"], unmarked_new["spans"])
        self.assertEqual(marked_new["sourceSha256"], unmarked_new["sourceSha256"])
        self.assertEqual(marked_before["stateLayout"]["choiceRevisions"][0]["semanticBefore"]["spans"][0]["text"],
                         "precondición")
        self.assertIsNone(entry["semanticBefore"])
        # The full-word C++ native loader test independently uses a shorter
        # "condición" selection. Do not compare those two valid QA authorings
        # as if their UTF-16 ranges were required to match.

    def test_live_lesson_preserves_origin_and_historical_provenance(self):
        current = json.loads(SOURCE.read_text(encoding="utf-8"))
        history.validate_live_descendant(self.original, current)

    def test_live_validation_rejects_origin_archive_and_answer_tampering(self):
        reviewed = self.revise()
        variants = []
        changed = deepcopy(reviewed)
        changed["stateLayout"]["origin"][0]["blocks"][0]["sha256"] = "0" * 64
        variants.append(changed)
        changed = deepcopy(reviewed)
        changed["stateLayout"]["choiceRevisions"][0]["block"]["options"][0] = "Origen falso"
        variants.append(changed)
        changed = deepcopy(reviewed)
        layout._blocks(changed)[OWNER]["correctIndex"] = 1
        variants.append(changed)
        for index, current in enumerate(variants):
            with self.subTest(mutation=index), self.assertRaises(ValueError):
                history.validate_live_descendant(self.original, current)

    def test_exports_ignore_live_correction_and_later_compatible_addition(self):
        expected = {mode: fixture_bytes(mode) for mode in EXPORT_MODES}
        reviewed = json.loads(expected["--write-reviewed"])
        layout.validate_transition(self.original, reviewed)
        plan = deepcopy(reviewed["stateLayout"]["current"])
        identity = "qa_editorial_followup_note"
        plan[-1]["blocks"].append(identity)
        expanded = layout.reorder(reviewed, plan, [{
            "id": identity, "storage": {"page": 900000, "block": 0},
            "progressPage": plan[-1]["id"],
            "block": {"type": "paragraph", "text": "Seguimiento editorial de QA."},
        }])
        unmarked = deepcopy(expanded)
        expanded["textSemantics"]["fields"].append(
            layout.text_semantics.make_field(expanded, identity, "/text", []))
        layout.validate_transition(unmarked, expanded)

        read_text, read_bytes = Path.read_text, Path.read_bytes
        for label, current in (("corrected", reviewed), ("expanded", expanded)):
            history.validate_live_descendant(self.original, current)
            payload = json.dumps(current, ensure_ascii=False).encode("utf-8")
            def live_text(path, *args, **kwargs):
                return payload.decode("utf-8") if path == SOURCE else read_text(path, *args, **kwargs)
            def live_bytes(path):
                return payload if path == SOURCE else read_bytes(path)
            with self.subTest(publication=label), \
                    patch.object(Path, "read_text", live_text), \
                    patch.object(Path, "read_bytes", live_bytes):
                for mode in EXPORT_MODES:
                    self.assertEqual(expected[mode], fixture_bytes(mode))

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
        aligned = layout.align_for_previous_validation(self.original, changed)
        self.assertEqual(aligned["textSemantics"], self.original["textSemantics"])
        layout.text_semantics.validate(aligned)
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

    def test_explicit_semantics_can_reselect_a_changed_marked_option(self):
        authored = deepcopy(self.original)
        prior = next(field for field in authored["textSemantics"]["fields"]
                     if field["block"] == OWNER and field["path"] == "/options/0")
        prior["spans"] = [layout.text_semantics.select(
            self.block["options"][0], "precondición", "cpp", reading="technical")]
        layout.validate(authored)
        revised_spans = [layout.text_semantics.select(
            REVISED, "condición para aceptar la transición", "prose", reading="natural")]
        reviewed = layout.revise_choice_editorial(
            authored, OWNER, 0, REVISED, spans=revised_spans)
        record = reviewed["stateLayout"]["choiceRevisions"][0]
        current = next(field for field in reviewed["textSemantics"]["fields"]
                       if field["block"] == OWNER and field["path"] == "/options/0")
        self.assertEqual(record["semanticBefore"], prior)
        self.assertEqual(record["semanticAfter"], current)
        self.assertTrue(record["semanticRootBefore"])
        self.assertEqual(current["spans"], revised_spans)
        self.assertEqual(layout.original_projection(reviewed)["textSemantics"],
                         authored["textSemantics"])
        aligned = layout.align_for_previous_validation(authored, reviewed)
        self.assertEqual(aligned["textSemantics"], authored["textSemantics"])
        layout.text_semantics.validate(aligned)
        self.assertEqual(layout._blocks(reviewed)[OWNER]["correctIndex"],
                         self.block["correctIndex"])
        layout.validate_transition(authored, reviewed)

        for mutation in ("after-archive", "current-mark", "wrong-answer",
                         "fake-root", "removed-mark", "unrelated-mark"):
            candidate = deepcopy(reviewed)
            record = candidate["stateLayout"]["choiceRevisions"][0]
            marker = next(field for field in candidate["textSemantics"]["fields"]
                          if field["block"] == OWNER and field["path"] == "/options/0")
            if mutation == "after-archive":
                record["semanticAfter"]["sourceSha256"] = "0" * 64
            elif mutation == "current-mark":
                marker["spans"] = []
            elif mutation == "wrong-answer":
                layout._blocks(candidate)[OWNER]["correctIndex"] = 1
            elif mutation == "fake-root":
                record["semanticRootBefore"] = False
            elif mutation == "removed-mark":
                candidate["textSemantics"]["fields"].remove(marker)
            else:
                candidate["textSemantics"]["fields"][0]["sourceSha256"] = "0" * 64
            with self.subTest(mutation=mutation), self.assertRaises(layout.LayoutError):
                layout.validate(candidate)

        with self.assertRaises(layout.LayoutError):
            layout.revise_choice_editorial(authored, OWNER, 0, REVISED, spans=[])

    def test_explicit_semantics_can_add_and_recover_a_missing_option_marker(self):
        original = deepcopy(self.original)
        prior = next(field for field in original["textSemantics"]["fields"]
                     if field["block"] == OWNER and field["path"] == "/options/0")
        original["textSemantics"]["fields"].remove(prior)
        layout.validate(original)
        spans = [layout.text_semantics.select(
            REVISED, "transición", "prose", reading="natural")]
        changed = layout.revise_choice_editorial(original, OWNER, 0, REVISED, spans=spans)
        record = changed["stateLayout"]["choiceRevisions"][0]
        self.assertIsNone(record["semanticBefore"])
        self.assertEqual(record["semanticAfter"]["spans"], spans)
        self.assertTrue(record["semanticRootBefore"])
        layout.validate_transition(original, changed)
        self.assertEqual(layout.original_projection(changed)["textSemantics"],
                         original["textSemantics"])
        aligned = layout.align_for_previous_validation(original, changed)
        self.assertEqual(aligned["textSemantics"], original["textSemantics"])
        layout.text_semantics.validate(aligned)

    def test_new_semantic_root_is_authored_and_removed_from_historical_projection(self):
        original = deepcopy(self.original)
        original.pop("textSemantics", None)
        layout.validate(original)
        changed = layout.revise_choice_editorial(original, OWNER, 0, REVISED, spans=[])
        self.assertFalse(changed["stateLayout"]["choiceRevisions"][0]["semanticRootBefore"])
        self.assertIn("textSemantics", changed)
        self.assertNotIn("textSemantics", layout.original_projection(changed))
        aligned = layout.align_for_previous_validation(original, changed)
        self.assertNotIn("textSemantics", aligned)
        self.assertEqual(layout.text_semantics.validate(aligned), {})
        layout.validate_transition(original, changed)
        # A later, independently authored descriptor belongs to the current
        # reading, never to a publication that had no semantic root at all.
        later = deepcopy(changed)
        later["textSemantics"]["fields"].append(layout.text_semantics.make_field(
            later, OWNER, "/question", []))
        layout.validate(later)
        self.assertNotIn("textSemantics", layout.original_projection(later))
        forged = deepcopy(changed)
        forged["stateLayout"]["choiceRevisions"][0]["semanticRootBefore"] = True
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(original, forged)

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
        for value in ("", "   ", "\u00a0", "\u0085", "\u2003",
                      "\u202f", "\u3000", "\u00a0\t\u2003",
                      self.block["options"][0]):
            with self.subTest(value=value), self.assertRaises(layout.LayoutError):
                layout.revise_choice_editorial(self.original, OWNER, 0, value)

    def test_general_option_review_has_safe_public_cli(self):
        # A published choice and its Spanish lesson are never rewritten here.
        published_bytes = SOURCE.read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "choice-original.json"
            source.write_bytes(history.historical_bytes("es/" + RELATIVE))
            original_bytes = source.read_bytes()
            proposal = Path(directory) / "choice-proposal.json"
            command = [
                sys.executable, "-B", str(Path(layout.__file__)),
                "revise-choice-editorial", str(source),
                "--previous", str(source), "--identity", OWNER,
                "--option-index", "0", "--replacement", REVISED,
                "--output", str(proposal),
            ]
            result = subprocess.run(command, capture_output=True, text=True,
                                    encoding="utf-8")
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(self.revise(),
                             json.loads(proposal.read_text(encoding="utf-8")))
            self.assertEqual(published_bytes, SOURCE.read_bytes())
            self.assertEqual(original_bytes, source.read_bytes())

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
            self.assertEqual(original_bytes, source.read_bytes())


    def test_explicit_semantic_cli_creates_isolated_authored_option(self):
        source_bytes = history.historical_bytes("es/" + RELATIVE)
        spans = [layout.text_semantics.select(
            REVISED, "condición para aceptar la transición", "prose",
            reading="natural")]
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            previous = base / "original.json"
            semantic = base / "spans.json"
            proposal = base / "proposal.json"
            previous.write_bytes(source_bytes)
            semantic.write_text(json.dumps(spans, ensure_ascii=False),
                                encoding="utf-8")
            command = [
                sys.executable, "-B", str(Path(layout.__file__)),
                "revise-choice-editorial", str(previous),
                "--previous", str(previous), "--identity", OWNER,
                "--option-index", "0", "--replacement", REVISED,
                "--semantic-spans", str(semantic),
                "--output", str(proposal),
            ]
            status = subprocess.run(command, capture_output=True,
                                    text=True, encoding="utf-8")
            self.assertEqual(status.returncode, 0, status.stdout + status.stderr)
            authored = json.loads(proposal.read_text(encoding="utf-8"))
            self.assertEqual(authored, layout.revise_choice_editorial(
                self.original, OWNER, 0, REVISED, spans=spans))
            self.assertEqual(previous.read_bytes(), source_bytes)
            self.assertEqual(layout.original_projection(authored)["textSemantics"],
                             self.original["textSemantics"])

            rejected = base / "invalid.json"
            corrupt = deepcopy(spans)
            corrupt[0]["start"] += 1
            semantic.write_text(json.dumps(corrupt, ensure_ascii=False), encoding="utf-8")
            command[-1] = str(rejected)
            failure = subprocess.run(command, capture_output=True,
                                     text=True, encoding="utf-8")
            self.assertNotEqual(failure.returncode, 0)
            self.assertFalse(rejected.exists())
            self.assertEqual(previous.read_bytes(), source_bytes)


class GeneralChoiceCompositionTests(unittest.TestCase):
    """Compose real U9 option/prose revisions across a skipped publication."""
    OTHER = "block_0006_0001"
    THIRD = "block_0003_0002"
    QUESTION = (
        "¿Qué prueba permite distinguir un rechazo correcto de una operación "
        "que escribe antes de comprobar?"
    )
    EXPLANATION = (
        "Después del rechazo, consulta la cantidad guardada. Devolver false "
        "no demuestra que se haya conservado: el bool sólo comunica el rechazo."
    )

    @classmethod
    def setUpClass(cls):
        cls.original = json.loads(fixture_bytes("--write-original"))
        layout.validate(cls.original)

    @staticmethod
    def marker(document, identity, path):
        return next(field for field in document["textSemantics"]["fields"]
                    if field["block"] == identity and field["path"] == path)

    def choice(self, document):
        return layout.revise_choice_editorial(document, OWNER, 0, REVISED)

    def ordinary(self, document, fields=("question",)):
        edits = {}
        for field in fields:
            if field == "question":
                edits[field] = {"text": self.QUESTION, "spans": []}
            else:
                edits[field] = {
                    "text": self.EXPLANATION,
                    "spans": [
                        layout.text_semantics.select(
                            self.EXPLANATION, "false", "output", reading="natural"),
                        layout.text_semantics.select(
                            self.EXPLANATION, "bool", "cpp", reading="technical"),
                    ],
                }
        return layout.revise_ordinary_editorial(document, self.OTHER, edits)

    def test_real_question_revision_accepts_all_three_edges_and_publication_guard(self):
        import validar_json_programacion as contract

        original = deepcopy(self.original)
        reviewed = self.choice(original)
        current = self.ordinary(reviewed)
        for label, before, after in (
                ("A-B", original, reviewed), ("B-C", reviewed, current),
                ("A-C", original, current)):
            with self.subTest(edge=label):
                layout.validate_transition(before, after)
                self.assertEqual(layout.validate(original), layout.validate(after))
        self.assertEqual(self.original, original, "Authoring changed the input lesson")
        self.assertEqual(layout._blocks(reviewed)[OWNER], layout._blocks(current)[OWNER])
        for field in ("origin", "current", "family"):
            self.assertEqual(original["stateLayout"][field], current["stateLayout"][field])
        self.assertEqual(reviewed["stateLayout"]["choiceRevisions"],
                         current["stateLayout"]["choiceRevisions"])
        self.assertEqual(reviewed["textSemantics"]["rootFields"],
                         current["textSemantics"]["rootFields"])

        aligned = layout.align_for_previous_validation(original, current)
        self.assertEqual(original["pages"][7]["blocks"][2],
                         aligned["pages"][7]["blocks"][2])
        self.assertEqual(self.marker(aligned, OWNER, "/options/0"),
                         self.marker(original, OWNER, "/options/0"))
        self.assertEqual(self.QUESTION, aligned["pages"][6]["blocks"][1]["question"])
        layout.text_semantics.validate(aligned)
        path = "app/src/main/assets/content/es/" + RELATIVE
        old, new = layout._blocks(original)[self.OTHER], layout._blocks(current)[self.OTHER]
        self.assertTrue(contract.preserves_ordinary_explanation_revision(
            path, 6, 1, old, new, original, current))
        self.assertTrue(contract.preserves_previous_exercise(
            path, 6, 1, old, new, previous_document=original, current_document=current))

    def test_reverse_order_and_one_or_two_changed_fields(self):
        for fields in (("question",), ("explanation",), ("question", "explanation")):
            with self.subTest(fields=fields):
                ordinary_first = self.ordinary(self.original, fields)
                choice_last = self.choice(ordinary_first)
                choice_first = self.choice(self.original)
                ordinary_last = self.ordinary(choice_first, fields)
                for before, after in (
                        (self.original, ordinary_first), (ordinary_first, choice_last),
                        (self.original, choice_last), (choice_first, ordinary_last),
                        (self.original, ordinary_last)):
                    layout.validate_transition(before, after)
                self.assertEqual(choice_last["pages"], ordinary_last["pages"])
                self.assertEqual(choice_last["textSemantics"], ordinary_last["textSemantics"])
                self.assertEqual(choice_last["stateLayout"]["choiceRevisions"],
                                 ordinary_last["stateLayout"]["choiceRevisions"])
                # Histories reflect the actual publication order, not a merged proof.
                self.assertNotEqual(choice_last["stateLayout"]["editorial"],
                                    ordinary_last["stateLayout"]["editorial"])

    def test_several_revisions_other_owners_and_explicit_return_remain_authenticated(self):
        first = self.ordinary(self.choice(self.original))
        second = layout.revise_ordinary_editorial(first, self.OTHER, {
            "question": {"text": "¿Qué prueba observa el estado que queda después del rechazo?",
                         "spans": []}})
        third_text = layout._blocks(self.original)[self.THIRD]["explanation"] + (
            " La regla de unicidad debe comprobarse donde se administra la colección.")
        third = layout.revise_ordinary_editorial(second, self.THIRD, {
            "explanation": {"text": third_text, "spans": []}})
        several = layout.revise_choice_editorial(
            third, "block_0005_0004", 2,
            "Acceder siempre al elemento cero para mostrar algún dato.")
        for current in (first, second, third, several):
            layout.validate_transition(self.original, current)
            self.assertEqual(layout.validate(self.original), layout.validate(current))
        self.assertEqual(2, len(several["stateLayout"]["choiceRevisions"]))

        original_owner = layout._blocks(self.original)[self.OTHER]
        owner_history = [edge for edge in self.original["stateLayout"]["editorial"]
                         if edge["id"] == self.OTHER]
        restored = layout.revise_ordinary_editorial(several, self.OTHER, {
            "question": {"text": original_owner["question"],
                         "spans": deepcopy(self.marker(
                             self.original, self.OTHER, "/question")["spans"])},
            "returnTo": len(owner_history)})
        layout.validate_transition(several, restored)
        layout.validate_transition(self.original, restored)
        proof = restored["stateLayout"]["editorial"][-1]
        self.assertEqual(len(owner_history), proof["returnTo"]["revision"])
        self.assertEqual(layout._blocks(several)[self.OTHER],
                         proof["returnTo"]["previousBlock"])
        for mutation in ("owner", "prefix", "snapshot", "revision"):
            invalid = deepcopy(restored)
            edge = invalid["stateLayout"]["editorial"][-1]
            if mutation == "owner":
                edge["id"] = self.THIRD
            elif mutation == "prefix":
                edge["returnTo"]["historySha256"] = "0" * 64
            elif mutation == "snapshot":
                edge["returnTo"]["previousBlock"]["options"][0] = "Origen inventado"
            else:
                edge["returnTo"]["revision"] = 999
            with self.subTest(return_mutation=mutation), self.assertRaises(layout.LayoutError):
                layout.validate_transition(self.original, invalid)

    def test_previously_unmarked_field_or_first_overlay_can_be_authored(self):
        for mode in ("missing-field", "missing-overlay"):
            with self.subTest(mode=mode):
                original = deepcopy(self.original)
                if mode == "missing-field":
                    original["textSemantics"]["fields"] = [
                        field for field in original["textSemantics"]["fields"]
                        if (field["block"], field["path"]) != (self.OTHER, "/question")]
                else:
                    del original["textSemantics"]
                layout.validate(original)
                reviewed = self.choice(original)
                current = self.ordinary(reviewed)
                layout.validate_transition(original, current)
                self.assertEqual(self.OTHER, current["textSemantics"]["fields"][-1]["block"])
                self.assertEqual("/question", current["textSemantics"]["fields"][-1]["path"])
                reverse = self.choice(self.ordinary(original))
                layout.validate_transition(original, reverse)
                self.assertEqual(current["textSemantics"], reverse["textSemantics"])
                if mode == "missing-overlay":
                    self.assertEqual({"version", "fields"}, set(current["textSemantics"]))
                    self.assertEqual(1, len(current["textSemantics"]["fields"]))
                    self.assertIsNone(current["stateLayout"]["choiceRevisions"][0]["semanticBefore"])
                    reverse_fields = self.ordinary(reviewed, ("explanation", "question"))
                    layout.validate_transition(original, reverse_fields)
                    self.assertEqual(["/explanation", "/question"],
                                     [field["path"] for field in reverse_fields["textSemantics"]["fields"]])
                    for mutation in ("root", "unrelated", "owner"):
                        invalid = deepcopy(current)
                        if mutation == "root":
                            invalid["textSemantics"]["rootFields"] = []
                        elif mutation == "unrelated":
                            invalid["textSemantics"]["fields"].append(
                                layout.text_semantics.make_field(
                                    invalid, OWNER, "/question", []))
                        else:
                            invalid["textSemantics"]["fields"] = [
                                layout.text_semantics.make_field(
                                    invalid, self.THIRD, "/question", [])]
                        layout.validate(invalid)
                        with self.subTest(extra=mutation), self.assertRaises(layout.LayoutError):
                            layout.validate_transition(original, invalid)

    def test_valid_unrelated_mark_changes_are_not_covered_by_the_new_proof(self):
        current = self.ordinary(self.choice(self.original))
        for identity, path in (
                (self.OTHER, "/explanation"), (self.OTHER, "/options/0"),
                (self.OTHER, "/reviewFeedback/0"), (self.THIRD, "/question"),
                (OWNER, "/question")):
            invalid = deepcopy(current)
            marker = self.marker(invalid, identity, path)
            self.assertTrue(marker["spans"])
            marker["spans"][0]["role"] = "prose"
            # The descriptor is individually valid. The composed transition
            # must still preserve the exact unchanged source's authored marks.
            layout.validate(invalid)
            with self.subTest(owner=identity, path=path), self.assertRaisesRegex(
                    layout.LayoutError, "unrelated text marker"):
                layout.validate_transition(self.original, invalid)
        for mutation in ("roots", "order", "version"):
            invalid = deepcopy(current)
            if mutation == "roots":
                del invalid["textSemantics"]["rootFields"]
            elif mutation == "order":
                invalid["textSemantics"]["fields"].reverse()
            else:
                invalid["textSemantics"]["version"] = 2
            with self.subTest(metadata=mutation), self.assertRaises(layout.LayoutError):
                layout.validate_transition(self.original, invalid)

    def test_changed_field_needs_current_descriptor_and_preserves_nonempty_spans(self):
        for fields, path in ((("question",), "/question"), (("explanation",), "/explanation")):
            current = self.ordinary(self.choice(self.original), fields)
            mutations = ["missing", "stale", "wrong-owner"]
            if path == "/explanation":
                mutations.append("empty-spans")
            for mutation in mutations:
                invalid = deepcopy(current)
                marker = self.marker(invalid, self.OTHER, path)
                if mutation == "missing":
                    invalid["textSemantics"]["fields"].remove(marker)
                elif mutation == "stale":
                    marker["sourceSha256"] = self.marker(
                        self.original, self.OTHER, path)["sourceSha256"]
                elif mutation == "wrong-owner":
                    marker["block"] = self.THIRD
                else:
                    marker["spans"] = []
                    layout.validate(invalid)
                with self.subTest(field=path, mutation=mutation), self.assertRaises(layout.LayoutError):
                    layout.validate_transition(self.original, invalid)

    def test_complete_payload_archive_and_proof_are_still_required(self):
        current = self.ordinary(self.choice(self.original))
        for mutation in ("answer", "option", "choice-owner", "archive", "proof"):
            invalid = deepcopy(current)
            if mutation == "answer":
                layout._blocks(invalid)[self.OTHER]["correctIndex"] = 1
                invalid["stateLayout"]["editorial"][-1]["afterSha256"] = (
                    layout.fingerprint(layout._blocks(invalid)[self.OTHER]))
            elif mutation == "option":
                layout._blocks(invalid)[self.OTHER]["options"][1] = "Una opción distinta"
                self.marker(invalid, self.OTHER, "/options/1").update(
                    layout.text_semantics.make_field(invalid, self.OTHER, "/options/1", []))
                invalid["stateLayout"]["editorial"][-1]["afterSha256"] = (
                    layout.fingerprint(layout._blocks(invalid)[self.OTHER]))
            elif mutation == "choice-owner":
                layout._blocks(invalid)[OWNER]["question"] += " Una pregunta distinta."
            elif mutation == "archive":
                invalid["stateLayout"]["choiceRevisions"][0]["block"]["question"] += " Inventada."
            else:
                invalid["stateLayout"]["editorial"].pop()
            with self.subTest(mutation=mutation), self.assertRaises(layout.LayoutError):
                layout.validate_transition(self.original, invalid)

    def test_ordinary_predicate_requires_supported_existing_fields_and_full_payload(self):
        original = deepcopy(layout._blocks(self.original)[self.THIRD])
        current = dict(original, question="¿Qué ocurre al copiar el pedido?")
        self.assertEqual(("question",), layout.ordinary_editorial_fields(original, current))
        self.assertEqual((), layout.ordinary_editorial_fields(original, original))
        for mutation in ("answer", "boolean-answer", "feedback", "missing", "empty", "type"):
            invalid = deepcopy(current)
            before = deepcopy(original)
            if mutation == "answer":
                invalid["correctIndex"] = 2
            elif mutation == "boolean-answer":
                self.assertEqual(1, before["correctIndex"])
                invalid["correctIndex"] = True
            elif mutation == "feedback":
                invalid["reviewFeedback"][0] = "Otra guía"
            elif mutation == "missing":
                del before["question"]
            elif mutation == "empty":
                invalid["question"] = " "
            else:
                before["type"] = invalid["type"] = "paragraph"
            with self.subTest(mutation=mutation):
                self.assertEqual((), layout.ordinary_editorial_fields(before, invalid))


class GeneralQuestionEditorialTests(unittest.TestCase):
    """The author may clarify a question without changing its expected answer."""

    @staticmethod
    def lesson(relative: str):
        family = history.historical_family(relative)
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


class MissingOrdinaryExplanationTests(unittest.TestCase):
    """The general authoring operation must work on moved U1L4 activities."""

    def test_u1l4_missing_explanation_survives_reordered_origin(self):
        import validar_json_programacion as contract

        relative = ("programming/cpp_fundamentals/programas_y_ejecucion/"
                    "lesson_fuente_compilacion_y_diagnostico.json")
        path = "app/src/main/assets/content/es/" + relative
        before = json.loads((history.CONTENT / "es" / relative).read_text(encoding="utf-8"))
        identity = "block_0004_0001"
        mapping = layout.validate(before)
        location = next(
            (p, b) for (p, b) in mapping
            if before["stateLayout"]["current"][p]["blocks"][b] == identity)
        p, b = location
        origin_p, origin_b = mapping[location]
        self.assertEqual((origin_p, origin_b), (4, 1))
        old = deepcopy(before["pages"][p]["blocks"][b])
        self.assertEqual((old["type"], old["id"]), ("drag_fill_code", "piezas_estructura"))
        self.assertNotIn("explanation", old)
        self.assertFalse(any(field["block"] == identity and field["path"] == "/explanation"
                             for field in before["textSemantics"]["fields"]))

        explanation = (
            "La ficha main completa el nombre de la función. "
            "El operador de salida envía el texto a la consola y "
            "el punto y coma termina esa instrucción.")
        after = layout.revise_ordinary_editorial(
            before, identity, {"explanation": {"text": explanation, "spans": []}})
        layout.validate_transition(before, after)
        self.assertEqual(mapping, layout.validate(after))
        self.assertEqual(before["stateLayout"]["origin"], after["stateLayout"]["origin"])
        self.assertEqual(before["stateLayout"]["current"], after["stateLayout"]["current"])
        self.assertEqual(before["stateLayout"]["family"], after["stateLayout"]["family"])
        self.assertEqual(old, before["pages"][p]["blocks"][b])
        updated = after["pages"][p]["blocks"][b]
        self.assertEqual(updated, dict(old, explanation=explanation))
        self.assertEqual(
            [(field["path"], field["spans"]) for field in after["textSemantics"]["fields"]
             if field["block"] == identity and field["path"] == "/explanation"],
            [("/explanation", [])])
        self.assertEqual(len(after["stateLayout"]["editorial"]),
                         len(before["stateLayout"]["editorial"]) + 1)

        aligned = layout.align_for_previous_validation(before, after)
        current = aligned["pages"][p]["blocks"][b]
        self.assertEqual(current, updated)
        self.assertTrue(contract.preserves_ordinary_explanation_revision(
            path, origin_p, origin_b, old, current, before, after))
        self.assertTrue(contract.preserves_previous_exercise(
            path, origin_p, origin_b, old, current,
            previous_document=before, current_document=after))
        self.assertEqual(old["correctOrder"], current["correctOrder"])
        self.assertEqual(old["choices"], current["choices"])

        changed_answer = deepcopy(after)
        changed_answer["pages"][p]["blocks"][b]["correctOrder"] = [1, 0, 2]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, changed_answer)
        without_proof = deepcopy(after)
        without_proof["stateLayout"]["editorial"].pop()
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, without_proof)
        without_mark = deepcopy(after)
        without_mark["textSemantics"]["fields"] = [
            field for field in without_mark["textSemantics"]["fields"]
            if (field["block"], field["path"]) != (identity, "/explanation")]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, without_mark)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] in EXPORT_MODES:
        output = Path(sys.argv[2])
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(fixture_bytes(sys.argv[1]))
    else:
        unittest.main()

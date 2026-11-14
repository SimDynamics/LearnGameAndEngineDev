"""Authenticated math prompt edits keep the original grading and reading owner."""
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
from functools import lru_cache
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import math_prompt_fixture_source as fixtures
import stable_state_layout as layout
import validar_json_programacion as contract


COURSE = "programming_practice_tools"
OWNER = fixtures.OWNER


@lru_cache(maxsize=1)
def cached_candidate():
    return fixtures.candidate()


def candidate():
    return deepcopy(cached_candidate())


def block(document):
    return layout._blocks(document)[OWNER]


def marker(document, path="/prompt"):
    return next(item for item in document["textSemantics"]["fields"]
                if item["block"] == OWNER and item["path"] == path)


def archive(document):
    return next(item for item in document["stateLayout"]["mathTextRevisions"]
                if item["id"] == OWNER)


def repoint_last_edge(document):
    edge = next(item for item in reversed(document["stateLayout"]["editorial"])
                if item["id"] == OWNER)
    edge["afterSha256"] = layout.fingerprint(block(document))


def preserved(before, after, **overrides):
    arguments = dict(relative=fixtures.RELATIVE, pn=2, bn=5,
                     old=block(before), current=block(after),
                     previous_document=before, current_document=after,
                     course_id=COURSE)
    arguments.update(overrides)
    return contract.preserves_previous_exercise(**arguments)


def replace_prompt_field(document, prompt, spans):
    block(document)["prompt"] = prompt
    old = marker(document)
    updated = layout.text_semantics.make_field(document, OWNER, "/prompt", spans)
    old.clear()
    old.update(updated)


class MathPromptRevisionTests(unittest.TestCase):
    def test_pinned_source_is_independent_of_candidate_and_rejects_changed_bytes(self):
        payload = fixtures.historical_bytes()
        self.assertEqual(fixtures.SOURCE_SHA256, hashlib.sha256(payload).hexdigest())
        with self.assertRaises(ValueError):
            fixtures.verify_bytes(payload + b" ")
        before, after = candidate()
        self.assertEqual(before, json.loads(payload))
        self.assertEqual(fixtures.PROMPT, block(after)["prompt"])
        self.assertEqual(payload, fixtures.historical_bytes())

    def test_real_assetcheck_prompt_keeps_all_other_fields_and_historical_addresses(self):
        before, after = candidate()
        self.assertEqual(dict(block(before), prompt=fixtures.PROMPT), block(after))
        self.assertEqual({"a": "cero", "b": "uno", "c": "dos_vacia", "d": "dos"},
                         block(after)["responses"][0]["validator"]["expected"])
        self.assertEqual(layout.validate(before), layout.validate(after))
        self.assertEqual(layout.lineage(before), layout.lineage(after))
        for field in ("origin", "current", "family", "additions"):
            self.assertEqual(before["stateLayout"][field], after["stateLayout"][field])
        self.assertTrue(preserved(before, after))
        layout.validate_transition(before, after)
        # There is no global exception for the word 'prompt': its ordinary
        # block fingerprint still changes; only the contextual archive projects.
        self.assertNotEqual(layout.state_fingerprint(block(before)),
                            layout.state_fingerprint(block(after)))
        self.assertEqual(layout.original_projection(before), layout.original_projection(after))

    def test_archive_preserves_original_json_order_and_exact_semantic_source(self):
        before, after = candidate()
        entry = archive(after)
        self.assertEqual({"id", "field", "rawJson", "semanticBefore"}, set(entry))
        self.assertEqual("/prompt", entry["field"])
        self.assertEqual(json.dumps(block(before), ensure_ascii=False,
                                    separators=(",", ":"), allow_nan=False), entry["rawJson"])
        self.assertEqual(list(block(before)), list(json.loads(entry["rawJson"])))
        self.assertEqual(marker(before), entry["semanticBefore"])
        self.assertEqual(before["stateLayout"]["editorial"],
                         after["stateLayout"]["editorial"][:-1])
        self.assertEqual({"id": OWNER,
                          "beforeSha256": layout.fingerprint(block(before)),
                          "afterSha256": layout.fingerprint(block(after))},
                         after["stateLayout"]["editorial"][-1])

    def test_current_prompt_has_authored_utf16_spans_and_other_markers_are_untouched(self):
        before, after = candidate()
        self.assertEqual(fixtures.authored_spans(), marker(after)["spans"])
        self.assertEqual(hashlib.sha256(fixtures.PROMPT.encode()).hexdigest(),
                         marker(after)["sourceSha256"])
        validated = layout.text_semantics.validate(after)
        self.assertEqual(fixtures.PROMPT, validated[OWNER]["/prompt"]["source"])
        old_fields = {(item["block"], item["path"]): item for item in before["textSemantics"]["fields"]}
        new_fields = {(item["block"], item["path"]): item for item in after["textSemantics"]["fields"]}
        self.assertEqual(old_fields.keys(), new_fields.keys())
        for key in old_fields:
            if key != (OWNER, "/prompt"):
                self.assertEqual(old_fields[key], new_fields[key])

    def test_second_unregistered_wording_keeps_the_same_evaluation_archive(self):
        before, after = candidate()
        prompt = "Cuenta las líneas físicas de cada secuencia de bytes; LF y CR representan terminadores."
        spans = [layout.text_semantics.select(prompt, token, "data", reading="natural")
                 for token in ("LF", "CR")]
        revised = layout.revise_math_prompt_editorial(after, OWNER, prompt, spans=spans)
        self.assertEqual(after["stateLayout"]["mathTextRevisions"],
                         revised["stateLayout"]["mathTextRevisions"])
        self.assertEqual(after["stateLayout"]["editorial"],
                         revised["stateLayout"]["editorial"][:-1])
        self.assertTrue(preserved(after, revised))
        self.assertTrue(preserved(before, revised))
        self.assertEqual(layout.original_projection(before), layout.original_projection(revised))
        layout.validate_transition(after, revised)

    def test_new_owner_without_previous_semantic_overlay_uses_general_capability(self):
        published, _ = candidate()
        raw = {"id": "programming_practice_tools__qa__math_prompt",
               "title": "QA, no curricular lesson",
               "pages": [{"blocks": [deepcopy(block(published))]}]}
        raw["pages"][0]["blocks"][0]["id"] = "qa_matching"
        before = layout.prepare_v2(raw, "es", {"es": raw})
        after = layout.revise_math_prompt_editorial(
            before, "block_0000_0000", "Asocia cada entrada con su recuento físico.", spans=[])
        self.assertIsNone(after["stateLayout"]["mathTextRevisions"][0]["semanticBefore"])
        self.assertEqual(layout.original_projection(before), layout.original_projection(after))
        layout.validate_transition(before, after)
        self.assertTrue(contract.preserves_previous_exercise(
            "app/src/main/assets/content/es/programming/programming_practice_tools/qa/lesson_math_prompt.json",
            0, 0, before["pages"][0]["blocks"][0], after["pages"][0]["blocks"][0],
            previous_document=before, current_document=after, course_id=COURSE))

    def test_unarchived_prompt_change_is_not_a_state_compatible_edit(self):
        before, _ = candidate()
        changed = deepcopy(before)
        replace_prompt_field(changed, fixtures.PROMPT, fixtures.authored_spans())
        changed["stateLayout"]["editorial"].append({
            "id": OWNER, "beforeSha256": layout.fingerprint(block(before)),
            "afterSha256": layout.fingerprint(block(changed))})
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed)
        self.assertFalse(preserved(before, changed))

    def test_complete_grading_payload_cannot_borrow_the_prompt_authorization(self):
        before, after = candidate()
        variants = {
            "response_id": lambda b: b["responses"][0].__setitem__("id", "otra_respuesta"),
            "source_id": lambda b: b["responses"][0]["sources"][0].__setitem__("id", "otra_fuente"),
            "source_label": lambda b: b["responses"][0]["sources"][0].__setitem__("label", "Archivo cambiado"),
            "target_label": lambda b: b["responses"][0]["targets"][0].__setitem__("label", "Una línea"),
            "expected_pair": lambda b: b["responses"][0]["validator"]["expected"].__setitem__("a", "uno"),
            "validator": lambda b: b["responses"][0]["validator"].__setitem__("type", "custom"),
            "feedback": lambda b: b["feedback"].__setitem__("correct", "Otra explicación"),
            "representation": lambda b: b["representations"].append({"type": "math_text", "text": "Dato nuevo"}),
            "tool": lambda b: b["tools"].append({"type": "calculator"}),
            "activity_id": lambda b: b.__setitem__("id", "actividad_ajena"),
            "schema": lambda b: b.__setitem__("schemaVersion", 2),
            "new_property": lambda b: b.__setitem__("una_propiedad", True),
        }
        for name, mutate in variants.items():
            changed = deepcopy(after)
            mutate(block(changed))
            repoint_last_edge(changed)
            with self.subTest(field=name):
                with self.assertRaises(layout.LayoutError):
                    layout.validate(changed)
                self.assertFalse(preserved(before, changed))

    def test_json_key_order_and_numeric_spelling_cannot_change_the_evaluation_contract(self):
        before, after = candidate()
        for name in ("reordered_root", "reordered_expected", "float_schema"):
            changed = deepcopy(after)
            current = block(changed)
            if name == "reordered_root":
                ordered = dict(reversed(list(current.items())))
                current.clear()
                current.update(ordered)
            elif name == "reordered_expected":
                expected = current["responses"][0]["validator"]["expected"]
                ordered = dict(reversed(list(expected.items())))
                expected.clear()
                expected.update(ordered)
            else:
                current["schemaVersion"] = 1.0
            repoint_last_edge(changed)
            with self.subTest(variant=name):
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(before, changed)
                self.assertFalse(preserved(before, changed))

    def test_archive_cannot_be_missing_duplicated_reassigned_or_rewritten(self):
        before, after = candidate()
        mutations = {
            "missing": lambda d: d["stateLayout"].pop("mathTextRevisions"),
            "duplicate": lambda d: d["stateLayout"]["mathTextRevisions"].append(deepcopy(archive(d))),
            "owner": lambda d: archive(d).__setitem__("id", "block_0002_0003"),
            "field": lambda d: archive(d).__setitem__("field", "/feedback/correct"),
            "no_edge": lambda d: d["stateLayout"]["editorial"].pop(),
            "false_source": lambda d: archive(d).__setitem__("rawJson", json.dumps(dict(block(before), prompt="Origen inventado"))),
            "false_descriptor": lambda d: archive(d)["semanticBefore"].__setitem__("sourceSha256", "0" * 64),
            "duplicate_keys": lambda d: archive(d).__setitem__("rawJson", '{"type":"math_puzzle",' + archive(d)["rawJson"][1:]),
            "nonfinite": lambda d: archive(d).__setitem__("rawJson", archive(d)["rawJson"].replace('"schemaVersion":1', '"schemaVersion":NaN')),
        }
        for name, mutate in mutations.items():
            changed = deepcopy(after)
            mutate(changed)
            with self.subTest(variant=name):
                with self.assertRaises(layout.LayoutError):
                    layout.validate(changed)
                self.assertFalse(preserved(before, changed))
        changed = deepcopy(after)
        archive(changed)["rawJson"] += "\n"
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(after, changed)

    def test_stale_missing_or_invalid_current_descriptor_is_rejected(self):
        before, after = candidate()
        mutations = {
            "old_sha": lambda d: marker(d).__setitem__("sourceSha256", marker(before)["sourceSha256"]),
            "offset": lambda d: marker(d)["spans"][0].__setitem__("start", 0),
            "missing": lambda d: d["textSemantics"]["fields"].remove(marker(d)),
        }
        for name, mutate in mutations.items():
            changed = deepcopy(after)
            mutate(changed)
            with self.subTest(variant=name):
                with self.assertRaises(layout.LayoutError):
                    layout.validate(changed)
                self.assertFalse(preserved(before, changed))
        # A compatible explanation does not repaint an unchanged response label.
        changed = deepcopy(after)
        path = "/responses/0/sources/0/label"
        label = block(changed)["responses"][0]["sources"][0]["label"]
        marker(changed, path)["spans"] = [
            layout.text_semantics.select(label, "vacío", "data", reading="natural")]
        self.assertFalse(preserved(before, changed))
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, changed)

    def test_contextual_guard_requires_the_exact_lesson_path_owner_and_previous_block(self):
        before, after = candidate()
        self.assertFalse(preserved(before, after, previous_document=None))
        self.assertFalse(preserved(before, after, current_document=None))
        for coordinates in ((-1, 5), (2, -1), (True, 5), (2, 5.0), (0, 0), (2, 4)):
            with self.subTest(coordinates=coordinates):
                self.assertFalse(preserved(before, after, pn=coordinates[0], bn=coordinates[1]))
        paths = [
            fixtures.RELATIVE.replace("lesson_leer_manifiesto_y_publicar_informe", "lesson_otro"),
            fixtures.RELATIVE.replace("proyecto_assetcheck/", "otra_unidad/"),
            fixtures.RELATIVE.replace("/es/", "/en/"),
            fixtures.RELATIVE.replace("/proyecto_assetcheck/", "/proyecto_assetcheck/../proyecto_assetcheck/"),
        ]
        for relative in paths:
            with self.subTest(path=relative):
                self.assertFalse(preserved(before, after, relative=relative))
        detached = deepcopy(block(before))
        detached["feedback"]["incorrect"] = "Bloque anterior ajeno"
        self.assertFalse(preserved(before, after, old=detached))
        self.assertFalse(contract.preserves_math_prompt_editorial_revision(
            fixtures.RELATIVE, 2, 5, block(before), block(after),
            previous_document=before, current_document=after, course_id="cpp_fundamentals"))

    def test_identity_origin_and_family_cannot_be_replaced(self):
        before, after = candidate()
        for field in ("identity", "origin", "family"):
            changed = deepcopy(after)
            if field == "identity":
                changed["id"] = "programming_practice_tools__qa__otra_leccion"
            elif field == "origin":
                changed["stateLayout"]["origin"][2]["blocks"][5]["sha256"] = layout.fingerprint(block(changed))
            else:
                changed["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
            with self.subTest(field=field):
                self.assertFalse(preserved(before, changed))
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(before, changed)

    def test_authoring_rejects_blank_unchanged_unowned_or_unmarked_text(self):
        before, after = candidate()
        for prompt in ("", "   ", block(before)["prompt"], None):
            with self.subTest(prompt=prompt), self.assertRaises(layout.LayoutError):
                layout.revise_math_prompt_editorial(before, OWNER, prompt, spans=[])
        for identity in ("block_9999_9999", "block_0002_0003"):
            with self.subTest(identity=identity), self.assertRaises(layout.LayoutError):
                layout.revise_math_prompt_editorial(before, identity, fixtures.PROMPT, spans=[])
        with self.assertRaises(layout.LayoutError):
            layout.revise_math_prompt_editorial(before, OWNER, fixtures.PROMPT, spans=None)
        # Previously highlighted prompt text cannot silently lose every mark.
        with self.assertRaises(layout.LayoutError):
            layout.revise_math_prompt_editorial(after, OWNER, "Otro enunciado QA.", spans=[])

    def test_explicit_return_keeps_the_original_archive_and_complete_editorial_chain(self):
        before, _ = candidate()
        after = layout.revise_math_prompt_editorial(before, OWNER, fixtures.PROMPT, spans=[])
        with self.assertRaises(layout.LayoutError):
            layout.revise_math_prompt_editorial(after, OWNER, block(before)["prompt"], spans=[])
        restored = layout.revise_math_prompt_editorial(
            after, OWNER, block(before)["prompt"], spans=[], return_to=0)
        self.assertEqual(block(before), block(restored))
        self.assertEqual(archive(after), archive(restored))
        self.assertEqual(0, restored["stateLayout"]["editorial"][-1]["returnTo"]["revision"])
        layout.validate_transition(after, restored)
        self.assertTrue(preserved(after, restored))
        self.assertEqual(layout.original_projection(before), layout.original_projection(restored))

    def test_public_cli_creates_a_new_proposal_and_never_overwrites_the_source(self):
        before, expected = candidate()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.json"
            proposal = root / "proposal.json"
            spans = root / "spans.json"
            source.write_text(json.dumps(before, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            spans.write_text(json.dumps(fixtures.authored_spans(), ensure_ascii=False), encoding="utf-8")
            original_bytes = source.read_bytes()
            command = ["stable_state_layout.py", "revise-math-prompt-editorial", str(source),
                       "--previous", str(source), "--identity", OWNER,
                       "--replacement", fixtures.PROMPT, "--semantic-spans", str(spans),
                       "--output", str(proposal)]
            def invoke(arguments):
                with patch.object(sys, "argv", arguments), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    layout.main()
            invoke(command)
            self.assertEqual(expected, json.loads(proposal.read_text(encoding="utf-8")))
            self.assertEqual(original_bytes, source.read_bytes())
            with self.assertRaises(SystemExit) as stopped:
                invoke(command)
            self.assertEqual(1, stopped.exception.code)
            self.assertEqual(expected, json.loads(proposal.read_text(encoding="utf-8")))
            rejected = root / "rejected.json"
            invalid = command.copy()
            invalid[invalid.index("--identity") + 1] = "block_9999_9999"
            invalid[invalid.index("--output") + 1] = str(rejected)
            with self.assertRaises(SystemExit):
                invoke(invalid)
            self.assertFalse(rejected.exists())
            self.assertEqual(original_bytes, source.read_bytes())

    def test_archive_survives_whitespace_roundtrip_without_curricular_writes(self):
        before, after = candidate()
        encoded = json.dumps(after, ensure_ascii=False, indent=4, allow_nan=False)
        for text in (encoded, encoded.replace("\n", "\r\n")):
            restored = json.loads(text)
            self.assertEqual(archive(after)["rawJson"], archive(restored)["rawJson"])
            self.assertTrue(preserved(before, restored))
            layout.validate_transition(before, restored)
        with tempfile.TemporaryDirectory() as directory:
            outputs = fixtures.export_fixtures(Path(directory))
            self.assertEqual(("assetcheck-prompt-before.json", "assetcheck-prompt-after.json"),
                             tuple(path.name for path in outputs))
            self.assertEqual(before, json.loads(outputs[0].read_text(encoding="utf-8")))
            self.assertEqual(after, json.loads(outputs[1].read_text(encoding="utf-8")))
            self.assertEqual(fixtures.SOURCE_SHA256,
                             hashlib.sha256(fixtures.historical_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()

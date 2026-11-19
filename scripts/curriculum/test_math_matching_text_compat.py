"""Real U5L13 math matching text authoring, without writing curricular assets."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import math_matching_text_compat as matching
import stable_state_layout as state
import text_semantics


PATH = (Path(__file__).resolve().parents[2] /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json")
OWNER = "block_0007_0007"
OLD = "maxima"
NEW = "frecuencia_maxima_encontrada"
PROMPT = "/prompt"
FEEDBACK = "/feedback/correct"
CHANGED_SOURCES = (2, 3)


class RealFrequencyMatchingEditorial(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_bytes = PATH.read_bytes()
        cls.lesson = json.loads(cls.original_bytes)
        state.validate(cls.lesson)
        cls.original = state._blocks(cls.lesson)[OWNER]
        assert cls.original["type"] == "math_puzzle"
        assert cls.original["responses"][0]["type"] == "math_matching_drag"
        cls.old_expected = deepcopy(
            cls.original["responses"][0]["validator"]["expected"])
        cls.old_sources = [
            item["id"] for item in cls.original["responses"][0]["sources"]]
        cls.old_targets = [
            item["id"] for item in cls.original["responses"][0]["targets"]]
        cls.original_marks = {
            item["path"]: item
            for item in cls.lesson["textSemantics"]["fields"]
            if item["block"] == OWNER
        }

    def revisions(self):
        before = self.original
        targets = {
            PROMPT: before["prompt"].replace(OLD, NEW),
            FEEDBACK: before["feedback"]["correct"].replace(OLD, NEW),
        }
        for index in CHANGED_SOURCES:
            for suffix in ("label", "content/0/text"):
                path = f"/responses/0/sources/{index}/{suffix}"
                targets[path] = matching._get(before, path).replace(OLD, NEW)

        desired = deepcopy(before)
        for path, value in targets.items():
            matching._set(desired, path, value)
        spans = {}
        for path in targets:
            if path not in text_semantics.public_text_fields(before):
                continue
            source = text_semantics.public_text_fields(desired)[path]
            previous = self.original_marks[path]["spans"]
            # These are explicit positions selected from each *new* text.
            # This fixture does not authorize Work to rewrite its real marks.
            spans[path] = [
                text_semantics.select(
                    source, entry["text"].replace(OLD, NEW),
                    entry["role"], reading=entry.get("reading", "auto"))
                for entry in previous
            ]
        return targets, spans

    def test_real_matching_review_preserves_card_and_grading_owners(self):
        targets, spans = self.revisions()
        candidate, proof = matching.preview_math_matching_text_review(
            self.lesson, OWNER, targets, spans)
        self.assertEqual(proof["paths"], sorted(targets))
        self.assertEqual(proof["beforeSha256"], state.fingerprint(self.original))
        revised = state._blocks(candidate)[OWNER]
        self.assertEqual(proof["afterSha256"], state.fingerprint(revised))
        self.assertTrue(matching.verify_matching_preview(self.lesson, candidate, proof))
        response = revised["responses"][0]
        self.assertEqual(response["validator"]["expected"], self.old_expected)
        self.assertEqual([item["id"] for item in response["sources"]], self.old_sources)
        self.assertEqual([item["id"] for item in response["targets"]], self.old_targets)
        for index in CHANGED_SOURCES:
            self.assertEqual(response["sources"][index]["label"],
                             response["sources"][index]["content"][0]["text"])
            self.assertIn(NEW,response["sources"][index]["label"])
        self.assertIn(NEW,revised["prompt"])
        self.assertIn(NEW,revised["feedback"]["correct"])
        self.assertEqual(
            state._blocks(candidate)["block_0007_0002"],
            state._blocks(self.lesson)["block_0007_0002"])
        self.assertEqual(candidate["stateLayout"]["origin"],
                         self.lesson["stateLayout"]["origin"])
        self.assertEqual(candidate["stateLayout"]["additions"],
                         self.lesson["stateLayout"]["additions"])
        self.assertEqual(len(candidate["stateLayout"]["editorial"]),
                         len(self.lesson["stateLayout"]["editorial"]) + 1)
        self.assertEqual(PATH.read_bytes(),self.original_bytes)

    def test_current_loaders_still_reject_unpublished_matching_journal(self):
        targets, spans = self.revisions()
        draft, proof = matching.preview_math_matching_text_review(
            self.lesson, OWNER, targets, spans)
        with self.assertRaises(state.LayoutError):
            state.validate(draft)
        self.assertTrue(matching.verify_matching_preview(self.lesson,draft,proof))

    def test_matching_source_label_must_match_its_text(self):
        targets, _ = self.revisions()
        after = deepcopy(self.original)
        for path, value in targets.items():
            matching._set(after,path,value)
        after["responses"][0]["sources"][2]["label"] = "Etiquetas de texto"
        with self.assertRaisesRegex(matching.MatchingTextError,
                                    "source text disagree"):
            matching.require_matching_text_only(
                self.original,after,sorted(targets))

    def test_undeclared_answer_and_order_changes_are_rejected(self):
        targets, _ = self.revisions()
        revised = deepcopy(self.original)
        for path,value in targets.items():
            matching._set(revised,path,value)
        for name, mutate in (
            ("mapping",lambda d: d["responses"][0]["validator"]["expected"].update(
                {"supera_maximo":"conservar_empate"})),
            ("source ID",lambda d: d["responses"][0]["sources"][2].update(
                {"id":"invented_owner"})),
            ("target label",lambda d: d["responses"][0]["targets"][0].update(
                {"label":"changed outside the declared scope"})),
            ("source order",lambda d: d["responses"][0]["sources"].reverse()),
            ("extra response",lambda d: d["responses"].append(
                deepcopy(d["responses"][0]))),
        ):
            with self.subTest(name=name):
                forged=deepcopy(revised)
                mutate(forged)
                with self.assertRaises(matching.MatchingTextError):
                    matching.require_matching_text_only(
                        self.original,forged,sorted(targets))

    def test_forged_or_missing_semantics_are_rejected(self):
        targets, spans = self.revisions()
        self.assertTrue(spans[PROMPT])
        for path in (PROMPT, FEEDBACK,
                     "/responses/0/sources/2/content/0/text"):
            bad=deepcopy(spans)
            bad.pop(path)
            with self.subTest(path=path):
                with self.assertRaises(matching.MatchingTextError):
                    matching.preview_math_matching_text_review(
                        self.lesson,OWNER,targets,bad)
        before, proof = matching.preview_math_matching_text_review(
            self.lesson,OWNER,targets,spans)
        for field,value in (
            ("beforeSha256","0"*64),
            ("afterSha256","0"*64),
            ("historySha256","0"*64),
            ("editorialIndex",999),
            ("paths",["/prompt"]),
            ("rawJson",'{"type":"math_puzzle","prompt":"forged"}'),
        ):
            altered=deepcopy(proof);altered[field]=value
            with self.subTest(field=field):
                with self.assertRaises(matching.MatchingTextError):
                    matching.verify_matching_preview(self.lesson,before,altered)
        replaced=deepcopy(before)
        for field in replaced["textSemantics"]["fields"]:
            if field["block"]==OWNER and field["path"]==PROMPT:
                field["sourceSha256"]="0"*64
        with self.assertRaises(matching.MatchingTextError):
            matching.verify_matching_preview(self.lesson,replaced,proof)

    def test_only_supported_text_and_matching_type_can_be_reviewed(self):
        targets, spans = self.revisions()
        for invalid in (
            {"/responses/0/validator/expected/supera_maximo":"bad"},
            {"/responses/0/sources/2/id":"other"},
            {"/responses/0/sources/2/label":""},
            {"/responses/0/sources/2/content/0/text":"value without card label"},
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(matching.MatchingTextError):
                    matching.preview_math_matching_text_review(
                        self.lesson,OWNER,invalid,{})
        renamed=deepcopy(self.lesson)
        for page,record in zip(
                renamed["pages"],renamed["stateLayout"]["current"]):
            for identity,block in zip(record["blocks"],page["blocks"]):
                if identity==OWNER:
                    block["responses"][0]["type"]="math_numeric_input"
        with self.assertRaises((matching.MatchingTextError,state.LayoutError)):
            matching.preview_math_matching_text_review(renamed,OWNER,targets,spans)
        self.assertEqual(PATH.read_bytes(),self.original_bytes)


if __name__=="__main__":
    unittest.main()

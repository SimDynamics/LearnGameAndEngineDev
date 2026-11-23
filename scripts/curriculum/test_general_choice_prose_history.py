"""A published option correction can receive later independent prose reviews."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

import general_choice_prose_fixture_source as fixtures
import stable_state_layout as state
import validar_json_programacion as curriculum


class GeneralChoiceProseHistoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.published, cls.question, cls.final = fixtures.publication_chain()

    def test_each_immediate_predecessor_and_skipped_publication_are_valid(self):
        for label, before, after in (("question", self.published, self.question),
                                     ("explanation", self.question, self.final),
                                     ("both", self.published, self.final)):
            with self.subTest(stage=label):
                state.validate_transition(before, after)
                self.assertEqual(state.validate(before), state.validate(after))
                self.assertTrue(curriculum.preserves_previous_exercise(
                    fixtures.PUBLISHED_RELATIVE, 6, 2,
                    fixtures.owner(before), fixtures.owner(after),
                    previous_document=before, current_document=after))
                state.validate(json.loads(json.dumps(after, ensure_ascii=False)))

    def test_first_option_edge_stays_exact_while_the_prose_head_advances(self):
        first = fixtures.owner_edges(self.published)
        self.assertEqual(len(first), 1)
        for document, count in ((self.question, 2), (self.final, 3)):
            edges = fixtures.owner_edges(document)
            self.assertEqual(len(edges), count)
            self.assertEqual(edges[:1], first)
            self.assertNotEqual(first[0]["afterSha256"], edges[-1]["afterSha256"])
            self.assertFalse(any(edge["beforeSha256"] == first[0]["beforeSha256"]
                                 and edge["afterSha256"] == edges[-1]["afterSha256"]
                                 for edge in edges))
            self.assertEqual(document["stateLayout"]["choiceRevisions"],
                             self.published["stateLayout"]["choiceRevisions"])

    def test_every_answer_source_and_other_field_remains_published(self):
        before = deepcopy(self.published)
        expected = deepcopy(before)
        fixtures.owner(expected)["question"] = fixtures.QUESTION
        fixtures.owner(expected)["explanation"] = fixtures.EXPLANATION
        self.assertEqual(expected["pages"], self.final["pages"])
        for field in ("origin", "current", "family", "additions", "retired",
                      "retirementHistory", "sourceFormatRevisions", "choiceRevisions"):
            self.assertEqual(before["stateLayout"].get(field), self.final["stateLayout"].get(field))
        for field in set(before) - {"pages", "stateLayout", "textSemantics"}:
            self.assertEqual(before[field], self.final[field])
        self.assertEqual(fixtures.owner(before)["options"], fixtures.owner(self.final)["options"])
        self.assertEqual(fixtures.owner(self.final)["correctIndex"], 0)

    def test_only_current_prose_gets_its_new_semantic_sources(self):
        old = deepcopy(self.published["textSemantics"])
        final = deepcopy(self.final["textSemantics"])
        changed = {"/question", "/explanation"}
        for path in changed:
            previous = fixtures.marker(self.published, path)
            current = fixtures.marker(self.final, path)
            self.assertNotEqual(previous["sourceSha256"], current["sourceSha256"])
            self.assertTrue(current["spans"])
            self.assertEqual(current["sourceSha256"], hashlib.sha256(
                fixtures.owner(self.final)[path[1:]].encode("utf-8")).hexdigest())
        keep = lambda field: not (field["block"] == fixtures.OWNER and field["path"] in changed)
        old["fields"] = list(filter(keep, old["fields"]))
        final["fields"] = list(filter(keep, final["fields"]))
        self.assertEqual(old, final)

    def test_recomputed_heads_do_not_authorize_false_answers_history_or_marks(self):
        rejected = fixtures.rejected_documents(self.published, self.final)
        self.assertEqual(len(rejected), 13)
        for name, document, _ in rejected:
            with self.subTest(reason=name), self.assertRaises(ValueError):
                state.validate_transition(self.published, document)

    def test_publication_transport_rejects_even_one_extra_byte(self):
        payload = fixtures.published_bytes()
        self.assertEqual(hashlib.sha256(payload).hexdigest(), fixtures.PUBLISHED_SHA256)
        with self.assertRaises(ValueError):
            fixtures.verify_published_bytes(payload + b"\n")

    def test_export_is_exact_idempotent_and_refuses_to_overwrite(self):
        with TemporaryDirectory(prefix="choice-prose-") as temporary:
            output = Path(temporary) / "fixtures"
            path = fixtures.export(output)
            manifest = json.loads(path.read_bytes())
            self.assertEqual(path, output.resolve() / "manifest.json")
            self.assertEqual((output / "published.json").read_bytes(), fixtures.published_bytes())
            self.assertEqual(len(manifest["stages"]), 3)
            self.assertEqual(len(manifest["rejected"]), 13)
            for name, digest in manifest["sha256"].items():
                self.assertEqual(hashlib.sha256((output / name).read_bytes()).hexdigest(), digest)
            previous = {file.name: (file.read_bytes(), file.stat().st_mtime_ns)
                        for file in output.iterdir()}
            fixtures.export(output)
            self.assertEqual(previous, {file.name: (file.read_bytes(), file.stat().st_mtime_ns)
                                        for file in output.iterdir()})
            (output / "question.json").write_bytes(b"Existing unrelated QA evidence")
            with self.assertRaises(FileExistsError):
                fixtures.export(output)
            self.assertEqual((output / "question.json").read_bytes(), b"Existing unrelated QA evidence")


if __name__ == "__main__":
    unittest.main()

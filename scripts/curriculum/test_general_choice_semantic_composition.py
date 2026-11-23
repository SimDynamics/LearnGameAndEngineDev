"""Independent prose descriptors compose with an authenticated option review."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

import general_choice_semantic_composition_fixture_source as fixtures
import stable_state_layout as state
import validar_json_programacion as curriculum


class GeneralChoiceSemanticCompositionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before, cls.published, cls.reviewed = fixtures.publication_chain()

    def test_real_publications_and_skipped_intermediate_keep_the_same_contract(self):
        for before, after in ((self.before, self.published),
                              (self.published, self.reviewed),
                              (self.before, self.reviewed)):
            with self.subTest(previous=state.fingerprint(before)):
                state.validate_transition(before, after)
                positions = state.validate(before)
                updated = state.validate(after)
                old_locations = {owner: (p, b) for p, page in enumerate(before["stateLayout"]["current"])
                                 for b, owner in enumerate(page["blocks"])}
                new_locations = {owner: (p, b) for p, page in enumerate(after["stateLayout"]["current"])
                                 for b, owner in enumerate(page["blocks"])}
                self.assertEqual(old_locations.keys(), new_locations.keys())
                for owner, location in old_locations.items():
                    self.assertEqual(positions[location], updated[new_locations[owner]], owner)
                p, b = positions[old_locations[fixtures.OWNER]]
                self.assertTrue(curriculum.preserves_previous_exercise(
                    fixtures.RELATIVE, p, b,
                    fixtures.blocks(before)[fixtures.OWNER], fixtures.blocks(after)[fixtures.OWNER],
                    previous_document=before, current_document=after))
                # This legacy comparison projection restores old blocks at
                # their earlier visual positions; it is not a publication.
                aligned = state.align_for_previous_validation(before, after)
                page, block = old_locations[fixtures.OWNER]
                projected = aligned["pages"][page]["blocks"][block]
                self.assertEqual(projected["options"], fixtures.blocks(before)[fixtures.OWNER]["options"])
                self.assertEqual(projected["correctIndex"], 1)
                state.validate(json.loads(json.dumps(after, ensure_ascii=False)))

    def test_option_revision_preserves_all_other_published_sources_and_history(self):
        expected = deepcopy(self.published)
        fixtures.blocks(expected)[fixtures.OWNER]["options"][1] = fixtures.REPLACEMENT
        self.assertEqual(expected["pages"], self.reviewed["pages"])
        initial = fixtures.blocks(self.published)[fixtures.OWNER]
        current = fixtures.blocks(self.reviewed)[fixtures.OWNER]
        self.assertEqual(current["correctIndex"], 1)
        self.assertEqual(current["options"][::2], initial["options"][::2])
        self.assertEqual(current["reviewFeedback"], initial["reviewFeedback"])
        for field in set(self.published) - {"pages", "stateLayout", "textSemantics"}:
            self.assertEqual(self.published[field], self.reviewed[field], field)
        for field in set(self.published["stateLayout"]) - {"editorial", "choiceRevisions"}:
            self.assertEqual(self.published["stateLayout"][field], self.reviewed["stateLayout"][field], field)
        old = self.published["stateLayout"].get("choiceRevisions", [])
        self.assertEqual(self.reviewed["stateLayout"]["choiceRevisions"][:-1], old)
        self.assertEqual(fixtures.option_revision(self.reviewed)["block"], initial)
        self.assertEqual(self.reviewed["stateLayout"]["editorial"][:-1], self.published["stateLayout"]["editorial"])

    def test_exact_published_descriptors_and_new_option_ranges_stay_separate(self):
        expected = deepcopy(self.published["textSemantics"])
        revision = fixtures.option_revision(self.reviewed)
        old = fixtures.markers(self.published)
        self.assertEqual(revision["semanticBefore"], old.get((fixtures.OWNER, "/options/1")))
        field = next((entry for entry in expected["fields"]
                      if (entry["block"], entry["path"]) == (fixtures.OWNER, "/options/1")), None)
        if field is None:
            expected["fields"].append(deepcopy(revision["semanticAfter"]))
        else:
            field.clear()
            field.update(deepcopy(revision["semanticAfter"]))
        self.assertEqual(expected, self.reviewed["textSemantics"])
        self.assertEqual(len(revision["semanticAfter"]["spans"]), 2)
        self.assertTrue(all(span["role"] == "cpp" and span["reading"] == "technical"
                            for span in revision["semanticAfter"]["spans"]))
        for owner, path in fixtures.CHANGED_FIELDS:
            self.assertEqual(fixtures.marker(self.published, owner, path),
                             fixtures.marker(self.reviewed, owner, path))
            self.assertNotEqual(fixtures.marker(self.before, owner, path)["sourceSha256"],
                                fixtures.marker(self.reviewed, owner, path)["sourceSha256"])

    def test_unmarked_prose_does_not_acquire_a_required_descriptor(self):
        # A synthetic unmarked variant tests compatibility, not publication
        # provenance; the separately pinned authentic fixture remains intact.
        before, published = deepcopy(self.before), deepcopy(self.published)
        for document in (before, published):
            document["textSemantics"]["fields"] = [field for field in document["textSemantics"]["fields"]
                if not (field["block"] in fixtures.PARAGRAPHS and field["path"] == "/text")]
        state.validate_transition(before, published)
        reviewed = fixtures.review_option(published)
        state.validate_transition(before, reviewed)
        self.assertTrue(all((owner, "/text") not in fixtures.markers(reviewed)
                            for owner in fixtures.PARAGRAPHS))

    def test_existing_presentation_types_compose_in_either_order(self):
        for kind in ("heading", "paragraph", "note", "image"):
            for marked in (False, True):
                for option_first in (False, True):
                    with self.subTest(kind=kind, marked=marked, option_first=option_first):
                        field = "caption" if kind == "image" else "text"
                        presentation = {"type": kind, field: "Texto anterior."}
                        if kind == "image":
                            presentation.update(imageId="qa_only", src="images/qa_only.png",
                                                alt="Descripción estable.")
                        source = {"id": "cpp_fundamentals__qa__choice_semantics", "title": "QA fixture",
                                  "pages": [{"blocks": [
                                      {"type": "multiple_choice", "question": "Elige la segunda.",
                                       "options": ["Primera.", "Segunda.", "Tercera."], "correctIndex": 1},
                                      presentation]}]}
                        before = state.prepare_v2(source, "es", {"es": source})
                        if marked:
                            before["textSemantics"] = {"version": 1, "fields": [state.text_semantics.make_field(
                                before, "block_0000_0001", "/" + field,
                                [state.text_semantics.select("Texto anterior.", "Texto", "prose", reading="natural")])]}
                        state.validate(before)

                        def prose(document):
                            updated = deepcopy(document)
                            state._blocks(updated)["block_0000_0001"][field] = "Texto revisado."
                            if marked:
                                target = fixtures.marker(updated, "block_0000_0001", "/" + field)
                                target.clear()
                                target.update(state.text_semantics.make_field(updated, "block_0000_0001", "/" + field,
                                    [state.text_semantics.select("Texto revisado.", "Texto", "prose", reading="natural")]))
                            return state.prepare_v2(updated, "es", {"es": document}, previous=document)

                        def option(document):
                            return state.revise_choice_editorial(document, "block_0000_0000", 1,
                                                                "Segunda revisada.", spans=[])
                        middle = option(before) if option_first else prose(before)
                        final = prose(middle) if option_first else option(middle)
                        state.validate_transition(before, final)
                        self.assertEqual(state.validate(before), state.validate(final))
                        self.assertEqual(marked, ("block_0000_0001", "/" + field) in fixtures.markers(final))

    def test_corrupted_archives_answers_sources_and_proof_edges_are_rejected(self):
        cases = fixtures.rejected_documents(self.before, self.published, self.reviewed)
        self.assertEqual(len(cases), 10)
        for name, document, _diagnostic in cases:
            with self.subTest(reason=name):
                with self.assertRaises(ValueError):
                    state.validate(document)
                with self.assertRaises(ValueError):
                    state.validate_transition(self.before, document)

    def test_option_review_does_not_authorize_unrelated_repainting(self):
        cases = fixtures.transition_rejections(self.before, self.published, self.reviewed)
        self.assertEqual(len(cases), 7)
        for name, previous, document in cases:
            with self.subTest(reason=name), self.assertRaises(ValueError):
                state.validate_transition(previous, document)

    def test_both_complete_publications_reject_changed_bytes_or_wrong_versions(self):
        for name, (_commit, digest) in fixtures.PUBLICATIONS.items():
            payload = fixtures.publication_bytes(name)
            self.assertEqual(hashlib.sha256(payload).hexdigest(), digest)
            with self.assertRaises(ValueError):
                fixtures.verify_publication_bytes(name, payload + b"\n")
            other = "published" if name == "before" else "before"
            with self.assertRaises(ValueError):
                fixtures.verify_publication_bytes(name, fixtures.publication_bytes(other))

    def test_export_preserves_published_bytes_and_refuses_conflicting_evidence(self):
        with TemporaryDirectory(prefix="choice-semantic-composition-") as temporary:
            output = Path(temporary) / "fixtures"
            manifest_path = fixtures.export(output)
            manifest = json.loads(manifest_path.read_bytes())
            self.assertEqual(manifest["stages"], ["before.json", "published.json", "reviewed.json"])
            self.assertEqual(len(manifest["rejected"]), 10)
            for name in ("before", "published"):
                self.assertEqual((output / (name + ".json")).read_bytes(), fixtures.publication_bytes(name))
            for name, digest in manifest["sha256"].items():
                self.assertEqual(hashlib.sha256((output / name).read_bytes()).hexdigest(), digest)
            old = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()}
            fixtures.export(output)
            self.assertEqual(old, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()})
            (output / "reviewed.json").write_bytes(b"Unrelated QA evidence")
            with self.assertRaises(FileExistsError):
                fixtures.export(output)
            self.assertEqual((output / "reviewed.json").read_bytes(), b"Unrelated QA evidence")


if __name__ == "__main__":
    unittest.main()

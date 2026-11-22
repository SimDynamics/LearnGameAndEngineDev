"""Historical first hints keep their ordinary, authenticated editorial history."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest

import stable_state_layout as layout


IDENTITY = "block_0000_0000"


def original_lesson(kind="output_prediction"):
    attributes = (
        {"options": ["Primera", "Segunda", "Tercera"], "correctIndex": 1}
        if kind == "multiple_choice" else
        {"language": "cpp", "code": "std::cout << 3;", "answers": ["3"]}
    )
    return {
        "id": "cpp_fundamentals__qa__legacy_review_adoption",
        "title": "Isolated historical guidance contract",
        "pages": [{"blocks": [{
            "type": kind, "id": "original_activity",
            "question": "Pregunta original.",
            "explanation": "Explicación original.", **attributes,
        }]}],
    }


def first_hint(kind="output_prediction", *, mapped=True):
    source = original_lesson(kind)
    family = {"es": source}
    before = layout.prepare_v2(source, "es", family) if mapped else source
    draft = deepcopy(before)
    draft["pages"][0]["blocks"][0]["reviewFeedback"] = (
        ["Revisa la primera.", "Revisa la segunda.", "Revisa la tercera."]
        if kind == "multiple_choice" else "Revisa la expresión."
    )
    return before, layout.prepare_v2(draft, "es", family, before)


class LegacyReviewFeedbackAdoptionTest(unittest.TestCase):
    def test_first_scalar_and_indexed_hints_preserve_positional_and_v2_owners(self):
        for kind in ("output_prediction", "multiple_choice"):
            for mapped in (False, True):
                with self.subTest(kind=kind, mapped=mapped):
                    before, after = first_hint(kind, mapped=mapped)
                    old = before["pages"][0]["blocks"][0]
                    new = layout._blocks(after)[IDENTITY]
                    self.assertNotIn("reviewFeedback", old)
                    self.assertEqual(old, {
                        key: value for key, value in new.items()
                        if key != "reviewFeedback"
                    })
                    self.assertEqual(layout.state_fingerprint(old),
                                     layout.state_fingerprint(new))
                    self.assertNotIn("textSemantics", after)
                    self.assertEqual(layout.validate(after)[0, 0], (0, 0))
                    self.assertEqual(after["stateLayout"]["origin"],
                                     layout.prepare(before)["stateLayout"]["origin"])
                    self.assertEqual(after["stateLayout"]["editorial"], [{
                        "id": IDENTITY,
                        "beforeSha256": layout.fingerprint(old),
                        "afterSha256": layout.fingerprint(new),
                    }])
                    layout.validate_transition(before, after)

    def test_first_hint_still_requires_exact_before_and_after_proof(self):
        for mapped in (False, True):
            before, after = first_hint(mapped=mapped)
            for fault in ("missing", "before", "after"):
                altered = deepcopy(after)
                if fault == "missing":
                    altered["stateLayout"]["editorial"].clear()
                else:
                    altered["stateLayout"]["editorial"][0][fault + "Sha256"] = "0" * 64
                with self.subTest(mapped=mapped, fault=fault), self.assertRaises(ValueError):
                    layout.validate_transition(before, altered)

    def test_first_hint_cannot_license_answer_code_option_or_identity_changes(self):
        for kind, key, value in (
            ("output_prediction", "answers", ["4"]),
            ("output_prediction", "code", "std::cout << 4;"),
            ("output_prediction", "id", "another_activity"),
            ("multiple_choice", "options", ["Otra", "Segunda", "Tercera"]),
            ("multiple_choice", "correctIndex", 0),
        ):
            before, after = first_hint(kind)
            altered = deepcopy(after)
            owner = layout._blocks(altered)[IDENTITY]
            owner[key] = value
            # Even a recomputed complete destination cannot change the
            # independently retained response contract or its storage owner.
            altered["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(owner)
            with self.subTest(kind=kind, field=key), self.assertRaises(ValueError):
                layout.validate_transition(before, altered)

    def test_first_hint_cannot_claim_existing_hint_revision_paths(self):
        before, after = first_hint()
        altered = deepcopy(after)
        altered["stateLayout"]["editorial"][-1]["reviewPaths"] = ["/reviewFeedback"]
        with self.assertRaises(ValueError):
            layout.validate_transition(before, altered)

    def test_existing_hint_replacement_removal_and_option_count_stay_rejected(self):
        for kind in ("output_prediction", "multiple_choice"):
            _, before = first_hint(kind)
            old = layout._blocks(before)[IDENTITY]
            replacements = (
                ["Otra pista.", None] if kind == "output_prediction" else
                [["Otra.", "Revisa la segunda.", "Revisa la tercera."],
                 ["Solo una."], None]
            )
            for replacement in replacements:
                altered = deepcopy(before)
                owner = layout._blocks(altered)[IDENTITY]
                if replacement is None:
                    owner.pop("reviewFeedback")
                else:
                    owner["reviewFeedback"] = replacement
                altered["stateLayout"]["editorial"].append({
                    "id": IDENTITY,
                    "beforeSha256": layout.fingerprint(old),
                    "afterSha256": layout.fingerprint(owner),
                })
                with self.subTest(kind=kind, value=replacement), self.assertRaises(ValueError):
                    layout.validate_transition(before, altered)

    def test_present_but_empty_hint_is_not_treated_as_an_absent_field(self):
        for previous_hint in (None, "", []):
            source = original_lesson()
            source["pages"][0]["blocks"][0]["reviewFeedback"] = previous_hint
            before = layout.prepare_v2(source, "es", {"es": source})
            draft = deepcopy(before)
            layout._blocks(draft)[IDENTITY]["reviewFeedback"] = "Nueva pista."
            with self.subTest(previous_hint=previous_hint), self.assertRaises(ValueError):
                layout.prepare_v2(draft, "es", {"es": source}, before)

    def test_existing_hint_and_authored_question_remain_a_separate_valid_revision(self):
        _, before = first_hint()
        after = layout.revise_ordinary_editorial(before, IDENTITY, {
            "question": {"text": "Pregunta revisada.", "spans": []},
            "reviewFeedback": {"text": "Pista revisada.", "spans": []},
        })
        layout.validate_transition(before, after)
        self.assertEqual(after["stateLayout"]["editorial"][-1]["reviewPaths"],
                         ["/reviewFeedback"])
        self.assertEqual(
            {(field["block"], field["path"]) for field in after["textSemantics"]["fields"]},
            {(IDENTITY, "/question"), (IDENTITY, "/reviewFeedback")},
        )
        self.assertEqual(layout.state_fingerprint(layout._blocks(before)[IDENTITY]),
                         layout.state_fingerprint(layout._blocks(after)[IDENTITY]))

    def test_ordinary_revision_helper_does_not_gain_a_new_hint_addition_api(self):
        source = original_lesson()
        before = layout.prepare_v2(source, "es", {"es": source})
        with self.assertRaises(ValueError):
            layout.revise_ordinary_editorial(before, IDENTITY, {
                "question": {"text": "Pregunta revisada.", "spans": []},
                "reviewFeedback": {"text": "Pista antes ausente.", "spans": []},
            })


class PublishedFeedbackFixtureExportTest(unittest.TestCase):
    """Run the five build generators against their immutable Git sources."""

    def exported_files(self, root, exporter, expected_count):
        qa = root / "out"
        qa.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="legacy-hint-fixture-", dir=qa) as temp:
            output = Path(temp) / "fixture"
            manifest = exporter(output)
            self.assertTrue(manifest.is_file())
            self.assertEqual(len(list(output.glob("*.json"))), expected_count)

    def test_first_u12_composed_output_publication(self):
        import u12_composed_output_fixture_source as fixture
        self.exported_files(fixture.ROOT, fixture.export, 3)

    def test_fifteen_revision_u12_composed_output_publication(self):
        import u12_composed_output_fixture_source as fixture
        self.exported_files(fixture.ROOT, fixture.export_current, 3)

    def test_seventeen_revision_u12_composed_output_publication(self):
        import u12_composed_output_fixture_source as fixture
        self.exported_files(fixture.ROOT, fixture.export_published17, 3)

    def test_five_published_compiler_import_pairs(self):
        import compiler_import_fixture_source as fixture
        self.exported_files(fixture.ROOT, fixture.export, 11)

    def test_four_u12_append_hypothesis_publications(self):
        import u12_append_hypothesis_fixture_source as fixture
        self.exported_files(fixture.ROOT, fixture.export, 12)


if __name__ == "__main__":
    unittest.main()

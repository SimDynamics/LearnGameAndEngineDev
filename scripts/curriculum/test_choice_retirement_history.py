"""An option edit must preserve older authenticated reading presentations."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

import stable_state_layout as state
import choice_retirement_fixture_source as fixtures
import validar_json_programacion as curriculum


class ChoiceRetirementHistoryTest(unittest.TestCase):
    def test_earlier_question_explanation_and_guides_remain_exact(self):
        before, after, owner = fixtures.synthetic_pair()
        fixtures.verify_pair(before, after, owner)
        snapshot, original = fixtures.snapshot_source(after, owner)
        revised = state._blocks(after)[owner]
        archive = after["stateLayout"]["choiceRevisions"][0]
        self.assertNotEqual(original["question"], archive["block"]["question"])
        self.assertNotEqual(original["reviewFeedback"], archive["block"]["reviewFeedback"])
        self.assertEqual(revised["question"], archive["block"]["question"])
        self.assertEqual(revised["reviewFeedback"], archive["block"]["reviewFeedback"])
        projection = state._historical_choice_contract(
            after, owner, original, reading_snapshot=True)
        self.assertEqual(projection, original)
        self.assertEqual(snapshot, before["stateLayout"]["retirementHistory"][0])
        self.assertEqual(before["stateLayout"]["editorial"],
                         after["stateLayout"]["editorial"][:-1])
        relative = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                    "qa/lesson_choice_retirement.json")
        self.assertTrue(curriculum.preserves_previous_exercise(
            relative, 0, 0, state._blocks(before)[owner], revised,
            previous_document=before, current_document=after))

    def test_published_u2l6_revises_only_requested_option(self):
        before, after, owner = fixtures.published_pair()
        fixtures.verify_pair(before, after, owner)
        old, new = state._blocks(before)[owner], state._blocks(after)[owner]
        self.assertEqual(new["options"][2], fixtures.PUBLISHED_REPLACEMENT)
        self.assertEqual(old["options"][:2], new["options"][:2])
        self.assertEqual(old["correctIndex"], 2)
        for key in ("question", "explanation", "reviewFeedback"):
            self.assertEqual(old[key], new[key])
        position = next(position for position, origin in state.validate(before).items()
                        if origin == (7, 4))
        self.assertEqual(before["stateLayout"]["current"][position[0]]["blocks"][position[1]], owner)
        self.assertTrue(curriculum.preserves_previous_exercise(
            fixtures.PUBLISHED_RELATIVE, 7, 4, old, new,
            previous_document=before, current_document=after))
        for field in ("origin", "current", "family", "retired", "retirementHistory"):
            self.assertEqual(before["stateLayout"].get(field),
                             after["stateLayout"].get(field))

    def test_historical_prose_requires_the_complete_same_owner_node(self):
        _, after, owner = fixtures.synthetic_pair()
        _, original = fixtures.snapshot_source(after, owner)
        invented = deepcopy(original)
        invented["question"] = "Otra prosa con la misma respuesta."
        self.assertEqual(state.state_fingerprint(original), state.state_fingerprint(invented))
        with self.assertRaisesRegex(state.LayoutError, "owner's editorial history"):
            state._historical_choice_contract(after, owner, invented, reading_snapshot=True)
        # A matching digest attached to another owner cannot authenticate it.
        altered = deepcopy(after)
        altered["stateLayout"]["editorial"].append({
            "id": "block_0000_0001", "beforeSha256": state.fingerprint(invented),
            "afterSha256": "0" * 64,
        })
        with self.assertRaisesRegex(state.LayoutError, "owner's editorial history"):
            state._historical_choice_contract(altered, owner, invented, reading_snapshot=True)

    def test_protected_fields_stay_checked_even_with_a_recomputed_source_node(self):
        _, after, owner = fixtures.synthetic_pair()
        _, original = fixtures.snapshot_source(after, owner)
        for field, value in (
            ("options", ["Primera.", "Segunda.", "No autorizado."]),
            ("options", ["Otra.", "Segunda.", "Tercera."]),
            ("options", ["Primera."]),
            ("options", "No es una lista"),
            ("correctIndex", 0), ("type", "paragraph"), ("id", "Otro propietario"),
        ):
            changed = deepcopy(original)
            changed[field] = value
            forged = deepcopy(after)
            forged["stateLayout"]["editorial"].append({
                "id": owner, "beforeSha256": state.fingerprint(changed),
                "afterSha256": "0" * 64,
            })
            with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                state._historical_choice_contract(
                    forged, owner, changed, reading_snapshot=True)

    def test_active_projection_does_not_acquire_the_historical_exception(self):
        _, after, owner = fixtures.synthetic_pair()
        _, old = fixtures.snapshot_source(after, owner)
        with self.assertRaises(state.LayoutError):
            state._historical_choice_contract(after, owner, old)
        state._historical_choice_contract(
            after, owner, state._blocks(after)[owner])

    def test_exact_prefix_rejects_a_real_source_from_another_publication(self):
        _, after, owner = fixtures.synthetic_pair()
        forged = deepcopy(after)
        _, archived = fixtures.snapshot_source(forged, owner)
        newer = forged["stateLayout"]["choiceRevisions"][0]["block"]
        archived.clear()
        archived.update(deepcopy(newer))
        with self.assertRaisesRegex(state.LayoutError, "exact editorial prefix"):
            state.validate(forged)

    def test_legacy_snapshot_cannot_be_replaced_during_a_later_transition(self):
        _, current, owner = fixtures.synthetic_pair()
        previous = deepcopy(current)
        snapshot, _ = fixtures.snapshot_source(previous, owner)
        snapshot.pop("editorialPrefix", None)
        state.validate(previous)
        altered = deepcopy(previous)
        _, archived = fixtures.snapshot_source(altered, owner)
        archived.clear()
        archived.update(deepcopy(altered["stateLayout"]["choiceRevisions"][0]["block"]))
        # Legacy snapshots have no exact prefix; both source nodes are real,
        # but an already published complete snapshot must remain unchanged.
        state.validate(altered)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(previous, altered)

    def test_complete_rejected_documents_fail_the_shared_validator(self):
        rejected = fixtures.rejected_documents()
        self.assertEqual(len(rejected), 6)
        for reason, document in rejected:
            with self.subTest(reason=reason), self.assertRaises(ValueError):
                state.validate(document)

    def test_published_bytes_cannot_be_replaced_by_a_similar_current_lesson(self):
        original = fixtures.published_bytes()
        self.assertEqual(hashlib.sha256(original).hexdigest(), fixtures.PUBLISHED_SHA256)
        with self.assertRaises(ValueError):
            fixtures.verify_published_bytes(original + b"\n")

    def test_native_export_keeps_authentic_inputs_and_rejects_conflicts(self):
        with TemporaryDirectory(prefix="choice-retirement-") as temp:
            output = Path(temp) / "fixtures"
            manifest_path = fixtures.export(output)
            manifest = json.loads(manifest_path.read_bytes())
            self.assertEqual(len(manifest["fixtures"]), 2)
            self.assertEqual(len(manifest["rejected"]), 6)
            self.assertEqual((output / "published-before.json").read_bytes(),
                             fixtures.published_bytes())
            for fixture in manifest["fixtures"]:
                for side in ("before", "after"):
                    row = fixture[side]
                    self.assertEqual(hashlib.sha256((output / row["file"]).read_bytes()).hexdigest(),
                                     row["sha256"])
            previous = {path.name: (path.read_bytes(), path.stat().st_mtime_ns)
                        for path in output.iterdir()}
            fixtures.export(output)
            self.assertEqual(previous, {path.name: (path.read_bytes(), path.stat().st_mtime_ns)
                                        for path in output.iterdir()})
            (output / "synthetic-after.json").write_bytes(b"Different existing QA evidence")
            with self.assertRaises(FileExistsError):
                fixtures.export(output)
            self.assertEqual((output / "synthetic-after.json").read_bytes(),
                             b"Different existing QA evidence")


if __name__ == "__main__":
    unittest.main()


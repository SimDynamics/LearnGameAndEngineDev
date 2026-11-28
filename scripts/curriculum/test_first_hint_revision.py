"""The real six-page predecessor exercises all eleven independent owners."""
from copy import deepcopy
import unittest
import first_hint_revision_fixture as fixture
import stable_state_layout as layout
import validar_json_programacion as contract


class PublishedU3L5FirstHintRevision(unittest.TestCase):
    def test_all_eleven_owners_with_new_evidence_and_no_answer_changes(self):
        before, middle, final, owners = fixture.build()
        self.assertEqual(layout.validate(before), layout.validate(final))
        self.assertEqual(before["stateLayout"]["current"], final["stateLayout"]["current"])
        old = layout._blocks(before)
        new = layout._blocks(final)
        positions = {identity: position for position, identity in
                     (((p, b), owner) for p, page in enumerate(before["stateLayout"]["current"])
                      for b, owner in enumerate(page["blocks"]))}
        mapping = layout.validate(before)
        for owner in owners:
            with self.subTest(owner=owner):
                self.assertEqual(layout.state_fingerprint(old[owner]), layout.state_fingerprint(new[owner]))
                pn, bn = mapping[positions[owner]]
                self.assertTrue(contract.preserves_previous_exercise(
                    fixture.ASSET, pn, bn, old[owner], new[owner],
                    previous_document=before, current_document=final))
        legacy = deepcopy(final)
        for proof in legacy["stateLayout"]["editorial"]:
            proof.pop("reviewRevision", None)
        with self.assertRaises(ValueError):
            layout.validate_publication_transition(before, legacy)
        layout.validate_publication_transition(before, legacy, intermediate_publications=[middle])


if __name__ == "__main__":
    unittest.main()

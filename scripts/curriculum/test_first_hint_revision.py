"""The real six-page predecessor exercises all eleven independent owners."""
from copy import deepcopy
import unittest
import first_hint_revision_fixture as fixture
import stable_state_layout as layout
import validar_json_programacion as contract


class PublishedU3L5FirstHintRevision(unittest.TestCase):
    def test_real_u3l5_first_hints_and_option_editions_in_both_orders(self):
        before, stages = fixture.build_choice_guidance()
        owner = fixture.CHOICE_OWNER
        original = layout._blocks(before)[owner]
        position = next((p, b) for p, row in enumerate(before["stateLayout"]["current"])
                        for b, identity in enumerate(row["blocks"]) if identity == owner)
        pn, bn = layout.validate(before)[position]
        for name in ("guides-first", "option-first", "interleaved"):
            with self.subTest(order=name):
                final = stages[name]
                active = layout._blocks(final)[owner]
                layout.validate_publication_transition(before, final)
                self.assertEqual(active["correctIndex"], original["correctIndex"])
                self.assertEqual(active["reviewFeedback"], fixture.CHOICE_GUIDES)
                self.assertEqual(active["options"][0], fixture.CHOICE_LABEL)
                self.assertTrue(contract.preserves_previous_exercise(
                    fixture.ASSET, pn, bn, original, active,
                    previous_document=before, current_document=final))
                aligned = layout.align_for_previous_validation(before, final)
                self.assertEqual(layout._blocks(aligned)[owner]["options"], original["options"])
                layout.text_semantics.validate(aligned)
                for fault in ("hint-order", "missing-edge", "wrong-option-marker", "foreign-marker",
                              "wrong-answer", "earlier-block"):
                    forged = deepcopy(final)
                    block = layout._blocks(forged)[owner]
                    if fault == "hint-order":
                        block["reviewFeedback"][0], block["reviewFeedback"][2] = (
                            block["reviewFeedback"][2], block["reviewFeedback"][0])
                    elif fault == "missing-edge":
                        record = layout._general_choice_records(forged, owner)[0]
                        choice_edge = layout._choice_record_edge(record)
                        history = forged["stateLayout"]["editorial"]
                        history[:] = [edge for edge in history
                                      if edge["id"] != owner or edge == choice_edge]
                    elif fault == "wrong-option-marker":
                        record = layout._general_choice_records(forged, owner)[0]
                        record["semanticBefore"]["path"] = "/options/2"
                    elif fault == "foreign-marker":
                        marker = next(field for field in forged["textSemantics"]["fields"]
                                      if field["block"] == owner and field["path"] == "/options/1")
                        marker["spans"] = [layout.text_semantics.select(
                            block["options"][1], "entrada", "prose")]
                    elif fault == "wrong-answer":
                        block["correctIndex"] = 0
                    else:
                        layout._general_choice_records(forged, owner)[0]["block"]["question"] += " Falso."
                    with self.subTest(fault=fault), self.assertRaises(ValueError):
                        layout.validate_publication_transition(before, forged)

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

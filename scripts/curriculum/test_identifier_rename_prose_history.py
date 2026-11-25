"""Real published U2L6 source, independent prose archives and hostile history."""
from copy import deepcopy
import unittest

import identifier_rename_prose_fixture_source as fixture
import identifier_rename_archive_preview as rename
import stable_state_layout as state


class IdentifierRenameProseHistory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.documents = fixture.publication_chain()
        cls.rejected = fixture.rejected_documents(cls.documents)

    def test_authentic_source_and_each_complete_predecessor(self):
        original, renamed, question, interleaved, final, returned = self.documents
        self.assertEqual(fixture.verify_published_bytes(fixture.published_bytes()),
                         fixture.published_bytes())
        with self.assertRaisesRegex(ValueError, "exact published"):
            fixture.verify_published_bytes(fixture.published_bytes() + b" ")
        packet = next(item for item in renamed["stateLayout"]["identifierRenameRevisions"]
                      if item["id"] == fixture.OWNER)
        for document in self.documents[1:]:
            self.assertEqual(document["stateLayout"]["identifierRenameRevisions"],
                             renamed["stateLayout"]["identifierRenameRevisions"])
            for field in ("origin", "current", "family", "additions", "sourceFormatRevisions",
                          "retired", "retirementHistory"):
                self.assertEqual(document["stateLayout"].get(field), original["stateLayout"].get(field))
            self.assertEqual(fixture.owner(document)["answers"], ["3 3.25"])
            self.assertEqual(fixture.owner(document)["code"], fixture.owner(renamed)["code"])
        self.assertNotEqual(packet["afterSha256"], state.fingerprint(fixture.owner(final)))
        self.assertEqual(packet["afterSha256"], state.fingerprint(fixture.owner(renamed)))
        self.assertIn("cociente_convertido", fixture.owner(final)["question"])
        self.assertIn("cociente_con_operando_convertido", fixture.owner(final)["explanation"])
        state.validate_transition(question, final)
        state.validate_transition(renamed, returned)
        self.assertEqual(fixture.owner(returned), fixture.owner(renamed))

    def test_load_rejects_every_forged_source_contract_and_archive(self):
        self.assertEqual(set(self.rejected), set(fixture.NEGATIVES))
        for name, document in self.rejected.items():
            with self.subTest(name=name), self.assertRaises((ValueError, KeyError, TypeError)):
                state.validate(document)

    def test_matching_final_payload_cannot_hide_an_invalid_intermediate_contract(self):
        final = self.documents[-2]
        forged = self.rejected["intermediate_contract"]
        self.assertEqual(state._blocks(final), state._blocks(forged))
        for edge in fixture.prose_edges(forged):
            archive = edge["identifierRenameProse"]
            self.assertEqual(edge["beforeSha256"], state.fingerprint(archive["previousBlock"]))
            self.assertEqual(edge["afterSha256"], state.fingerprint(archive["currentBlock"]))
        with self.assertRaisesRegex(ValueError, "protected response contract"):
            state.validate(forged)

    def test_skipped_publications_preserve_exact_active_and_original_semantics(self):
        published, renamed, _, _, final, _ = self.documents
        aligned = state.align_for_previous_validation(published, final)
        self.assertEqual(fixture.owner(aligned)["code"], fixture.owner(published)["code"])
        self.assertEqual(fixture.owner(aligned)["question"], fixture.owner(final)["question"])
        state.text_semantics.validate(aligned)
        old = state.original_projection(published)
        history = state.original_projection(final)
        self.assertEqual(history, old)
        # Importing an already renamed publication must not undo its spelling.
        aligned_renamed = state.align_for_previous_validation(renamed, final)
        self.assertEqual(fixture.owner(aligned_renamed), fixture.owner(final))
        state.text_semantics.validate(aligned_renamed)

    def test_semantic_history_keeps_other_owners_interleaved_in_place(self):
        # A synthetic ordering of the genuine published descriptors makes
        # accidental regrouping visible without inventing their text or IDs.
        published = deepcopy(self.documents[0])
        fields = published["textSemantics"]["fields"]
        indices = [i for i, field in enumerate(fields) if field["block"] == fixture.OWNER]
        foreign = next(i for i, field in enumerate(fields) if field["block"] == fixture.OTHER_OWNER)
        descriptor = fields.pop(foreign)
        at = next(i for i, field in enumerate(fields) if field["block"] == fixture.OWNER)
        fields.insert(at + 1, descriptor)
        state.validate(published)
        renamed = rename.prepare_declared_identifier_rename_draft(published, fixture.PLANS)
        text = "Compara ambas conversiones: ¿qué muestra std::cout?"
        final = state.revise_ordinary_editorial(renamed, fixture.OWNER, {
            "question": {"text": text, "spans": [
                state.text_semantics.select(text, "std::cout", "cpp", reading="technical")]}})
        self.assertEqual(state.original_projection(final), state.original_projection(published))

    def test_published_snapshot_cannot_be_replaced_or_painted_retroactively(self):
        _, renamed, question, _, final, _ = self.documents
        candidate = deepcopy(final)
        first = fixture.prose_edges(candidate)[0]["identifierRenameProse"]
        first["previousTextSemantics"][0]["spans"][0]["role"] = "prose"
        # Recompute all following prefix seals so the rejection specifically
        # checks the actual publication's semantic predecessor, not a stale SHA.
        for ordinal, proof in enumerate(candidate["stateLayout"]["editorial"]):
            if "identifierRenameProse" in proof:
                proof["identifierRenameProse"]["historySha256"] = state.fingerprint({
                    "lessonId": candidate["id"], "locale": "es",
                    "editorial": candidate["stateLayout"]["editorial"][:ordinal]})
        with self.assertRaises((ValueError, KeyError, TypeError)):
            state.validate_transition(renamed, candidate)
        with self.assertRaises((ValueError, KeyError, TypeError)):
            state.validate_transition(question, candidate)

    def test_first_optional_explanations_create_one_semantic_root_across_owners(self):
        source = {"id": "cpp_fundamentals__qa__rename_multi_root", "title": "QA", "pages": [{
            "blocks": [{"type": "output_prediction", "language": "cpp",
                        "question": "¿Qué muestra el programa?",
                        "code": "#include <iostream>\nint main() { int value = 3; std::cout << value; }\n",
                        "answers": ["3"]} for _ in range(2)]}]}
        published = state.prepare_v2(source, "es", {"es": source})
        spaced = deepcopy(published)
        for block in spaced["pages"][0]["blocks"]:
            block["code"] = block["code"].replace("\nint main", "\n\nint main")
        formatted = state.prepare_v2(spaced, "es", {"es": published}, published)
        renamed = rename.prepare_declared_identifier_rename_draft(formatted, {
            f"block_0000_000{i}": {"code": {"value": "number"}} for i in range(2)})
        first = state.revise_ordinary_editorial(renamed, "block_0000_0000", {
            "explanation": {"text": "La variable conserva tres.", "spans": []}})
        second = state.revise_ordinary_editorial(first, "block_0000_0001", {
            "explanation": {"text": "Esta variable también conserva tres.", "spans": []}})
        state.validate_transition(renamed, second)
        state.validate_transition(formatted, second)
        self.assertEqual(state.original_projection(second), state.original_projection(formatted))
        self.assertNotIn("textSemantics", state.original_projection(second))
        self.assertFalse(fixture.prose_edges(second, "block_0000_0000")[0][
            "identifierRenameProse"]["semanticRootBefore"])
        self.assertTrue(fixture.prose_edges(second, "block_0000_0001")[0][
            "identifierRenameProse"]["semanticRootBefore"])
        # The first descriptor may also belong to an ordinary, unrenamed
        # owner. Its real before/after block and marks authenticate the birth.
        mixed_rename = rename.prepare_declared_identifier_rename_draft(formatted, {
            "block_0000_0001": {"code": {"value": "number"}}})
        ordinary = state.revise_ordinary_editorial(mixed_rename, "block_0000_0000", {
            "explanation": {"text": "La variable conserva tres.", "spans": []}})
        mixed_final = state.revise_ordinary_editorial(ordinary, "block_0000_0001", {
            "explanation": {"text": "Esta variable también conserva tres.", "spans": []}})
        state.validate_transition(mixed_rename, mixed_final)
        state.validate_transition(formatted, mixed_final)
        self.assertTrue(fixture.prose_edges(mixed_final, "block_0000_0001")[0][
            "identifierRenameProse"]["semanticRootBefore"])


if __name__ == "__main__":
    unittest.main()

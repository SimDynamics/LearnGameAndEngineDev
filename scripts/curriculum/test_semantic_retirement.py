"""Authored text semantics belong to the exact publication being read."""
from copy import deepcopy
import hashlib
import unittest

import semantic_retirement_fixture_source as fixtures


layout = fixtures.layout
semantics = layout.text_semantics


class SemanticRetirementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.documents, cls.manifest = fixtures.build_fixtures()
        cls.cases = {case["name"]: case for case in cls.manifest["cases"]}

    def pair(self, name):
        case = self.cases[name]
        return self.documents[case["before"]], self.documents[case["after"]]

    @staticmethod
    def marker(document, owner, path):
        return next(field for field in document["textSemantics"]["fields"]
                    if (field["block"], field["path"]) == (owner, path))

    def test_pinned_u7_publication_authenticates_real_owners_and_bytes(self):
        self.assertEqual(fixtures.COMMIT, "52a871c4ca18875837d22453374abbf938b4d2b4")
        self.assertEqual(hashlib.sha256(fixtures.historical_bytes()).hexdigest(),
                         "d65de29e472e1f1518559c7e6e785d68943bf1378ff80f0b55278786bbd9f788")
        real = fixtures.published()
        self.assertEqual(len(real["textSemantics"]["fields"]), 49)
        self.assertEqual(len(real["stateLayout"]["sourceFormatRevisions"]), 6)
        for owner in fixtures.REAL_OWNERS:
            self.assertTrue(self.marker(real, owner, "/text")["spans"])
        with self.assertRaisesRegex(ValueError, "pinned publication"):
            fixtures.verify_bytes(fixtures.historical_bytes() + b" ")

    def test_real_marked_paragraphs_retire_without_removing_any_authored_history(self):
        before, after = self.pair("u7-marked-batch")
        original = deepcopy(before)
        history = after["stateLayout"]["retirementHistory"][0]
        self.assertEqual(history["textSemantics"], before["textSemantics"])
        self.assertEqual(history["pages"], before["pages"])
        self.assertEqual(history["current"], before["stateLayout"]["current"])
        self.assertEqual(after["textSemantics"], {
            **before["textSemantics"], "fields": [field for field in before["textSemantics"]["fields"]
                if field["block"] not in fixtures.REAL_OWNERS]})
        for owner in fixtures.REAL_OWNERS:
            self.assertNotIn(owner, layout._blocks(after))
            self.assertEqual(layout._all_blocks(after)[owner], layout._blocks(before)[owner])
        for key in ("origin", "family", "additions", "editorial", "sourceFormatRevisions"):
            self.assertEqual(after["stateLayout"][key], before["stateLayout"][key])
        layout.validate_transition(before, after)
        self.assertEqual(before, original)

    def test_successive_real_retirements_keep_first_snapshot_and_its_pin_exact(self):
        before, after = self.pair("u7-marked-second")
        first = before["stateLayout"]["retirementHistory"][0]
        self.assertEqual(first, after["stateLayout"]["retirementHistory"][0])
        self.assertEqual(layout.fingerprint(first),
                         layout.fingerprint(after["stateLayout"]["retirementHistory"][0]))
        self.assertEqual(after["stateLayout"]["retirementHistory"][1]["textSemantics"], before["textSemantics"])
        self.assertEqual(after["stateLayout"]["retired"][0], before["stateLayout"]["retired"][0])

    def test_same_text_different_role_and_reading_are_versioned_independently(self):
        _, after = self.pair("semantic-repainted")
        old, new = (fixtures.history_view(after, index) for index in (0, 1))
        first = self.marker(old, fixtures.NOTE_B, "/text")
        second = self.marker(new, fixtures.NOTE_B, "/text")
        self.assertEqual(first["sourceSha256"], second["sourceSha256"])
        self.assertNotEqual(first["spans"], second["spans"])
        self.assertEqual(first["spans"][1]["role"], "output")
        self.assertEqual(second["spans"][1]["role"], "data")
        self.assertEqual(first["spans"][1]["reading"], "natural")
        self.assertEqual(second["spans"][1]["reading"], "characters")
        self.assertNotEqual(old["textSemantics"]["rootFields"], new["textSemantics"]["rootFields"])
        semantics.validate(old)
        semantics.validate(new)

    def test_old_questions_and_math_prompts_keep_their_own_descriptors(self):
        _, after = self.pair("semantic-reworded")
        old = fixtures.history_view(after, 0)
        new = fixtures.history_view(after, 2)
        first, last = semantics.validate(old), semantics.validate(new)
        for owner, path in ((fixtures.OUTPUT, "/question"), (fixtures.MATH, "/prompt")):
            self.assertNotEqual(first[owner][path]["source"], last[owner][path]["source"])
            self.assertNotEqual(self.marker(old, owner, path)["sourceSha256"],
                                self.marker(new, owner, path)["sourceSha256"])
        wrong = deepcopy(after)
        wrong["stateLayout"]["retirementHistory"][0]["textSemantics"] = deepcopy(new["textSemantics"])
        with self.assertRaisesRegex(layout.LayoutError, "source fingerprint is stale"):
            layout.validate(wrong)

    def test_root_fields_bind_to_immutable_title_and_preserve_utf16_scalars(self):
        before, after = self.pair("semantic-root-only")
        resolved = semantics.validate_root_fields(fixtures.history_view(after))
        self.assertEqual(resolved["/title"]["source"], before["title"])
        span = resolved["/title"]["spans"][0]
        self.assertEqual(span["end"] - span["start"], 2)
        self.assertEqual(span["text"], "🌌")
        self.assertEqual(after["textSemantics"], before["textSemantics"])
        wrong = deepcopy(after)
        wrong["title"] += " cambiado"
        with self.assertRaisesRegex(layout.LayoutError, "source fingerprint is stale"):
            layout.validate(wrong)

    def test_explicit_empty_overlay_and_prose_only_descriptor_are_preserved(self):
        before, after = self.pair("semantic-empty-overlay")
        self.assertEqual(after["stateLayout"]["retirementHistory"][0]["textSemantics"],
                         {"version": 1, "fields": [], "rootFields": []})
        self.assertEqual(before["textSemantics"], after["textSemantics"])
        source = fixtures.synthetic(marked=False)
        fixtures.mark(source, fixtures.NOTE_A, "/text", [])
        retired = layout.retire(source, [fixtures.NOTE_A])
        descriptor = self.marker(fixtures.history_view(retired), fixtures.NOTE_A, "/text")
        self.assertEqual(descriptor["spans"], [])
        self.assertEqual(retired["textSemantics"], {"version": 1, "fields": []})

    def test_absence_is_certified_as_null_without_borrowing_later_marks(self):
        _, after = self.pair("semantic-null-before-marking")
        history = after["stateLayout"]["retirementHistory"]
        self.assertIn("textSemantics", history[0])
        self.assertIsNone(history[0]["textSemantics"])
        self.assertNotIn("textSemantics", fixtures.history_view(after, 0))
        self.assertEqual(semantics.validate(fixtures.history_view(after, 0)), {})
        self.assertTrue(semantics.validate(fixtures.history_view(after, 1)))
        self.assertTrue(after["textSemantics"]["fields"])

    def test_legacy_six_fields_remain_unknown_and_cannot_be_backfilled(self):
        before, after = self.pair("semantic-legacy-before-marking")
        old_snapshot = before["stateLayout"]["retirementHistory"][0]
        self.assertEqual(len(old_snapshot), 6)
        self.assertNotIn("textSemantics", old_snapshot)
        self.assertEqual(old_snapshot, after["stateLayout"]["retirementHistory"][0])
        self.assertEqual(semantics.validate(fixtures.history_view(after, 0)), {})
        wrong = deepcopy(after)
        wrong["stateLayout"]["retirementHistory"][0]["textSemantics"] = None
        layout.validate(wrong)
        with self.assertRaisesRegex(layout.LayoutError, "append-only"):
            layout.validate_transition(before, wrong)

    def test_new_transition_requires_complete_exact_semantic_capture(self):
        before, after = self.pair("semantic-batch")
        mutations = [
            lambda snapshot: snapshot.pop("textSemantics"),
            lambda snapshot: snapshot.update(textSemantics=None),
            lambda snapshot: snapshot["textSemantics"]["fields"].pop(),
            lambda snapshot: snapshot["textSemantics"]["fields"][0]["spans"][0].update(role="prose"),
            lambda snapshot: snapshot["textSemantics"]["rootFields"][0]["spans"][0].update(reading="natural"),
        ]
        for mutate in mutations:
            wrong = deepcopy(after)
            mutate(wrong["stateLayout"]["retirementHistory"][0])
            # A standalone legacy shape cannot certify how a snapshot was
            # published. The actual predecessor and persisted raw pin do.
            layout.validate(wrong)
            with self.subTest(mutation=mutate), self.assertRaisesRegex(layout.LayoutError, "complete previous published presentation"):
                layout.validate_transition(before, wrong)

    def test_published_semantic_history_is_append_only_even_when_text_is_equal(self):
        before, after = self.pair("semantic-repainted")
        wrong = deepcopy(after)
        field = wrong["stateLayout"]["retirementHistory"][0]["textSemantics"]["fields"][0]
        field["spans"][0].update(role="prose", reading="natural")
        layout.validate(wrong)
        with self.assertRaisesRegex(layout.LayoutError, "append-only"):
            layout.validate_transition(before, wrong)

    def test_malformed_or_hidden_historical_descriptors_are_rejected(self):
        for record in self.manifest["invalid"]:
            with self.subTest(name=record["name"]), self.assertRaises(layout.LayoutError):
                layout.validate(self.documents[record["file"]])

    def test_retired_owner_binding_checks_complete_block_not_only_public_text(self):
        before, after = self.pair("semantic-batch")
        wrong = deepcopy(self.documents["invalid-different-archived-block.json"])
        historical = fixtures.format_fixtures.history_block(wrong, fixtures.NOTE_A)
        archived = layout._all_blocks(after)[fixtures.NOTE_A]
        self.assertEqual(semantics.public_text_fields(historical), semantics.public_text_fields(archived))
        self.assertEqual(layout.state_fingerprint(historical), layout.state_fingerprint(archived))
        with self.assertRaisesRegex(layout.LayoutError, "exact archived block"):
            layout.validate(wrong)
        # Legacy remains backward compatible but cannot masquerade as a newly
        # authored retirement from a marked publication.
        wrong["stateLayout"]["retirementHistory"][0].pop("textSemantics")
        layout.validate(wrong)
        with self.assertRaisesRegex(layout.LayoutError, "complete previous published presentation"):
            layout.validate_transition(before, wrong)

    def test_bibliography_snapshots_preserve_both_exact_citation_versions(self):
        before, after = self.pair("semantic-bibliography")
        history = [fixtures.history_view(after, index) for index in (0, 1)]
        old, new = [semantics.validate(view)[fixtures.OUTPUT]["/explanation"] for view in history]
        self.assertIn(", p. 9", old["source"])
        self.assertNotIn(", p. 9", new["source"])
        proof = next(proof["bibliography"] for proof in before["stateLayout"]["editorial"] if "bibliography" in proof)
        self.assertEqual(proof["previousTextSemantics"], self.marker(history[0], fixtures.OUTPUT, "/explanation"))
        self.assertEqual(proof["currentTextSemantics"], self.marker(history[1], fixtures.OUTPUT, "/explanation"))
        self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"])

    def test_editorial_return_does_not_alias_same_text_with_different_intent(self):
        _, after = self.pair("semantic-editorial-return")
        first, returned = (fixtures.history_view(after, index) for index in (0, 2))
        old = self.marker(first, fixtures.OUTPUT, "/question")
        new = self.marker(returned, fixtures.OUTPUT, "/question")
        self.assertEqual(old["sourceSha256"], new["sourceSha256"])
        self.assertNotEqual(old["spans"], new["spans"])
        self.assertTrue(any("returnTo" in proof for proof in after["stateLayout"]["editorial"]))
        layout.validate(after)

    def test_formatting_and_retirement_compose_in_both_orders(self):
        for name in ("semantic-format-before-retirement", "semantic-retirement-before-format"):
            before, after = self.pair(name)
            with self.subTest(name=name):
                layout.validate_transition(before, after)
                self.assertEqual(before["stateLayout"]["origin"], after["stateLayout"]["origin"])
                snapshot = after["stateLayout"]["retirementHistory"][0]
                self.assertTrue(snapshot["textSemantics"]["fields"])
                self.assertTrue(after["stateLayout"]["sourceFormatRevisions"])
                semantics.validate(fixtures.history_view(after))

    def test_retired_compiler_preserves_exact_guidance_and_state_address(self):
        before, after = self.pair("semantic-compiler-retirement")
        self.assertNotIn(fixtures.COMPILER, layout._blocks(after))
        self.assertEqual(layout._all_blocks(after)[fixtures.COMPILER], layout._blocks(before)[fixtures.COMPILER])
        self.assertEqual(after["stateLayout"]["origin"], before["stateLayout"]["origin"])
        self.assertFalse(any(field["block"] == fixtures.COMPILER for field in after["textSemantics"]["fields"]))
        resolved = semantics.validate(fixtures.history_view(after))[fixtures.COMPILER]
        self.assertIn("/outputCheck/cases/0/explanation", resolved)
        self.assertIn("/outputCheck/cases/0/reviewFeedback/0/text", resolved)

    def test_case_feedback_intent_is_bound_to_its_exact_path(self):
        before, after = self.pair("semantic-multicase-compiler")
        fields = semantics.validate(fixtures.history_view(after))[fixtures.COMPILER]
        for suffix in ("explanation", "reviewFeedback/0/text"):
            left, right = [fields[f"/outputCheck/cases/{index}/{suffix}"] for index in (0, 1)]
            self.assertEqual(left["source"], right["source"])
            self.assertNotEqual(left["spans"], right["spans"])
            self.assertEqual(left["spans"][0]["role"], "output")
            self.assertEqual(right["spans"][0]["reading"], "characters")
        cases = layout._blocks(before)[fixtures.COMPILER]["outputCheck"]["cases"]
        self.assertEqual([case["standardInput"] for case in cases], ["2\n", "3\n"])
        self.assertEqual([case["outputs"] for case in cases], [["2"], ["3"]])

    def test_general_choice_and_retirement_share_one_exact_predecessor(self):
        before, after = self.pair("semantic-retirement-and-choice")
        layout.validate_transition(before, after)
        self.assertEqual(after["stateLayout"]["retirementHistory"][0]["textSemantics"], before["textSemantics"])
        self.assertNotEqual(layout._blocks(before)[fixtures.CHOICE]["options"][0],
                            layout._blocks(after)[fixtures.CHOICE]["options"][0])
        wrong = deepcopy(after)
        self.marker(wrong, fixtures.NOTE_B, "/text")["spans"][0]["role"] = "prose"
        layout.validate(wrong)
        with self.assertRaisesRegex(layout.LayoutError, "unrelated text marker"):
            layout.validate_transition(before, wrong)
        missing = deepcopy(after)
        missing["stateLayout"]["retirementHistory"][0].pop("textSemantics")
        with self.assertRaisesRegex(layout.LayoutError, "complete previous published presentation"):
            layout.validate_transition(before, missing)

    def test_choice_before_retirement_preserves_old_marks_and_authenticates_new_prose(self):
        before, after = self.pair("semantic-choice-before-retirement")
        source = self.documents[self.cases["semantic-choice-before-retirement"]["stateSource"]]
        old = self.marker(source, fixtures.CHOICE, "/explanation")
        self.assertEqual(old, self.marker(before, fixtures.CHOICE, "/explanation"))
        self.assertEqual(old, self.marker(fixtures.history_view(after), fixtures.CHOICE, "/explanation"))
        self.assertNotEqual(layout._blocks(source)[fixtures.CHOICE]["options"],
                            layout._blocks(before)[fixtures.CHOICE]["options"])

        # The option was already corrected. A second, independently signed
        # editorial step may clarify its explanation without recycling the
        # earlier source marker, answer or historical state address.
        text = "El resultado sigue siendo N=7 después de comparar."
        edited = layout.revise_ordinary_editorial(before, fixtures.CHOICE, {"explanation": {
            "text": text, "spans": [semantics.select(text, "N=7", "output", reading="natural")]}})
        layout.validate_transition(before, edited)
        revised = self.marker(edited, fixtures.CHOICE, "/explanation")
        self.assertNotEqual(old["sourceSha256"], revised["sourceSha256"])
        self.assertEqual(revised["spans"][0]["text"], "N=7")
        self.assertEqual(layout._blocks(edited)[fixtures.CHOICE]["options"],
                         layout._blocks(before)[fixtures.CHOICE]["options"])
        self.assertEqual(layout._blocks(edited)[fixtures.CHOICE]["correctIndex"], 0)
        self.assertEqual(edited["stateLayout"]["origin"], before["stateLayout"]["origin"])
        self.assertEqual(edited["stateLayout"]["choiceRevisions"],
                         before["stateLayout"]["choiceRevisions"])

        retired = layout.retire(edited, [fixtures.NOTE_A])
        layout.validate_transition(edited, retired)
        self.assertEqual(retired["stateLayout"]["retirementHistory"][0]["textSemantics"],
                         edited["textSemantics"])
        self.assertEqual(self.marker(fixtures.history_view(retired),
                                     fixtures.CHOICE, "/explanation"), revised)
        self.assertEqual(self.marker(fixtures.history_view(after),
                                     fixtures.CHOICE, "/explanation"), old)

        # The optional prose mechanism must not accept an unproven wording or
        # a fabricated new response contract under the same choice owner.
        without_edge = deepcopy(edited)
        without_edge["stateLayout"]["editorial"].pop()
        with self.assertRaises(layout.LayoutError):
            layout.validate(without_edge)
        changed_answer = deepcopy(edited)
        layout._blocks(changed_answer)[fixtures.CHOICE]["correctIndex"] = 1
        changed_answer["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(
            layout._blocks(changed_answer)[fixtures.CHOICE])
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed_answer)

    def test_shared_manifest_histories_are_complete_authentic_publications(self):
        for case in self.manifest["cases"]:
            result = self.documents[case["after"]]
            for entry in case["history"]:
                source = self.documents[entry["source"]]
                snapshot = result["stateLayout"]["retirementHistory"][entry["index"]]
                with self.subTest(case=case["name"], index=entry["index"]):
                    self.assertEqual(snapshot["pages"], source["pages"])
                    self.assertEqual(snapshot["current"], source["stateLayout"]["current"])
                    self.assertEqual(snapshot["compilerPuzzlesRequired"], source.get("compilerPuzzlesRequired", False))
                    for name in ("referenceAfter", "activityAfter"):
                        self.assertEqual(snapshot[name], source.get(name, []))
                    if "textSemantics" in snapshot:
                        self.assertEqual(snapshot["textSemantics"], source.get("textSemantics"))
                    else:
                        self.assertNotIn("textSemantics", source)


if __name__ == "__main__":
    unittest.main()

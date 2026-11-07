"""Authenticated bibliography-only history; no active asset or student data writes."""
from copy import deepcopy
import unittest

import stable_state_layout as layout
import text_semantics as semantics


def field(document, identity, text, *, explicit=True):
    metadata = document.setdefault("textSemantics", {"version": 1, "fields": []})
    metadata["fields"] = [item for item in metadata["fields"]
                          if (item["block"], item["path"]) != (identity, "/explanation")]
    if explicit:
        citation = text[text.index("("):text.index(")") + 1]
        metadata["fields"].append(semantics.make_field(
            document, identity, "/explanation", [semantics.select(text, citation, role="citation", reading="natural")]))


def initial(*, explicit=True):
    source = {"id": "qa_bibliography", "title": "Historia bibliográfica",
              "pages": [{"blocks": [
                  {"type": "multiple_choice", "id": "choose", "question": "Elige el resultado.",
                   "options": ["A", "B"], "correctIndex": 0,
                   "explanation": "La razón permanece (García et al., 2024, pp. 7–9)."},
                  {"type": "output_prediction", "id": "predict", "question": "Predice la salida.",
                   "code": "int main() {}", "answers": [""],
                   "explanation": "El resultado se conserva (Ramos, 2022, sección 3)."}]}]}
    result = layout.prepare_v2(source, "es", {"es": source})
    for identity, block in zip(result["stateLayout"]["current"][0]["blocks"], result["pages"][0]["blocks"]):
        field(result, identity, block["explanation"], explicit=explicit)
    layout.validate(result)
    return result


def shorten(document, index=0, *, explicit=True):
    result = deepcopy(document)
    block = result["pages"][0]["blocks"][index]
    block["explanation"] = block["explanation"].replace(", pp. 7–9", "").replace(", sección 3", "")
    identity = result["stateLayout"]["current"][0]["blocks"][index]
    field(result, identity, block["explanation"], explicit=explicit)
    return result


def prepare(previous, changed):
    return layout.prepare_v2(changed, "es", {"es": previous}, previous=previous)


class BibliographyArchiveTest(unittest.TestCase):
    def test_two_changes_keep_exact_ordered_prefix_sources_and_semantics(self):
        before = initial()
        snapshot = deepcopy(before)
        changed = shorten(shorten(before), 1)
        result = prepare(before, changed)
        edges = result["stateLayout"]["editorial"]
        self.assertEqual(len(edges), 2)
        for index, edge in enumerate(edges):
            archive = edge["bibliography"]
            self.assertEqual(set(archive), {"historySha256", "previousBlock", "currentBlock",
                                            "previousTextSemantics", "currentTextSemantics"})
            self.assertEqual(archive["historySha256"], layout.fingerprint({
                "lessonId": result["id"], "locale": "es", "editorial": edges[:index]}))
            self.assertEqual(archive["previousBlock"], before["pages"][0]["blocks"][index])
            self.assertEqual(archive["currentBlock"], changed["pages"][0]["blocks"][index])
            self.assertEqual(archive["previousTextSemantics"],
                             layout._explanation_semantics(before, edge["id"]))
            self.assertEqual(archive["currentTextSemantics"],
                             layout._explanation_semantics(changed, edge["id"]))
        layout.validate_transition(before, result)
        self.assertEqual(before, snapshot)
        self.assertEqual(result["pages"], changed["pages"], "The helper must never shorten the author's text")
        self.assertEqual(result["stateLayout"]["origin"], before["stateLayout"]["origin"])
        self.assertEqual(result["stateLayout"]["family"], before["stateLayout"]["family"])

    def test_no_change_never_rewrites_text_or_adds_a_proof(self):
        before = initial()
        self.assertEqual(prepare(before, deepcopy(before)), before)

    def test_first_v2_adoption_keeps_the_actual_prior_source(self):
        before = initial(explicit=False)
        before.pop("stateLayout")
        before.pop("textSemantics")
        changed = deepcopy(before)
        changed["pages"][0]["blocks"][0]["explanation"] = "La razón permanece (García et al., 2024)."
        result = layout.prepare_v2(changed, "es", {"es": before})
        archive = result["stateLayout"]["editorial"][0]["bibliography"]
        self.assertEqual(archive["previousBlock"], before["pages"][0]["blocks"][0])
        self.assertIsNone(archive["previousTextSemantics"])
        self.assertEqual(result["stateLayout"]["family"]["es"]["sha256"], layout.fingerprint(before))
        layout.validate_transition(before, result)

    def test_consecutive_changes_of_one_owner_keep_every_exact_intermediate(self):
        before = initial()
        identity = before["stateLayout"]["current"][0]["blocks"][0]
        block = before["pages"][0]["blocks"][0]
        block["explanation"] = "La razón permanece (García et al., 2024, pp. 7–9; Ramos, 2022, sección 3)."
        field(before, identity, block["explanation"])
        # This is a first literal fixture, not a claim about an earlier publication.
        baseline = deepcopy(before)
        baseline.pop("stateLayout")
        baseline.pop("textSemantics")
        before = layout.prepare_v2(baseline, "es", {"es": baseline})
        field(before, identity, block["explanation"])
        draft = deepcopy(before)
        draft["pages"][0]["blocks"][0]["explanation"] = block["explanation"].replace(", pp. 7–9", "")
        field(draft, identity, draft["pages"][0]["blocks"][0]["explanation"])
        first = prepare(before, draft)
        draft = deepcopy(first)
        draft["pages"][0]["blocks"][0]["explanation"] = draft["pages"][0]["blocks"][0]["explanation"].replace(", sección 3", "")
        field(draft, identity, draft["pages"][0]["blocks"][0]["explanation"])
        second = prepare(first, draft)
        layout.validate_transition(before, second)
        first_archive, second_archive = [edge["bibliography"] for edge in second["stateLayout"]["editorial"]]
        self.assertEqual(first_archive["currentBlock"], second_archive["previousBlock"])
        self.assertEqual(first_archive["currentTextSemantics"], second_archive["previousTextSemantics"])
        forged = deepcopy(second)
        forged["stateLayout"]["editorial"][1]["bibliography"]["previousTextSemantics"] = None
        layout.validate(forged)
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, forged)

    def test_absent_and_explicit_empty_semantics_remain_distinct(self):
        before = initial(explicit=False)
        changed = shorten(before, explicit=False)
        result = prepare(before, changed)
        archive = result["stateLayout"]["editorial"][0]["bibliography"]
        self.assertIsNone(archive["previousTextSemantics"])
        self.assertIsNone(archive["currentTextSemantics"])
        identity = before["stateLayout"]["current"][0]["blocks"][0]
        for document in (before, changed):
            source = document["pages"][0]["blocks"][0]["explanation"]
            document["textSemantics"]["fields"].append(semantics.make_field(document, identity, "/explanation", []))
        result = prepare(before, changed)
        archive = result["stateLayout"]["editorial"][0]["bibliography"]
        self.assertEqual(archive["previousTextSemantics"]["spans"], [])
        self.assertEqual(archive["currentTextSemantics"]["spans"], [])

    def test_malformed_and_forged_archive_data_are_rejected(self):
        before = initial()
        result = prepare(before, shorten(before))
        mutations = [
            lambda a: a.update(historySha256="0" * 64),
            lambda a: a.pop("previousTextSemantics"),
            lambda a: a.update(unrecognized=True),
            lambda a: a["previousBlock"].update(correctIndex=1),
            lambda a: a["currentBlock"].update(explanation="Otra razón (García et al., 2024)."),
            lambda a: a["previousTextSemantics"].update(block="another_owner"),
            lambda a: a["currentTextSemantics"].update(path="/question"),
            lambda a: a["previousTextSemantics"].update(sourceSha256="0" * 64),
            lambda a: a["currentTextSemantics"]["spans"][0].update(end=99999),
        ]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                forged = deepcopy(result)
                mutate(forged["stateLayout"]["editorial"][0]["bibliography"])
                with self.assertRaises((layout.LayoutError, ValueError)):
                    layout.validate(forged)

    def test_valid_but_fictitious_previous_semantics_cannot_claim_a_published_transition(self):
        before = initial()
        result = prepare(before, shorten(before))
        forged = deepcopy(result)
        forged["stateLayout"]["editorial"][0]["bibliography"]["previousTextSemantics"]["spans"][0]["role"] = "prose"
        layout.validate(forged)  # Source/ranges alone cannot authenticate published authoring.
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, forged)

    def test_a_new_bare_hash_edge_cannot_drop_bibliography_reading_provenance(self):
        before = initial()
        result = prepare(before, shorten(before))
        result["stateLayout"]["editorial"][0].pop("bibliography")
        layout.validate(result)  # Historical bare edges remain a supported format.
        with self.assertRaisesRegex(layout.LayoutError, "reading archive"):
            layout.validate_transition(before, result)

    def test_published_archives_are_append_only(self):
        before = initial()
        first = prepare(before, shorten(before))
        second = prepare(first, shorten(first, 1))
        self.assertEqual(second["stateLayout"]["editorial"][:1], first["stateLayout"]["editorial"])
        layout.validate_transition(before, second)
        altered = deepcopy(second)
        altered["stateLayout"]["editorial"][0]["bibliography"]["previousTextSemantics"] = None
        edges = altered["stateLayout"]["editorial"]
        edges[1]["bibliography"]["historySha256"] = layout.fingerprint({
            "lessonId": altered["id"], "locale": "es", "editorial": edges[:1]})
        layout.validate(altered)
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(first, altered)

    def test_mixed_reasoning_changes_never_get_a_bibliography_archive(self):
        before = initial()
        changed = shorten(before)
        changed["pages"][0]["blocks"][0]["explanation"] += " Otra afirmación."
        identity = changed["stateLayout"]["current"][0]["blocks"][0]
        field(changed, identity, changed["pages"][0]["blocks"][0]["explanation"])
        result = prepare(before, changed)
        self.assertNotIn("bibliography", result["stateLayout"]["editorial"][0])

    def test_real_authored_explanations_have_exact_source_anchored_archives(self):
        from bibliography_fixture_source import load_pairs
        pairs = load_pairs()
        self.assertEqual(len(pairs), 3)
        changes = 0
        for before, after in pairs:
            layout.validate_transition(before, after)
            old_edges = before["stateLayout"]["editorial"]
            appended = after["stateLayout"]["editorial"][len(old_edges):]
            self.assertTrue(appended)
            self.assertTrue(all("bibliography" in proof for proof in appended))
            changes += len(appended)
        self.assertEqual(changes, 10)


if __name__ == "__main__":
    unittest.main()

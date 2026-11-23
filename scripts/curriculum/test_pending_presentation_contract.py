"""Scoped editorial revisions and optional presentation never relax answer identity."""

import copy
import json
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


U2L8_LESSON = "objetos_y_estado/lesson_caracteres_y_valores_logicos.json"
U2L8_LOCATIONS = ((1, 5), (2, 3), (3, 3), (4, 3), (6, 2), (6, 3), (6, 5), (8, 5))
U2L8_RETIRED_EDITOR = "block_0006_0006"


def verified_u2l8_explanations(document):
    """Keep the old oracle and trace active prose through its actual archives.

    The six-edge publication, its later bibliography archives, and the reading
    snapshot retained by the current edition are all supported. No live prose
    becomes a replacement oracle merely because its answer contract matches.
    """
    layout.validate(document)
    layout.text_semantics.validate(document)
    metadata = document["stateLayout"]
    current = layout._blocks(document)
    snapshots = [snapshot for snapshot in metadata.get("retirementHistory", [])
                 if U2L8_RETIRED_EDITOR in snapshot["retired"]]
    layout.require(len(snapshots) <= 1, "The U2L8 source snapshot must be unique")
    if snapshots:
        snapshot = snapshots[0]
        prefix = layout._validate_retirement_editorial_prefix(document, snapshot)
        layout.require(prefix is not None, "The U2L8 source needs its exact editorial prefix")
        historical = {
            identity: block
            for page, record in zip(snapshot["pages"], snapshot["current"])
            for block, identity in zip(page["blocks"], record["blocks"])
        }
    else:
        historical = current
        prefix = len(metadata["editorial"])

    markers = {(field["block"], field["path"]): field
               for field in document.get("textSemantics", {}).get("fields", [])}
    result = []
    prose = {"question", "explanation", "reviewFeedback"}
    for page, block_index in U2L8_LOCATIONS:
        identity = f"block_{page - 1:04d}_{block_index - 1:04d}"
        layout.require(identity in historical and identity in current,
                       "A reviewed U2L8 exercise lost its stable owner")
        previous, active = historical[identity], current[identity]
        reviewed = copy.deepcopy(previous)
        # A bibliography-only revision already carries both complete blocks.
        # Rewind only an exact destination at the authenticated snapshot time.
        for edge in reversed(metadata["editorial"][:prefix]):
            if edge["id"] != identity or "bibliography" not in edge:
                continue
            if layout.fingerprint(reviewed) != edge["afterSha256"]:
                continue
            archive = edge["bibliography"]
            layout.require(reviewed == archive["currentBlock"]
                           and layout.fingerprint(archive["previousBlock"]) == edge["beforeSha256"],
                           "The reviewed explanation lost its exact bibliography source")
            reviewed = copy.deepcopy(archive["previousBlock"])
        location = (U2L8_LESSON, page, block_index)
        kind, explanation = contract.EXPLANATION_CLARIFICATIONS[location]
        restored = contract.restore_approved_editorial_fields(
            location, {"type": kind, "explanation": explanation}, reviewed)
        layout.require(reviewed["type"] == kind and restored["explanation"] == explanation,
                       "The archived U2L8 explanation differs from its pinned reviewed source")
        # Compare the protected contract through the published format archive,
        # while retaining the complete physical blocks for provenance below.
        source_changed = layout.code_format.require_source_lexical(previous, active)
        previous_contract = layout._historical_source_format_contract(
            document, identity, previous, editorial_prefix=prefix if snapshots else None)
        active_contract = layout._historical_source_format_contract(document, identity, active)
        layout.require({key: value for key, value in previous_contract.items() if key not in prose}
                       == {key: value for key, value in active_contract.items() if key not in prose},
                       "A revised U2L8 explanation changed its program, answer or other protected field")
        if active != previous:
            paths = layout.ordinary_editorial_paths(previous_contract, active_contract)
            layout.require(bool(paths) or source_changed,
                           "The active U2L8 change is not an authored prose or source-format revision")
            proofs = [edge for edge in metadata["editorial"][prefix:] if edge["id"] == identity]
            layout.require(bool(proofs)
                           and proofs[0]["beforeSha256"] == layout.fingerprint(previous)
                           and proofs[-1]["afterSha256"] == layout.fingerprint(active),
                           "The active U2L8 explanation lost its complete previous/current provenance")
            layout.require(all((identity, path) in markers for path in paths),
                           "A revised U2L8 field lost its authored reading descriptor")
            required_reviews = set(layout.ordinary_feedback_paths(previous_contract, active_contract))
            recorded_reviews = {path for proof in proofs for path in proof.get("reviewPaths", [])}
            available_reviews = {path for path in layout.text_semantics.public_text_fields(active)
                                 if path == "/reviewFeedback" or path.startswith("/reviewFeedback/")}
            # A later valid editorial return can restore an earlier hint.
            # Its recorded revision remains real even when the net text delta
            # is empty; every still-changed hint must nevertheless be traced.
            layout.require(required_reviews <= recorded_reviews <= available_reviews,
                           "The active U2L8 hints differ from their authenticated review paths")
        result.append((location, kind, explanation, reviewed, previous, active))
    return result


class PendingPresentationContractTest(unittest.TestCase):
    def setUp(self):
        contract.ERRORS.clear()

    def emphasis(self, value, text="Un objeto conserva su valor."):
        contract.ERRORS.clear()
        contract.validate_paragraph_emphasis_contract(
            {"type": "paragraph", "text": text, "emphasis": value}, "fixture")
        return not contract.ERRORS

    def test_explicit_exact_emphasis_is_optional_and_never_rewrites_text(self):
        block = {"type": "paragraph", "text": "Un objeto conserva su valor."}
        original = copy.deepcopy(block)
        contract.validate_paragraph_emphasis_contract(block, "fixture")
        self.assertEqual([], contract.ERRORS)
        self.assertEqual(original, block)
        self.assertTrue(self.emphasis([{"text": "objeto"}, {"text": "valor"}]))
        self.assertTrue(self.emphasis([]))
        self.assertTrue(self.emphasis([{"text": "objeto", "occurrence": 1.0}]))

    def test_repeated_and_overlapping_selections_require_the_explicit_occurrence(self):
        self.assertFalse(self.emphasis([{"text": "objeto"}], "objeto y objeto"))
        self.assertTrue(self.emphasis([{"text": "objeto", "occurrence": 2}], "objeto y objeto"))
        self.assertTrue(self.emphasis([{"text": "aa", "occurrence": 2}], "aaa"))
        self.assertTrue(self.emphasis([{"text": "😀"}], "Un 😀 y valor"))

    def test_bad_emphasis_selectors_are_reported_without_using_answers(self):
        for value in (None, False, "objeto", {}, ["objeto"], [None], [{}],
                      [{"text": ""}], [{"text": " \n"}], [{"text": "missing"}],
                      [{"text": "objeto", "unknownStyle": "bold"}]):
            with self.subTest(value=value):
                self.assertFalse(self.emphasis(value))
        for occurrence in (None, False, 0, -1, 1.5, 2, "1", {}, [], 10**50):
            with self.subTest(occurrence=occurrence):
                self.assertFalse(self.emphasis([{"text": "objeto", "occurrence": occurrence}]))

    def test_eight_pinned_explanations_match_the_reviewed_source_and_preserve_all_other_fields(self):
        source = json.loads((contract.CONTENT / U2L8_LESSON).read_text(encoding="utf-8-sig"))
        reviewed_sources = verified_u2l8_explanations(source)
        self.assertEqual(len(reviewed_sources), 8)
        for location, kind, explanation, reviewed, previous, active in reviewed_sources:
            lesson, page, block = location
            self.assertEqual(previous["answers"], active["answers"])
            identity = f"block_{page - 1:04d}_{block - 1:04d}"
            self.assertEqual(
                layout._historical_source_format_contract(source, identity, previous)["code"],
                layout._historical_source_format_contract(source, identity, active)["code"])
            old = {"type": kind, "id": "stable", "question": "Original question",
                   "code": "Original C++ source", "answers": ["original"],
                   "language": "cpp", "explanation": "Previous wording"}
            current = dict(old, **contract.FILL_CODE_REVISIONS.get(location, {}), explanation=explanation)
            relative = str((contract.CONTENT / lesson).relative_to(contract.ROOT))
            self.assertTrue(contract.preserves_previous_exercise(relative, page - 1, block - 1, old, current))
            for key in ("id", "type", "question", "code", "answers", "language", "explanation"):
                changed = dict(current, **{key: ["changed"] if key == "answers" else "CHANGED"})
                with self.subTest(page=page, block=block, changed=key):
                    self.assertFalse(contract.preserves_previous_exercise(relative, page - 1, block - 1, old, changed))
            self.assertFalse(contract.preserves_previous_exercise(relative, page - 1, block, old, current))

    def test_u2l8_later_source_formats_preserve_real_blocks_archives_and_editorial_proofs(self):
        source = json.loads((contract.CONTENT / U2L8_LESSON).read_text(encoding="utf-8-sig"))
        original = copy.deepcopy(source)
        published = source
        reviewed_sources = verified_u2l8_explanations(source)
        identity = "block_0000_0004"
        for revision in range(2):
            with self.subTest(revision=revision):
                formatted = copy.deepcopy(source)
                before = layout._blocks(source)[identity]
                after = layout._blocks(formatted)[identity]
                after["code"] = "\n" + before["code"]
                metadata = formatted["stateLayout"]
                archives = metadata.setdefault("sourceFormatRevisions", [])
                if not any(entry["id"] == identity for entry in archives):
                    # A first format archives its complete real predecessor;
                    # later formats retain that same canonical publication.
                    archives.append({"id": identity, "block": copy.deepcopy(before)})
                metadata["editorial"].append({
                    "id": identity, "beforeSha256": layout.fingerprint(before),
                    "afterSha256": layout.fingerprint(after)})
                layout.validate_transition(source, formatted)
                previous_archives = source["stateLayout"].get("sourceFormatRevisions", [])
                self.assertEqual(previous_archives, archives[:len(previous_archives)])
                previous_history = source["stateLayout"]["editorial"]
                self.assertEqual(previous_history, metadata["editorial"][:len(previous_history)])
                self.assertEqual(
                    {key: value for key, value in source["stateLayout"].items()
                     if key not in {"editorial", "sourceFormatRevisions"}},
                    {key: value for key, value in metadata.items()
                     if key not in {"editorial", "sourceFormatRevisions"}})
                later_sources = verified_u2l8_explanations(formatted)
                self.assertEqual(len(later_sources), 8)
                for old_record, new_record in zip(reviewed_sources, later_sources):
                    self.assertEqual(old_record[:5], new_record[:5])
                    location, _, _, _, _, active = new_record
                    _, page, block = location
                    owner = f"block_{page - 1:04d}_{block - 1:04d}"
                    self.assertIs(active, layout._blocks(formatted)[owner])
                    if owner != identity:
                        self.assertEqual(old_record[5], active)
                self.assertEqual("\n" + before["code"], after["code"])
                self.assertEqual(original, published)
                source, reviewed_sources = formatted, later_sources

    def test_u2l8_rejects_unproven_active_text_and_rehashed_program_or_answer_changes(self):
        source = json.loads((contract.CONTENT / U2L8_LESSON).read_text(encoding="utf-8-sig"))
        identity = "block_0000_0004"
        original = layout._blocks(source)[identity]
        for field, value in (("explanation", original["explanation"] + " Texto sin procedencia."),
                             ("answers", ["'Z'"]), ("code", original["code"] + "\n// Alterado")):
            changed = copy.deepcopy(source)
            active = layout._blocks(changed)[identity]
            active[field] = value
            if field != "explanation":
                proofs = [edge for edge in changed["stateLayout"]["editorial"]
                          if edge["id"] == identity]
                if proofs:
                    proofs[-1]["afterSha256"] = layout.fingerprint(active)
                else:
                    changed["stateLayout"]["editorial"].append({
                        "id": identity, "beforeSha256": layout.fingerprint(original),
                        "afterSha256": layout.fingerprint(active),
                    })
            with self.subTest(field=field), self.assertRaises(ValueError):
                verified_u2l8_explanations(changed)

    def test_u2l8_rejects_false_archive_provenance_and_reading_descriptors(self):
        source = json.loads((contract.CONTENT / U2L8_LESSON).read_text(encoding="utf-8-sig"))
        changed = copy.deepcopy(source)
        changed["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
        with self.assertRaises(ValueError):
            verified_u2l8_explanations(changed)
        changed = copy.deepcopy(source)
        marker = next(field for field in changed["textSemantics"]["fields"]
                      if (field["block"], field["path"]) == ("block_0000_0004", "/explanation"))
        marker["sourceSha256"] = "0" * 64
        with self.assertRaises(ValueError):
            verified_u2l8_explanations(changed)
        for index, edge in enumerate(source["stateLayout"]["editorial"]):
            if "bibliography" not in edge or edge["id"] != "block_0002_0002":
                continue
            changed = copy.deepcopy(source)
            changed["stateLayout"]["editorial"][index]["bibliography"]["previousBlock"][
                "explanation"] += " Fuente inventada."
            with self.assertRaises(ValueError):
                verified_u2l8_explanations(changed)
        for index, snapshot in enumerate(source["stateLayout"].get("retirementHistory", [])):
            if U2L8_RETIRED_EDITOR not in snapshot["retired"]:
                continue
            changed = copy.deepcopy(source)
            historical = changed["stateLayout"]["retirementHistory"][index]
            for page, record in zip(historical["pages"], historical["current"]):
                if "block_0000_0004" in record["blocks"]:
                    page["blocks"][record["blocks"].index("block_0000_0004")][
                        "explanation"] += " Fuente inventada."
            with self.assertRaises(ValueError):
                verified_u2l8_explanations(changed)
            changed = copy.deepcopy(source)
            for edge in changed["stateLayout"]["editorial"]:
                if edge["id"] == "block_0000_0004":
                    edge.pop("reviewPaths", None)
            with self.assertRaises(ValueError):
                verified_u2l8_explanations(changed)
            changed = copy.deepcopy(source)
            changed["textSemantics"]["fields"] = [field for field in changed["textSemantics"]["fields"]
                if (field["block"], field["path"]) != ("block_0000_0004", "/explanation")]
            with self.assertRaises(ValueError):
                verified_u2l8_explanations(changed)

    def test_italic_styles_are_explicit_optional_and_do_not_rewrite_original_text(self):
        for style in ("bold", "italic", "bold_italic"):
            self.assertTrue(self.emphasis([{"text": "objeto", "style": style}]))
        for style in (None, "", "BOLD", "italics", 1, False, [], {}):
            with self.subTest(style=style):
                self.assertFalse(self.emphasis([{"text": "objeto", "style": style}]))

    def test_note_emphasis_selects_only_its_original_body_and_preserves_the_localized_heading(self):
        body = "La cadena conserva el valor (Stroustrup, 2024)."
        block = {"type": "note", "text": body,
                 "emphasis": [{"text": "(Stroustrup, 2024)", "style": "italic"}]}
        before = copy.deepcopy(block)
        contract.validate_paragraph_emphasis_contract(block, "note fixture")
        self.assertEqual([], contract.ERRORS)
        self.assertEqual(before, block)
        self.assertTrue(set(block) <= contract.FIELDS["note"])
        for prefix in ("Nota", "Note", "Nota: ", "Remarque"):
            contract.ERRORS.clear()
            invalid = dict(block, emphasis=[{"text": prefix, "style": "italic"}])
            contract.validate_paragraph_emphasis_contract(invalid, "localized UI prefix is absent from body")
            self.assertTrue(contract.ERRORS)
        for style in ("bold", "italic", "bold_italic"):
            contract.ERRORS.clear()
            contract.validate_paragraph_emphasis_contract(
                dict(block, emphasis=[{"text": "valor", "style": style}]), "note style fixture")
            self.assertEqual([], contract.ERRORS)

    def test_note_repetitions_invalid_styles_and_other_block_families_keep_their_guards(self):
        body = "valor y valor"
        for emphasis in ([{"text": "valor"}], [{"text": "valor", "occurrence": 3}],
                         [{"text": "valor", "occurrence": 2, "style": "unsupported"}],
                         [{"text": "valor", "occurrence": 2, "extra": True}]):
            contract.ERRORS.clear()
            contract.validate_paragraph_emphasis_contract(
                {"type": "note", "text": body, "emphasis": emphasis}, "invalid note fixture")
            self.assertTrue(contract.ERRORS)
        contract.ERRORS.clear()
        contract.validate_paragraph_emphasis_contract(
            {"type": "note", "text": body, "emphasis": [{"text": "valor", "occurrence": 2, "style": "italic"}]},
            "explicit note occurrence")
        self.assertEqual([], contract.ERRORS)
        for kind in contract.FIELDS.keys() - {"paragraph", "note"}:
            contract.ERRORS.clear()
            self.assertNotIn("emphasis", contract.FIELDS[kind])
            contract.validate_paragraph_emphasis_contract(
                {"type": kind, "text": body, "emphasis": [{"text": "y"}]}, "unsupported fixture")
            self.assertTrue(contract.ERRORS)

    def test_verified_conversion_alternatives_do_not_create_holes_or_license_new_answers(self):
        lesson = "objetos_y_estado/lesson_conversiones_y_deduccion.json"
        location = (lesson, 4, 6)
        alternatives = [["double", "float", "long double", "double long"]]
        self.assertEqual(alternatives, contract.INLINE_ANSWER_ALTERNATIVES[location])
        relative = str((contract.CONTENT / lesson).relative_to(contract.ROOT))
        old = {"type": "inline_fill_code", "question": "Same", "template": "static_cast<[]>(total)",
               "answers": ["double"]}
        current = dict(old, answerAlternatives=alternatives)
        contract.validate_answer_contract(current, "fixture")
        self.assertEqual([], contract.ERRORS)
        self.assertTrue(contract.preserves_previous_exercise(relative, 3, 5, old, current))
        for key, value in (("answers", ["float"]), ("template", "changed []"),
                           ("answerAlternatives", [["double", "int"]])):
            self.assertFalse(contract.preserves_previous_exercise(relative, 3, 5, old, dict(current, **{key: value})))
        self.assertFalse(contract.preserves_previous_exercise(relative, 3, 6, old, current))


if __name__ == "__main__":
    unittest.main()


"""Ordinary prose revisions preserve authentic owners and protected fields."""
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
from functools import lru_cache
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import ordinary_explanation_fixture_source as fixtures
import stable_state_layout as layout
import validar_json_programacion as contract


PREFIX = fixtures.PREFIX.format(locale="es")
SYNTHETIC_PATH = PREFIX + "qa/lesson_ordinary_feedback.json"


@lru_cache(maxsize=None)
def cached_candidate(name):
    return fixtures.candidate(name)


def candidate(name):
    return deepcopy(cached_candidate(name))


def blocks(before, after, identity):
    return layout._blocks(before)[identity], layout._blocks(after)[identity]


def preserved(name, before, after, **kwargs):
    case = fixtures.CASES[name]
    old, new = blocks(before, after, case["identity"])
    return contract.preserves_previous_exercise(PREFIX + case["lesson"], *case["historical"], old, new,
        previous_document=before, current_document=after, **kwargs)


def repoint_last_edge(document, identity):
    edge = next(edge for edge in reversed(document["stateLayout"]["editorial"]) if edge["id"] == identity)
    edge["afterSha256"] = layout.fingerprint(layout._blocks(document)[identity])


def refresh_field(document, identity, path):
    fields = document.setdefault("textSemantics", {"version": 1, "fields": []})["fields"]
    fields[:] = [field for field in fields if (field["block"], field["path"]) != (identity, path)]
    fields.append(layout.text_semantics.make_field(document, identity, path, []))


def with_revised_question(document, identity):
    """Append a compatible question revision without discarding existing spans."""
    question = layout._blocks(document)[identity]["question"]
    markers = document.get("textSemantics", {}).get("fields", [])
    previous = next((field for field in markers
                     if (field["block"], field["path"]) == (identity, "/question")), None)
    spans = deepcopy(previous["spans"]) if previous is not None else []
    return layout.revise_ordinary_editorial(document, identity, {
        "question": {"text": question + " Otra pregunta QA.", "spans": spans},
    })


def synthetic(kind):
    payloads = {
        "multiple_choice": {"options": ["A", "B"], "correctIndex": 1},
        "fill_code": {"language": "cpp", "code": "int x = [];", "answers": ["7"]},
        "output_prediction": {"language": "cpp", "code": 'std::cout << "A";', "answers": ["A"]},
        "order_code": {"language": "cpp", "fragments": ["int x = 1;", "++x;"], "correctOrder": [0, 1]},
        "drag_fill_code": {"language": "cpp", "template": "int x = [];", "choices": ["1", "2"], "correctOrder": [0]},
        "inline_fill_code": {"language": "cpp", "template": "int x = [];", "answers": ["1"]},
    }
    raw = {"id": "cpp_fundamentals__qa__ordinary_feedback", "title": "Fixture, no curricular lesson",
           "pages": [{"blocks": [dict(type=kind, question="Pregunta QA", explanation="Explicación QA inicial.",
                                      **payloads[kind])]}]}
    before = layout.prepare_v2(raw, "es", {"es": raw})
    draft = fixtures.replace_explanation(before, "block_0000_0000", "Aclaración QA completamente nueva.", [])
    return before, layout.prepare_v2(draft, "es", {"es": before}, before)


def synthetic_missing_explanation(kind):
    # Reuse all existing response contracts; omit only the optional prose
    # from the INITIAL published source, before creating its v2 provenance.
    anchored, _ = synthetic(kind)
    initial = deepcopy(anchored)
    initial.pop("stateLayout", None)
    initial.pop("textSemantics", None)
    del initial["pages"][0]["blocks"][0]["explanation"]
    return layout.prepare_v2(initial, "es", {"es": initial})


class OrdinaryExplanationRevisionTest(unittest.TestCase):
    def test_optional_missing_explanation_is_authored_without_changing_answers(self):
        for kind in sorted(contract.REVIEW_FEEDBACK_TYPES):
            with self.subTest(kind=kind):
                before = synthetic_missing_explanation(kind)
                identity = "block_0000_0000"
                old = deepcopy(layout._blocks(before)[identity])
                self.assertNotIn("explanation", old)
                wording = f"Explicación QA del tipo {kind}: datos, operación y resultado."
                after = layout.revise_ordinary_editorial(before, identity, {
                    "explanation": {"text": wording, "spans": []}
                })
                new = layout._blocks(after)[identity]
                self.assertEqual(dict(old, explanation=wording), new)
                self.assertEqual(("explanation",), layout.ordinary_editorial_fields(old, new))
                self.assertTrue(contract.preserves_ordinary_explanation_revision(
                    SYNTHETIC_PATH, 0, 0, old, new, before, after))
                self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
                self.assertEqual(before["stateLayout"]["family"], after["stateLayout"]["family"])
                self.assertEqual(before["stateLayout"]["origin"], after["stateLayout"]["origin"])
                self.assertEqual(before["stateLayout"]["current"], after["stateLayout"]["current"])
                self.assertEqual(before["stateLayout"]["editorial"],
                                 after["stateLayout"]["editorial"][:-1])
                self.assertEqual({
                    "id": identity, "beforeSha256": layout.fingerprint(old),
                    "afterSha256": layout.fingerprint(new)
                }, after["stateLayout"]["editorial"][-1])
                marker = next(field for field in after["textSemantics"]["fields"]
                              if (field["block"], field["path"]) == (identity, "/explanation"))
                self.assertEqual([], marker["spans"])
                self.assertEqual(wording,
                    layout.text_semantics.validate(after)[identity]["/explanation"]["source"])
                layout.validate_transition(before, after)
                self.assertNotIn("explanation", layout._blocks(before)[identity])

    def test_published_u1l4_missing_explanation_preserves_original_activity(self):
        """Check a relocated, published drag-fill owner with a real state history.

        This is a read-only test of the shipping lesson, not permission to
        rewrite its JSON or to special-case its question/answer. Work authors
        the actual explanation through the same generic mechanism.
        """
        relative = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                    "programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json")
        # Pin the exact previous publication. Work may legitimately add
        # /explanation to the current lesson later; that must not break this
        # regression or cause it to test an already edited source as baseline.
        # Use the shared historical main/Codex ancestor so both Windows
        # checkouts retain this fixture even after Work updates the lesson.
        commit = "5607e7e503a3240314b82bbd8d829166e8c117de"
        expected_blob = "3d2017f3c1d9feb842fb687f13e5569d6dbc7304"
        repository = Path(__file__).resolve().parents[2]
        actual_blob = subprocess.check_output(
            ["git", "rev-parse", f"{commit}:{relative}"], cwd=repository,
            text=True, encoding="utf-8").strip()
        self.assertEqual(expected_blob, actual_blob)
        raw = subprocess.check_output(
            ["git", "show", f"{commit}:{relative}", "--"], cwd=repository)
        published = layout.parse_json(raw.decode("utf-8-sig"))
        layout.validate(published)
        stable_id = "block_0004_0001"
        current_positions = published["stateLayout"]["current"]
        matches = [(p, b) for p, page in enumerate(current_positions)
                   for b, identity in enumerate(page["blocks"]) if identity == stable_id]
        self.assertEqual(1, len(matches))
        current_position = matches[0]
        original_address = layout.validate(published)[current_position]
        owner = layout._blocks(published)[stable_id]
        self.assertEqual(("drag_fill_code", "piezas_estructura"),
                         (owner["type"], owner["id"]))
        self.assertNotIn("explanation", owner)
        self.assertEqual([0, 1, 2], owner["correctOrder"])
        self.assertEqual(5, len(owner["choices"]))

        wording = ("Cada hueco corresponde a una parte de la estructura. "
                   "La función se llama main, << envía el texto a std::cout "
                   "y el punto y coma termina la instrucción.")
        drafted = layout.revise_ordinary_editorial(published, stable_id, {
            "explanation": {"text": wording, "spans": []}
        })
        current = layout._blocks(drafted)[stable_id]
        self.assertEqual(dict(owner, explanation=wording), current)
        self.assertEqual(published["stateLayout"]["origin"],
                         drafted["stateLayout"]["origin"])
        self.assertEqual(published["stateLayout"]["current"],
                         drafted["stateLayout"]["current"])
        self.assertEqual(published["stateLayout"]["editorial"],
                         drafted["stateLayout"]["editorial"][:-1])
        self.assertEqual(layout.state_fingerprint(owner),
                         layout.state_fingerprint(current))
        self.assertTrue(contract.preserves_previous_exercise(
            relative, *original_address, owner, current,
            previous_document=published, current_document=drafted))
        self.assertFalse(contract.preserves_previous_exercise(
            relative, *original_address, owner, current))

        # An authentic new explanation does not authorize other changes, even
        # if a caller recalculates the visible editorial hash afterward.
        for altered_field in ("correctOrder", "question", "choices"):
            invalid = deepcopy(drafted)
            changed = layout._blocks(invalid)[stable_id]
            if altered_field == "correctOrder":
                changed[altered_field] = [1, 0, 2]
            elif altered_field == "question":
                changed[altered_field] += " Otro requisito."
            else:
                changed[altered_field][0] = "incorrecta"
            invalid["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(changed)
            with self.subTest(field=altered_field):
                self.assertFalse(contract.preserves_previous_exercise(
                    relative, *original_address, owner, changed,
                    previous_document=published, current_document=invalid))
        self.assertNotIn("explanation", layout._blocks(published)[stable_id])

    def test_authenticated_choice_projection_retains_historical_answer(self):
        """The legacy exercise check must also accept proven option-only edits."""
        before = synthetic_missing_explanation("multiple_choice")
        identity = "block_0000_0000"
        old = layout._blocks(before)[identity]
        self.assertEqual("multiple_choice", old["type"])
        edited = layout.revise_choice_editorial(before, identity, 0,
                                                 "Opción A revisada sin alterar la respuesta.")
        self.assertNotEqual(old, layout._blocks(edited)[identity])
        projection = layout.align_for_previous_validation(before, edited)
        historical = layout._blocks(projection)[identity]
        self.assertEqual(old, historical)
        self.assertTrue(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 0, old, historical,
            previous_document=before, current_document=edited))
        self.assertEqual(old["correctIndex"], layout._blocks(edited)[identity]["correctIndex"])
        self.assertEqual(old["options"][1], layout._blocks(edited)[identity]["options"][1])

    def test_missing_optional_explanation_remains_a_protected_authored_field(self):
        before = synthetic_missing_explanation("drag_fill_code")
        identity = "block_0000_0000"
        old = layout._blocks(before)[identity]
        after = layout.revise_ordinary_editorial(before, identity, {
            "explanation": {"text": "Une cada pieza para formar la instrucción completa.", "spans": []}
        })
        new = layout._blocks(after)[identity]
        self.assertTrue(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 0, old, new,
            previous_document=before, current_document=after))
        self.assertFalse(contract.preserves_ordinary_explanation_revision(
            SYNTHETIC_PATH, 0, 0, old, new, None, None))
        self.assertFalse(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 0, old, new))
        self.assertEqual({"explanation"}, set(new) - set(old))

        # A hash alone does not authorize a new field: the exact ordered
        # editorial edge and source-marked /explanation must accompany it.
        for mutation in ("no-proof", "no-mark", "stale-mark", "changed-answer",
                         "extra-field", "changed-question", "deleted-mark", "empty-text"):
            invalid = deepcopy(after)
            current = layout._blocks(invalid)[identity]
            if mutation == "no-proof":
                invalid["stateLayout"]["editorial"].clear()
            elif mutation == "no-mark":
                invalid.pop("textSemantics")
            elif mutation == "stale-mark":
                invalid["textSemantics"]["fields"][0]["sourceSha256"] = "0" * 64
            elif mutation == "changed-answer":
                current["correctOrder"] = [1, 0, 2]
            elif mutation == "extra-field":
                current["notAuthorized"] = True
            elif mutation == "changed-question":
                current["question"] += " ¿Otra condición?"
            elif mutation == "deleted-mark":
                invalid["textSemantics"]["fields"] = []
            else:
                current["explanation"] = "   "
            with self.subTest(mutation=mutation):
                if mutation in ("changed-answer", "extra-field", "changed-question", "empty-text"):
                    invalid["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(current)
                self.assertFalse(contract.preserves_ordinary_explanation_revision(
                    SYNTHETIC_PATH, 0, 0, old, current, before, invalid))

        # A removed field, an absent question and a malformed old value do
        # not become a general license to introduce arbitrary text.
        self.assertEqual((), layout.ordinary_editorial_fields(
            {"type": "drag_fill_code", "question": "Texto anterior"},
            {"type": "drag_fill_code", "explanation": "Un texto distinto"}))
        self.assertEqual((), layout.ordinary_editorial_fields(
            {"type": "drag_fill_code", "explanation": ""},
            {"type": "drag_fill_code", "explanation": "Un texto distinto"}))
        with self.assertRaises(ValueError):
            layout.revise_ordinary_editorial(before, identity, {
                "explanation": {"text": "  ", "spans": []}
            })
        with self.assertRaises(ValueError):
            layout.revise_ordinary_editorial(after, identity, {
                "explanation": {"text": None, "spans": []}
            })

    def test_optional_explanation_uses_original_identity_after_page_reorganization(self):
        # A completed unit may have moved the activity to a different page.
        # Historical (pn, bn) still identifies its original owner, whereas
        # the physical location can change. Do not pin this to a Work lesson.
        raw = {
            "id": "cpp_fundamentals__qa__ordinary_feedback",
            "title": "Reorganización sintética, no lección curricular",
            "pages": [
                {"blocks": [
                    {"type": "paragraph", "text": "Presenta cada pieza antes del intento."},
                    {"type": "drag_fill_code", "id": "piezas_qa",
                     "question": "Completa la instrucción observada.",
                     "language": "cpp", "template": "int []() { return 0[] }",
                     "choices": ["main", ";", "Main", "."], "correctOrder": [0, 1]}
                ]},
                {"blocks": [
                    {"type": "multiple_choice", "id": "eleccion_qa",
                     "question": "¿Cuál es la salida?",
                     "options": ["Uno", "Dos"], "correctIndex": 0,
                     "explanation": "La primera opción coincide con el caso."}
                ]}
            ]
        }
        # Authenticate the unrelated question marker as part of the
        # initial published family, rather than adding it unproved later.
        bootstrap = layout.prepare(raw)
        bootstrap["textSemantics"] = {"version": 1, "fields": [
            layout.text_semantics.make_field(
                bootstrap, "block_0001_0000", "/question", [])
        ]}
        layout.validate(bootstrap)
        original = layout.prepare_v2(bootstrap, "es", {"es": bootstrap})
        moved = layout.reorder(original, [
            {"id": "page_0000", "blocks": ["block_0000_0000"]},
            {"id": "page_0001", "blocks": ["block_0001_0000", "block_0000_0001"]}
        ])
        identity = "block_0000_0001"
        self.assertEqual({"id": "page_0000", "blocks": ["block_0000_0000"]},
                         moved["stateLayout"]["current"][0])
        self.assertEqual(identity, moved["stateLayout"]["current"][1]["blocks"][1])
        before = deepcopy(layout._blocks(moved)[identity])
        self.assertNotIn("explanation", before)
        self.assertEqual((0, 1), layout.validate(moved)[1, 1])
        self.assertEqual(before, moved["pages"][1]["blocks"][1])
        marker_before = deepcopy(original["textSemantics"]["fields"][0])

        authored = layout.revise_ordinary_editorial(moved, identity, {
            "explanation": {
                "text": "Identifica la función, el operador y el signo final de la instrucción.",
                "spans": []
            }
        })
        after = layout._blocks(authored)[identity]
        self.assertEqual(dict(before, explanation=after["explanation"]), after)
        self.assertEqual(marker_before, authored["textSemantics"]["fields"][0])
        self.assertEqual(2, len(authored["textSemantics"]["fields"]))
        self.assertEqual((identity, "/explanation"), (
            authored["textSemantics"]["fields"][1]["block"],
            authored["textSemantics"]["fields"][1]["path"]))
        self.assertEqual(moved["stateLayout"]["family"], authored["stateLayout"]["family"])
        self.assertEqual(moved["stateLayout"]["origin"], authored["stateLayout"]["origin"])
        self.assertEqual(moved["stateLayout"]["current"], authored["stateLayout"]["current"])
        self.assertEqual(layout.state_fingerprint(before), layout.state_fingerprint(after))
        self.assertTrue(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 1, before, after,
            previous_document=moved, current_document=authored))
        self.assertFalse(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 1, 1, before, after,
            previous_document=moved, current_document=authored))
        self.assertFalse(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 1, before, after))
        layout.validate_transition(moved, authored)

        # Subsequent wording updates must use the ordinary revision flow,
        # preserve the newly published descriptor and append another edge.
        revised = layout.revise_ordinary_editorial(authored, identity, {
            "explanation": {
                "text": "Justifica por separado cada pieza, operador y separador.",
                "spans": []
            }
        })
        current = layout._blocks(revised)[identity]
        self.assertTrue(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 1, before, current,
            previous_document=moved, current_document=revised))
        self.assertEqual(authored["stateLayout"]["editorial"],
                         revised["stateLayout"]["editorial"][:-1])
        self.assertEqual(moved["stateLayout"]["current"],
                         revised["stateLayout"]["current"])
        self.assertEqual(marker_before, revised["textSemantics"]["fields"][0])
        self.assertEqual(layout.state_fingerprint(before),
                         layout.state_fingerprint(current))
        layout.validate_transition(authored, revised)

    def test_combined_question_revision_and_new_explanation_is_atomic(self):
        before = synthetic_missing_explanation("drag_fill_code")
        identity = "block_0000_0000"
        original = deepcopy(layout._blocks(before)[identity])
        fields = {
            "question": {"text": "Indica y ordena cada pieza de la instrucción.", "spans": []},
            "explanation": {"text": "La instrucción correcta conserva el orden y el signo final.",
                            "spans": []}
        }
        after = layout.revise_ordinary_editorial(before, identity, fields)
        current = layout._blocks(after)[identity]
        self.assertEqual(("question", "explanation"),
                         layout.ordinary_editorial_fields(original, current))
        self.assertEqual({**original, "question": fields["question"]["text"],
                          "explanation": fields["explanation"]["text"]}, current)
        self.assertEqual(layout.state_fingerprint(original),
                         layout.state_fingerprint(current))
        self.assertEqual(["/question", "/explanation"],
                         [field["path"] for field in after["textSemantics"]["fields"]])
        self.assertEqual(1, len(after["stateLayout"]["editorial"]))
        self.assertTrue(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 0, original, current,
            previous_document=before, current_document=after))

        # Refuse partial and forged proof for a combined revision.
        invalid = deepcopy(after)
        invalid["stateLayout"]["editorial"][0]["afterSha256"] = "0" * 64
        self.assertFalse(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 0, original, layout._blocks(invalid)[identity],
            previous_document=before, current_document=invalid))
        missing = deepcopy(after)
        missing["textSemantics"]["fields"] = missing["textSemantics"]["fields"][:1]
        self.assertFalse(contract.preserves_previous_exercise(
            SYNTHETIC_PATH, 0, 0, original, layout._blocks(missing)[identity],
            previous_document=before, current_document=missing))

    def test_all_five_work_proposals_keep_every_other_raw_field_and_provenance(self):
        for name, case in fixtures.CASES.items():
            with self.subTest(name=name):
                before, after, family = candidate(name)
                old, new = blocks(before, after, case["identity"])
                self.assertEqual(dict(old, explanation=case["after"]), new)
                self.assertTrue(preserved(name, before, after))
                self.assertFalse(contract.preserves_previous_exercise(
                    PREFIX + case["lesson"], *case["historical"], old, new))
                self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
                self.assertEqual(layout.lineage(before), layout.lineage(after))
                self.assertEqual(before["stateLayout"]["family"], after["stateLayout"]["family"])
                self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:-1])
                self.assertEqual({"id": case["identity"], "beforeSha256": layout.fingerprint(old),
                                  "afterSha256": layout.fingerprint(new)}, after["stateLayout"]["editorial"][-1])
                layout.check_localized_family([after, family["en"], family["fr"]], ["es", "en", "fr"])
                before_overlay = {(field["block"], field["path"]): field for field in before["textSemantics"]["fields"]}
                after_overlay = {(field["block"], field["path"]): field for field in after["textSemantics"]["fields"]}
                self.assertEqual(before_overlay.keys(), after_overlay.keys())
                for owner_path, field in before_overlay.items():
                    if owner_path != (case["identity"], "/explanation"):
                        self.assertEqual(field, after_overlay[owner_path])
                self.assertEqual(case["after"], layout.text_semantics.validate(after)[case["identity"]]["/explanation"]["source"])

    def test_any_ordinary_type_and_new_wording_use_the_same_rule(self):
        for kind in sorted(contract.REVIEW_FEEDBACK_TYPES):
            with self.subTest(kind=kind):
                before, after = synthetic(kind)
                old, new = blocks(before, after, "block_0000_0000")
                self.assertTrue(contract.preserves_previous_exercise(SYNTHETIC_PATH, 0, 0, old, new,
                    previous_document=before, current_document=after))
                self.assertNotIn("textSemantics", before)
                self.assertEqual([], after["textSemantics"]["fields"][0]["spans"])

    def test_chained_new_wording_needs_no_phrase_registration(self):
        before, after, family = candidate("reset_sum")
        identity = fixtures.CASES["reset_sum"]["identity"]
        draft = fixtures.replace_explanation(after, identity, "Otro desarrollo QA explica cada visita sin cambiar la respuesta.", [])
        updated = layout.prepare_v2(draft, "es", dict(family, es=after), after)
        self.assertTrue(preserved("reset_sum", after, updated))
        self.assertTrue(preserved("reset_sum", before, updated))
        self.assertEqual(after["stateLayout"]["editorial"], updated["stateLayout"]["editorial"][:-1])

    def test_original_program_answers_rules_identity_and_guides_cannot_change(self):
        for name, case in fixtures.CASES.items():
            before, after, _ = candidate(name)
            original = layout._blocks(before)[case["identity"]]
            for field, value in original.items():
                if field == "explanation":
                    continue
                changed = deepcopy(after)
                replacement = value + " NO AUTORIZADO" if isinstance(value, str) else {"changed": value}
                layout._blocks(changed)[case["identity"]][field] = replacement
                repoint_last_edge(changed, case["identity"])
                with self.subTest(name=name, field=field):
                    self.assertFalse(preserved(name, before, changed))
            for field, value in (("id", "otra_actividad"), ("campo_nuevo", True), ("reviewFeedback", "Guía colada")):
                changed = deepcopy(after)
                layout._blocks(changed)[case["identity"]][field] = value
                repoint_last_edge(changed, case["identity"])
                with self.subTest(name=name, added=field):
                    self.assertFalse(preserved(name, before, changed))

    def test_authenticated_question_and_explanation_revisions_are_compatible(self):
        for name in ("empty_vector", "boundary_cases"):
            before, after, _ = candidate(name)
            identity = fixtures.CASES[name]["identity"]
            changed = with_revised_question(after, identity)
            with self.subTest(name=name):
                layout.validate_transition(before, changed)
                self.assertTrue(preserved(name, before, changed))
                self.assertEqual(after["stateLayout"]["editorial"],
                                 changed["stateLayout"]["editorial"][:-1])
                old, new = blocks(before, changed, identity)
                self.assertFalse(contract.preserves_previous_exercise(
                    PREFIX + fixtures.CASES[name]["lesson"],
                    *fixtures.CASES[name]["historical"], old, new))

    def test_revised_question_requires_its_own_marking_and_provenance(self):
        for name in ("empty_vector", "boundary_cases"):
            before, after, _ = candidate(name)
            identity = fixtures.CASES[name]["identity"]
            valid = with_revised_question(after, identity)
            for mutation in ("missing_marker", "stale_marker", "missing_edge"):
                changed = deepcopy(valid)
                fields = changed["textSemantics"]["fields"]
                if mutation == "missing_marker":
                    fields[:] = [field for field in fields
                                 if (field["block"], field["path"]) != (identity, "/question")]
                elif mutation == "stale_marker":
                    marker = next(field for field in fields
                                  if (field["block"], field["path"]) == (identity, "/question"))
                    marker["sourceSha256"] = "0" * 64
                else:
                    changed["stateLayout"]["editorial"].pop()
                with self.subTest(name=name, mutation=mutation):
                    self.assertFalse(preserved(name, before, changed))

    def test_valid_state_chain_does_not_hide_review_edits(self):
        name = "case_sensitive_label"
        before, after, _ = candidate(name)
        identity = fixtures.CASES[name]["identity"]
        changed = deepcopy(after)
        layout._blocks(changed)[identity]["reviewFeedback"][0] += " Otra guía QA."
        refresh_field(changed, identity, "/reviewFeedback/0")
        repoint_last_edge(changed, identity)
        layout.validate_transition(before, changed)
        self.assertFalse(preserved(name, before, changed))

    def test_ownership_comes_from_historical_address_not_visible_position_or_block_id(self):
        name = "case_sensitive_label"
        before, after, _ = candidate(name)
        case = fixtures.CASES[name]
        old, new = blocks(before, after, case["identity"])
        for page, block in ((6, 8), (8, 0), (9, 1)):
            self.assertFalse(contract.preserves_previous_exercise(PREFIX + case["lesson"], page, block, old, new,
                previous_document=before, current_document=after))
        for relative in (PREFIX + "colecciones_y_recorridos/lesson_otra.json",
                         PREFIX.replace("/es/", "/en/") + case["lesson"]):
            self.assertFalse(contract.preserves_ordinary_explanation_revision(relative, *case["historical"],
                old, new, before, after))
        for side in ("before", "after"):
            borrowed_before, borrowed_after = deepcopy(before), deepcopy(after)
            (borrowed_before if side == "before" else borrowed_after)["id"] += "_otro"
            self.assertFalse(contract.preserves_ordinary_explanation_revision(PREFIX + case["lesson"],
                *case["historical"], old, new, borrowed_before, borrowed_after))
        self.assertFalse(contract.preserves_ordinary_explanation_revision(PREFIX + case["lesson"],
            *case["historical"], dict(old, id="otro"), dict(new, id="otro"), before, after))

    def test_explicit_current_explanation_descriptor_is_required(self):
        name = "empty_vector"
        before, after, _ = candidate(name)
        identity = fixtures.CASES[name]["identity"]
        missing = deepcopy(after)
        missing["textSemantics"]["fields"] = [field for field in missing["textSemantics"]["fields"]
            if (field["block"], field["path"]) != (identity, "/explanation")]
        layout.validate_transition(before, missing)
        self.assertFalse(preserved(name, before, missing))
        for change in ("hash", "path", "owner", "range", "role", "duplicate"):
            malformed = deepcopy(after)
            field = next(field for field in malformed["textSemantics"]["fields"]
                         if (field["block"], field["path"]) == (identity, "/explanation"))
            if change == "hash": field["sourceSha256"] = "0" * 64
            elif change == "path": field["path"] = "/answers/0"
            elif change == "owner": field["block"] = "otro_propietario"
            elif change == "range": field["spans"][0]["start"] += 1
            elif change == "role": field["spans"][0]["role"] = "unregistered"
            else: malformed["textSemantics"]["fields"].append(deepcopy(field))
            with self.subTest(change=change): self.assertFalse(preserved(name, before, malformed))

    def test_missing_or_replaced_published_provenance_is_rejected(self):
        name = "range_copy"
        before, after, _ = candidate(name)
        for change in ("missing_edge", "wrong_before", "wrong_after", "rewritten_prefix", "owner", "family", "v1"):
            malformed = deepcopy(after)
            meta = malformed["stateLayout"]
            if change == "missing_edge": meta["editorial"].pop()
            elif change == "wrong_before": meta["editorial"][-1]["beforeSha256"] = "0" * 64
            elif change == "wrong_after": meta["editorial"][-1]["afterSha256"] = "0" * 64
            elif change == "rewritten_prefix": meta["editorial"][0]["beforeSha256"] = "0" * 64
            elif change == "owner": meta["editorial"][-1]["id"] = "block_0000_0000"
            elif change == "family": meta["family"]["es"]["sha256"] = "0" * 64
            else: malformed["stateLayout"] = {key: meta[key] for key in ("origin", "current")}; malformed["stateLayout"]["version"] = 1
            with self.subTest(change=change): self.assertFalse(preserved(name, before, malformed))

    def test_special_paired_token_option_guard_remains_closed(self):
        before = fixtures.paired_choice_document()
        identity = "block_0003_0004"
        after = fixtures.replace_explanation(before, identity, "Una explicación diferente no revisa esta pareja protegida.", [])
        old, new = blocks(before, after, identity)
        after["stateLayout"]["editorial"].append({"id": identity, "beforeSha256": layout.fingerprint(old),
                                                 "afterSha256": layout.fingerprint(new)})
        with self.assertRaisesRegex(ValueError, "same exact version"):
            layout.validate_transition(before, after)
        self.assertFalse(contract.preserves_ordinary_explanation_revision(PREFIX + fixtures.PAIRED_CHOICE_LESSON,
            3, 4, old, new, before, after))

    def test_empty_replacement_and_nonordinary_blocks_do_not_gain_permission(self):
        for kind in sorted(contract.REVIEW_FEEDBACK_TYPES):
            before, after = synthetic(kind)
            identity = "block_0000_0000"
            for text in ("", " \n\t", None, 9):
                malformed = deepcopy(after)
                layout._blocks(malformed)[identity]["explanation"] = text
                old, new = blocks(before, malformed, identity)
                with self.subTest(kind=kind, text=text):
                    self.assertFalse(contract.preserves_ordinary_explanation_revision(
                        SYNTHETIC_PATH, 0, 0, old, new, before, malformed))
            old, new = blocks(before, after, identity)
            for kind in ("cpp_compiler", "math_puzzle", "code", "paragraph"):
                self.assertFalse(contract.preserves_ordinary_explanation_revision(SYNTHETIC_PATH, 0, 0,
                    dict(old, type=kind), dict(new, type=kind), before, after))

    def test_pinned_sources_reject_even_parseable_replacements(self):
        for name in fixtures.CASES:
            for locale in ("es", "en", "fr"):
                payload = fixtures.historical_bytes(name, locale)
                self.assertEqual(payload, fixtures.verify_bytes(name, locale, payload))
                with self.assertRaises(ValueError): fixtures.verify_bytes(name, locale, payload + b"\n")
                with self.assertRaises(ValueError): fixtures.verify_bytes(name, locale, b"{}")


class OrdinaryExplanationMainTest(unittest.TestCase):
    def run_main(self, mutation=None):
        # Replace only Git transport in this isolated fixture tree. The real
        # repository/family/transition validator and main() all execute.
        sources, delivered = {}, {}
        for name, case in fixtures.CASES.items():
            before, after, _ = candidate(name)
            if mutation is not None:
                mutation(name, before, after)
            for locale in ("es", "en", "fr"):
                relative = fixtures.PREFIX.format(locale=locale) + case["lesson"]
                sources[relative] = fixtures.historical_bytes(name, locale)
                delivered[relative] = after if locale == "es" else json.loads(sources[relative])
        with tempfile.TemporaryDirectory(prefix="lgaed-explanation-qa-") as directory:
            root = Path(directory)
            (root / ".git").mkdir()  # Marker only, no checkout or repository.
            for relative, document in delivered.items():
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
            content = root / PREFIX
            course_units = []
            unit_names = sorted({Path(case["lesson"]).parent.name for case in fixtures.CASES.values()})
            for number, unit_name in enumerate(unit_names, 1):
                lessons = [delivered[PREFIX + case["lesson"]] for case in fixtures.CASES.values()
                           if Path(case["lesson"]).parent.name == unit_name]
                unit_id = "cpp_fundamentals__" + unit_name
                unit = {"courseId": "cpp_fundamentals", "unitId": unit_id, "title": unit_name,
                        "lessons": [{"number": i, "id": lesson["id"], "title": lesson["title"]}
                                    for i, lesson in enumerate(lessons, 1)]}
                (content / unit_name / ("unit_" + unit_name + ".json")).write_text(json.dumps(unit), encoding="utf-8")
                course_units.append({"number": number, "id": unit_id, "title": unit_name})
            (content / "course_cpp_fundamentals.json").write_text(json.dumps({"units": course_units}), encoding="utf-8")

            def git_run(command, **kwargs):
                self.assertEqual(root, Path(kwargs["cwd"]))
                if command == ["git", "rev-parse", "--verify", "--end-of-options", fixtures.COMMIT + "^{commit}"]:
                    output = fixtures.COMMIT + "\n"
                elif command == ["git", "ls-tree", "-r", "--name-only", "-z", fixtures.COMMIT,
                                 "--", layout.CONTENT_DIRECTORY]:
                    output = "\0".join(sorted(sources)) + "\0"
                elif command == ["git", "grep", "-l", "-z", "-F", '"stateLayout"', fixtures.COMMIT,
                                 "--", layout.CONTENT_DIRECTORY]:
                    output = "\0".join(fixtures.COMMIT + ":" + relative for relative in sorted(sources)
                                       if "stateLayout" in json.loads(sources[relative])) + "\0"
                elif (command[:2] == ["git", "show"] and
                      (len(command) == 3 or
                       (len(command) == 4 and command[3] == "--"))):
                    commit, relative = command[2].split(":", 1)
                    self.assertEqual(fixtures.COMMIT, commit)
                    output = sources[relative].decode("utf-8")
                else:
                    raise AssertionError("Unexpected Git operation: " + repr(command))
                return subprocess.CompletedProcess(command, 0, stdout=output, stderr="")

            def git_output(command, **kwargs):
                self.assertEqual(["git", "show"], command[:2])
                result = git_run(command, **kwargs).stdout
                return result if kwargs.get("text") else result.encode("utf-8")

            output = io.StringIO()
            with patch.object(contract, "ROOT", root), patch.object(contract, "CONTENT", content), \
                    patch.object(contract, "ERRORS", []), patch.object(subprocess, "run", side_effect=git_run), \
                    patch.object(subprocess, "check_output", side_effect=git_output), \
                    redirect_stdout(output), redirect_stderr(output):
                try:
                    contract.main(["--base-ref", fixtures.COMMIT])
                except SystemExit as error:
                    return error.code, output.getvalue()
            return 0, output.getvalue()

    def test_full_programming_validator_accepts_real_u1l4_explanation(self):
        """The actual CLI must accept a new explanation on the moved U1L4
        drag-fill owner, not merely the isolated authoring helper. The source
        remains pinned and the disposable tree never modifies curriculum.
        """
        repository = Path(__file__).resolve().parents[2]
        published = "5607e7e503a3240314b82bbd8d829166e8c117de"
        lesson_tail = ("programas_y_ejecucion/"
                       "lesson_fuente_compilacion_y_diagnostico.json")
        known_blobs = {
            "es": "3d2017f3c1d9feb842fb687f13e5569d6dbc7304",
            "en": "295f7c0ba99613395d252f7937ecce8012f64d0d",
            "fr": "8f02ef555262744255f9ffc4ee63dfdab776689d",
        }
        sources = {}
        for locale, expected_blob in known_blobs.items():
            relative = fixtures.PREFIX.format(locale=locale) + lesson_tail
            actual_blob = subprocess.check_output(
                ["git", "rev-parse", published + ":" + relative],
                cwd=repository, text=True, encoding="utf-8").strip()
            self.assertEqual(expected_blob, actual_blob)
            sources[relative] = subprocess.check_output(
                ["git", "show", published + ":" + relative], cwd=repository)
        spanish_path = fixtures.PREFIX.format(locale="es") + lesson_tail
        before = layout.parse_json(sources[spanish_path].decode("utf-8-sig"))
        stable_id = "block_0004_0001"
        old = layout._blocks(before)[stable_id]
        self.assertEqual(("drag_fill_code", "piezas_estructura"),
                         (old["type"], old["id"]))
        self.assertNotIn("explanation", old)
        wording = ("La función main inicia la ejecución; el operador << "
                   "envía Revision lista a std::cout y el punto y coma "
                   "termina la instrucción.")
        authored = layout.revise_ordinary_editorial(before, stable_id, {
            "explanation": {"text": wording, "spans": []},
        })
        self.assertEqual(dict(old, explanation=wording),
                         layout._blocks(authored)[stable_id])

        with tempfile.TemporaryDirectory(prefix="lgaed-u1l4-edition-qa-") as directory:
            root = Path(directory)
            (root / ".git").mkdir()
            for relative, payload in sources.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                if relative == spanish_path:
                    path.write_text(json.dumps(authored, ensure_ascii=False),
                                    encoding="utf-8")
                else:
                    path.write_bytes(payload)
            content = root / fixtures.PREFIX.format(locale="es")
            unit_id = "cpp_fundamentals__programas_y_ejecucion"
            unit = {
                "courseId": "cpp_fundamentals", "unitId": unit_id,
                "title": "Programas y ejecución",
                "lessons": [{"number": 1, "id": authored["id"],
                             "title": authored["title"]}],
            }
            (content / "programas_y_ejecucion" /
             "unit_programas_y_ejecucion.json").write_text(
                json.dumps(unit, ensure_ascii=False), encoding="utf-8")
            (content / "course_cpp_fundamentals.json").write_text(
                json.dumps({"units": [{"number": 1, "id": unit_id,
                                        "title": unit["title"]}]}),
                encoding="utf-8")

            def git_run(command, **kwargs):
                self.assertEqual(root, Path(kwargs["cwd"]))
                if command == ["git", "rev-parse", "--verify", "--end-of-options",
                               published + "^{commit}"]:
                    output = published + "\n"
                elif command == ["git", "ls-tree", "-r", "--name-only", "-z",
                                 published, "--", layout.CONTENT_DIRECTORY]:
                    output = "\0".join(sorted(sources)) + "\0"
                elif command == ["git", "grep", "-l", "-z", "-F", '"stateLayout"',
                                 published, "--", layout.CONTENT_DIRECTORY]:
                    output = "\0".join(
                        published + ":" + relative for relative in sorted(sources)
                        if "stateLayout" in json.loads(sources[relative])) + "\0"
                elif (command[:2] == ["git", "show"] and
                      (len(command) == 3 or
                       (len(command) == 4 and command[3] == "--"))):
                    commit, relative = command[2].split(":", 1)
                    self.assertEqual(published, commit)
                    output = sources[relative].decode("utf-8-sig")
                else:
                    raise AssertionError("Unexpected Git operation: " + repr(command))
                return subprocess.CompletedProcess(command, 0, stdout=output, stderr="")

            def git_output(command, **kwargs):
                self.assertEqual(["git", "show"], command[:2])
                value = git_run(command, **kwargs).stdout
                return value if kwargs.get("text") else value.encode("utf-8")

            # Rewriting the declared correct order is not an editorial
            # explanation, even when its new editorial hash is also forged.
            # The complete CLI must reject it on the same pinned family.
            forged = deepcopy(authored)
            changed = layout._blocks(forged)[stable_id]
            changed["correctOrder"] = [1, 0, 2]
            forged["stateLayout"]["editorial"][-1]["afterSha256"] = (
                layout.fingerprint(changed))
            for candidate, expected_status in ((authored, 0), (forged, 1)):
                (root / spanish_path).write_text(
                    json.dumps(candidate, ensure_ascii=False), encoding="utf-8")
                output = io.StringIO()
                with patch.object(contract, "ROOT", root), \
                        patch.object(contract, "CONTENT", content), \
                        patch.object(contract, "ERRORS", []), \
                        patch.object(subprocess, "run", side_effect=git_run), \
                        patch.object(subprocess, "check_output", side_effect=git_output), \
                        redirect_stdout(output), redirect_stderr(output):
                    try:
                        contract.main(["--course", "cpp_fundamentals",
                                       "--base-ref", published])
                    except SystemExit as error:
                        status = error.code
                    else:
                        status = 0
                self.assertEqual(expected_status, status, output.getvalue())
                if expected_status == 0:
                    self.assertIn("1 lecciones", output.getvalue())
        self.assertNotIn("explanation", layout._blocks(before)[stable_id])

    def test_main_accepts_all_five_authored_revisions_from_the_publication(self):
        code, output = self.run_main()
        self.assertEqual(0, code, output)
        self.assertIn("5 lecciones", output)

    def test_main_fast_path_checks_entire_choice_not_just_unchanged_prose(self):
        """Avoid redundant state validation but never exempt changed responses."""
        target = "boundary_cases"
        case = fixtures.CASES[target]
        relative = PREFIX + case["lesson"]
        identity = case["identity"]
        captured = contract.preserves_previous_exercise

        def unchanged_choice(name, before, after):
            if name == target:
                after.clear()
                after.update(deepcopy(before))

        with patch.object(contract, "preserves_previous_exercise", wraps=captured) as guard:
            code, output = self.run_main(unchanged_choice)
        self.assertEqual(0, code, output)
        self.assertFalse([
            call for call in guard.call_args_list
            if (len(call.args) >= 5 and call.args[0] == relative
                and call.args[3].get("type") == "multiple_choice")
        ], "An identical choice was needlessly sent through expensive legacy validators")

        before, _, _ = candidate(target)
        old = layout._blocks(before)[identity]
        # The prose remains literally identical in both invalid edits.
        for field in ("correctIndex", "options"):
            altered = deepcopy(old)
            if field == "correctIndex":
                altered[field] = (old[field] + 1) % len(old["options"])
            else:
                altered[field][0] += " Respuesta alterada."
            with self.subTest(field=field):
                self.assertEqual(old["question"], altered["question"])
                self.assertEqual(old["explanation"], altered["explanation"])
                self.assertFalse(contract.preserves_previous_exercise(
                    relative, *case["historical"], old, altered,
                    previous_document=before, current_document=before))

        def changed_answer(name, before, after):
            if name == target:
                after.clear()
                after.update(deepcopy(before))
                layout._blocks(after)[identity]["correctIndex"] = (
                    old["correctIndex"] + 1) % len(old["options"])
        code, output = self.run_main(changed_answer)
        self.assertEqual(1, code, output)

    def test_main_checks_multiple_choice_explanations_instead_of_skipping_them(self):
        def remove_marking(name, before, after):
            if name == "boundary_cases":
                identity = fixtures.CASES[name]["identity"]
                after["textSemantics"]["fields"] = [field for field in after["textSemantics"]["fields"]
                    if (field["block"], field["path"]) != (identity, "/explanation")]
        code, output = self.run_main(remove_marking)
        self.assertEqual(1, code, output)
        self.assertIn("explanation", output)

    def test_main_requires_current_marking_for_a_question_revision(self):
        for marking in ("authored", "missing", "stale"):
            def revise(name, before, after):
                if name != "boundary_cases":
                    return
                identity = fixtures.CASES[name]["identity"]
                revised = with_revised_question(after, identity)
                fields = revised["textSemantics"]["fields"]
                if marking == "missing":
                    fields[:] = [field for field in fields
                                 if (field["block"], field["path"]) != (identity, "/question")]
                elif marking == "stale":
                    marker = next(field for field in fields
                                  if (field["block"], field["path"]) == (identity, "/question"))
                    marker["sourceSha256"] = "0" * 64
                after.clear()
                after.update(revised)
            with self.subTest(marking=marking):
                code, output = self.run_main(revise)
                self.assertEqual(0 if marking == "authored" else 1, code, output)

    def test_main_checks_question_only_multiple_choice_revision(self):
        # Start from the published block, not the candidate whose explanation
        # already changed: that older scenario enters the explanation guard.
        for marking in ("authored", "missing", "stale", "unrelated"):
            def revise(name, before, after):
                if name != "boundary_cases":
                    return
                identity = fixtures.CASES[name]["identity"]
                revised = with_revised_question(before, identity)
                old, current = blocks(before, revised, identity)
                self.assertEqual("multiple_choice", old["type"])
                self.assertEqual(old["explanation"], current["explanation"])
                self.assertEqual(dict(old, question=current["question"]), current)
                fields = revised["textSemantics"]["fields"]
                if marking == "missing":
                    fields[:] = [field for field in fields
                                 if (field["block"], field["path"]) != (identity, "/question")]
                elif marking == "stale":
                    marker = next(field for field in fields
                                  if (field["block"], field["path"]) == (identity, "/question"))
                    marker["sourceSha256"] = "0" * 64
                elif marking == "unrelated":
                    original = next((field for field in fields
                                     if (field["block"], field["path"]) ==
                                     (identity, "/options/0")), None)
                    spans = [] if original and original["spans"] else [
                        layout.text_semantics.select(
                            current["options"][0], current["options"][0],
                            "prose", reading="natural")]
                    marker = layout.text_semantics.make_field(
                        revised, identity, "/options/0", spans)
                    self.assertNotEqual(original, marker)
                    if original is None:
                        fields.append(marker)
                    else:
                        fields[fields.index(original)] = marker
                after.clear()
                after.update(revised)
            with self.subTest(marking=marking):
                code, output = self.run_main(revise)
                self.assertEqual(0 if marking == "authored" else 1, code, output)
                if marking in ("missing", "unrelated"):
                    self.assertIn("campos previos modificados sin autorización: question", output)
                elif marking == "stale":
                    self.assertIn("Text semantic source fingerprint is stale", output)

    def test_main_rejects_extra_changes_even_with_updated_editorial_hashes(self):
        for target, field in (("empty_vector", "code"), ("range_copy", "answers"),
                              ("boundary_cases", "correctIndex"),
                              ("case_sensitive_label", "reviewFeedback"), ("empty_vector", "id")):
            def inject(name, before, after):
                if name != target:
                    return
                identity = fixtures.CASES[name]["identity"]
                block = layout._blocks(after)[identity]
                if field == "code":
                    block[field] += " Cambio no autorizado."
                elif field == "answers": block[field] = ["incorrecto"]
                elif field == "correctIndex": block[field] = 0
                elif field == "reviewFeedback":
                    block[field][0] += " Guía no autorizada."
                    refresh_field(after, identity, "/reviewFeedback/0")
                else: block[field] = "otro_propietario"
                repoint_last_edge(after, identity)
            with self.subTest(target=target, field=field):
                code, output = self.run_main(inject)
                self.assertEqual(1, code, output)


if __name__ == "__main__":
    unittest.main()

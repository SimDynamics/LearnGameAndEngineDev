"""Authenticated co-edition of exercise wording and existing response guidance."""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


IDENTITY = "block_0000_0000"
PATH = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "qa/lesson_guidance_edition.json")


def published(kind):
    attrs = {
        "multiple_choice": {"options": ["Primera", "Segunda", "Tercera"],
                            "correctIndex": 1,
                            "reviewFeedback": ["Revisa A.", "Revisa B.", "Revisa C."]},
        "fill_code": {"language": "cpp", "code": "int n = [];", "answers": ["3"],
                      "reviewFeedback": "Revisa el tipo."},
        "output_prediction": {"language": "cpp", "code": "std::cout << 3;",
                              "answers": ["3"],
                              "reviewFeedback": "Revisa la expresión."},
    }
    source = {
        "id": "cpp_fundamentals__qa__guidance_edition",
        "title": "QA only, not a published lesson",
        "pages": [{"blocks": [{
            "type": kind, "question": "Pregunta inicial.",
            "explanation": "Explicación original.", **attrs[kind]}]}],
    }
    return layout.prepare_v2(source, "es", {"es": source})


def check_provenance(test, before, after, changed_paths):
    initial, revised = layout._blocks(before)[IDENTITY], layout._blocks(after)[IDENTITY]
    layout.validate_transition(before, after)
    test.assertEqual(layout.state_fingerprint(initial), layout.state_fingerprint(revised))
    test.assertTrue(contract.preserves_ordinary_explanation_revision(
        PATH, 0, 0, initial, revised,
        previous_document=before, current_document=after))
    test.assertEqual(changed_paths, after["stateLayout"]["editorial"][-1]["reviewPaths"])
    markers = {(item["block"], item["path"]): item for item in
               after["textSemantics"]["fields"]}
    for field in changed_paths:
        test.assertIn((IDENTITY, field), markers)
    return initial, revised


class ReviewFeedbackProseRevisionTest(unittest.TestCase):
    def test_string_guidance_with_question_or_explanation(self):
        for kind in ("fill_code", "output_prediction"):
            for prose in ("question", "explanation"):
                with self.subTest(kind=kind, prose=prose):
                    before = published(kind)
                    after = layout.revise_ordinary_editorial(before, IDENTITY, {
                        prose: {"text": "Texto reformulado.", "spans": []},
                        "reviewFeedback": {"text": "Orientación revisada.", "spans": []},
                    })
                    original, current = check_provenance(
                        self, before, after, ["/reviewFeedback"])
                    self.assertEqual(original["answers"], current["answers"])
                    self.assertEqual(original["code"], current["code"])
                    self.assertEqual((prose, "reviewFeedback"),
                                     layout.ordinary_editorial_fields(original, current))
                    self.assertEqual(("/" + prose, "/reviewFeedback"),
                                     layout.ordinary_editorial_paths(original, current))
                    # The new guide has no relationship to the student's saved result.
                    self.assertEqual(before["stateLayout"]["origin"],
                                     after["stateLayout"]["origin"])

    def test_indexed_guidance_preserves_choice_and_each_hint(self):
        before = published("multiple_choice")
        revised = layout.revise_ordinary_editorial(before, IDENTITY, {
            "question": {"text": "¿Qué opción corresponde al dato?", "spans": []},
            "explanation": {"text": "Explicación más clara.", "spans": []},
            "reviewFeedback": {
                "0": {"text": "Distingue el primer caso.", "spans": []},
                "2": {"text": "Comprueba el tercer caso.", "spans": []},
            },
        })
        old, new = check_provenance(self, before, revised,
                                    ["/reviewFeedback/0", "/reviewFeedback/2"])
        self.assertEqual(old["options"], new["options"])
        self.assertEqual(old["correctIndex"], new["correctIndex"])
        self.assertEqual(old["reviewFeedback"][1], new["reviewFeedback"][1])
        self.assertEqual(( "question", "explanation", "reviewFeedback"),
                         layout.ordinary_editorial_fields(old, new))
        self.assertEqual(("/question", "/explanation", "/reviewFeedback/0",
                          "/reviewFeedback/2"), layout.ordinary_editorial_paths(old, new))
        follow_up = layout.revise_ordinary_editorial(revised, IDENTITY, {
            "explanation": {"text": "Una explicación adicional.", "spans": []},
        })
        layout.validate_transition(revised, follow_up)
        self.assertNotIn("reviewPaths", follow_up["stateLayout"]["editorial"][-1])
        self.assertEqual(revised["stateLayout"]["editorial"],
                         follow_up["stateLayout"]["editorial"][:-1])

    def test_published_u2l6_and_u2l7_preserve_real_review_contracts(self):
        # Exercise the actual active Spanish v2 assets that blocked Work's
        # combined wording/hint revision. No curricular file is rewritten.
        prefix = ("app/src/main/assets/content/es/programming/"
                  "cpp_fundamentals/objetos_y_estado/")
        cases = (
            ("lesson_conversiones_y_deduccion.json", "multiple_choice", "question"),
            ("lesson_cadenas_y_texto.json", "fill_code", "explanation"),
        )
        for filename, kind, prose in cases:
            with self.subTest(lesson=filename, kind=kind, prose=prose):
                relative = prefix + filename
                before = json.loads(
                    (Path(__file__).resolve().parents[2] / relative).read_text(
                        encoding="utf-8"))
                positions = layout.validate(before)
                owners = layout._blocks(before)
                candidates = [
                    (page, block, identity)
                    for page, record in enumerate(before["stateLayout"]["current"])
                    for block, identity in enumerate(record["blocks"])
                    if (owners[identity].get("type") == kind
                        and owners[identity].get("question")
                        and owners[identity].get("explanation")
                        and owners[identity].get("reviewFeedback"))
                ]
                self.assertTrue(candidates, "Published U2 review owner is missing")
                page, block, identity = candidates[0]
                previous = owners[identity]

                def authored_spans(path):
                    matching = [
                        field["spans"] for field in
                        before.get("textSemantics", {}).get("fields", [])
                        if field["block"] == identity and field["path"] == path
                    ]
                    self.assertLessEqual(len(matching), 1)
                    return deepcopy(matching[0] if matching else [])

                changes = {
                    prose: {
                        "text": previous[prose] + " Revisión de prosa QA.",
                        "spans": authored_spans("/" + prose),
                    },
                }
                if kind == "multiple_choice":
                    changes["reviewFeedback"] = {
                        "0": {
                            "text": previous["reviewFeedback"][0] +
                                    " Revisión de orientación QA.",
                            "spans": authored_spans("/reviewFeedback/0"),
                        },
                    }
                    hint_paths = ["/reviewFeedback/0"]
                else:
                    changes["reviewFeedback"] = {
                        "text": previous["reviewFeedback"] +
                                " Revisión de orientación QA.",
                        "spans": authored_spans("/reviewFeedback"),
                    }
                    hint_paths = ["/reviewFeedback"]

                after = layout.revise_ordinary_editorial(before, identity, changes)
                current = layout._blocks(after)[identity]
                origin = positions[(page, block)]
                self.assertTrue(contract.preserves_ordinary_explanation_revision(
                    relative, *origin, previous, current,
                    previous_document=before, current_document=after))
                self.assertEqual(layout.state_fingerprint(previous),
                                 layout.state_fingerprint(current))
                self.assertEqual(before["stateLayout"]["origin"],
                                 after["stateLayout"]["origin"])
                self.assertEqual(before["stateLayout"]["current"],
                                 after["stateLayout"]["current"])
                self.assertEqual(before["stateLayout"]["editorial"],
                                 after["stateLayout"]["editorial"][:-1])
                self.assertEqual(
                    ["/" + prose, *hint_paths],
                    list(layout.ordinary_editorial_paths(previous, current)))
                for field in set(previous) - {prose, "reviewFeedback"}:
                    self.assertEqual(previous[field], current[field])

    def test_rejects_guide_only_addition_and_mismatched_choice_positions(self):
        for kind in ("fill_code", "multiple_choice"):
            before = published(kind)
            spec = ({"text": "Solo pista.", "spans": []} if kind == "fill_code"
                    else {"1": {"text": "Solo pista.", "spans": []}})
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                layout.revise_ordinary_editorial(before, IDENTITY, {
                    "reviewFeedback": spec})
        before = published("multiple_choice")
        question = {"question": {"text": "Pregunta nueva.", "spans": []}}
        for malformed in (
            {"3": {"text": "Fuera de rango.", "spans": []}},
            {"01": {"text": "Índice no canónico.", "spans": []}},
            {"1": {"text": "", "spans": []}},
            {"1": {"text": "Sin descripción."}},
        ):
            with self.subTest(guide=malformed), self.assertRaises(ValueError):
                layout.revise_ordinary_editorial(before, IDENTITY, {
                    **question, "reviewFeedback": malformed})

    def test_rejects_forged_proof_missing_semantics_and_answers(self):
        before = published("multiple_choice")
        correct = layout.revise_ordinary_editorial(before, IDENTITY, {
            "question": {"text": "Pregunta revisada.", "spans": []},
            "reviewFeedback": {"1": {"text": "Pista vinculada al inciso dos.", "spans": []}},
        })
        for mode in ("no-provenance", "wrong-index", "missing-marker",
                     "changed-option", "changed-solution", "extra-hint"):
            forged = deepcopy(correct)
            block = layout._blocks(forged)[IDENTITY]
            if mode == "no-provenance":
                del forged["stateLayout"]["editorial"][-1]["reviewPaths"]
            elif mode == "wrong-index":
                forged["stateLayout"]["editorial"][-1]["reviewPaths"] = ["/reviewFeedback/0"]
            elif mode == "missing-marker":
                forged["textSemantics"]["fields"] = [
                    f for f in forged["textSemantics"]["fields"]
                    if (f["block"], f["path"]) != (IDENTITY, "/reviewFeedback/1")]
            elif mode == "changed-option":
                block["options"][0] = "Una respuesta distinta"
            elif mode == "changed-solution":
                block["correctIndex"] = 2
            elif mode == "extra-hint":
                block["reviewFeedback"].append("Índice no publicado.")
            if mode in ("changed-option", "changed-solution", "extra-hint"):
                forged["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(block)
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                layout.validate_transition(before, forged)


if __name__ == "__main__":
    unittest.main()

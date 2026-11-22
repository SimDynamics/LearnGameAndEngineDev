"""Authenticated co-edition of exercise wording and existing response guidance."""
from copy import deepcopy
import hashlib
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
    def test_new_inline_explanation_requires_authenticated_review(self):
        # A formerly absent explanation cannot be silently dropped by the
        # legacy comparison and credited as unchanged educational material.
        source = {
            "id": "cpp_fundamentals__qa__guidance_edition",
            "title": "Fixture without an explanation",
            "pages": [{"blocks": [{
                "type": "inline_fill_code",
                "question": "Completa el código.",
                "language": "cpp", "template": "int [] = 3;",
                "answers": ["cantidad"],
            }]}],
        }
        before = layout.prepare_v2(source, "es", {"es": source})
        original = layout._blocks(before)[IDENTITY]
        self.assertNotIn("explanation", original)
        forged = deepcopy(before)
        unproven = layout._blocks(forged)[IDENTITY]
        unproven["explanation"] = "El identificador guarda un número entero."
        self.assertNotEqual(original, unproven)
        self.assertFalse(contract.preserves_previous_exercise(
            PATH, 0, 0, original, unproven,
            previous_document=before, current_document=forged))
        self.assertFalse(contract.preserves_previous_exercise(
            PATH, 0, 0, original, unproven))
        with self.assertRaises(ValueError):
            layout.validate_transition(before, forged)
        approved = layout.revise_ordinary_editorial(before, IDENTITY, {
            "explanation": {
                "text": unproven["explanation"], "spans": [],
            },
        })
        current = layout._blocks(approved)[IDENTITY]
        self.assertEqual(current["answers"], original["answers"])
        self.assertEqual(current["template"], original["template"])
        self.assertTrue(contract.preserves_previous_exercise(
            PATH, 0, 0, original, current,
            previous_document=before, current_document=approved))
        self.assertEqual(len(approved["stateLayout"]["editorial"]),
                         len(before["stateLayout"]["editorial"]) + 1)
        self.assertIn(
            (IDENTITY, "/explanation"),
            {(field["block"], field["path"])
             for field in approved["textSemantics"]["fields"]})
        tampered = deepcopy(approved)
        tampered["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
        self.assertFalse(contract.preserves_previous_exercise(
            PATH, 0, 0, original, layout._blocks(tampered)[IDENTITY],
            previous_document=before, current_document=tampered))

    def test_published_u1l4_drag_fill_new_explanation_is_authenticated(self):
        # Real U1L4: Work must be able to explain a previously unannotated
        # drag-fill activity without changing its choices, order or storage.
        # This is a QA-only proposal; the published curriculum is untouched.
        relative = ("app/src/main/assets/content/es/programming/"
                    "cpp_fundamentals/programas_y_ejecucion/"
                    "lesson_fuente_compilacion_y_diagnostico.json")
        asset = Path(__file__).resolve().parents[2] / relative
        original_bytes = asset.read_bytes()
        before = json.loads(original_bytes)
        origins = layout.validate(before)
        identity = "block_0004_0001"
        live = [(page, block)
                for page, record in enumerate(before["stateLayout"]["current"])
                for block, owner in enumerate(record["blocks"])
                if owner == identity]
        self.assertEqual(len(live), 1)
        position = live[0]
        old = layout._blocks(before)[identity]
        self.assertEqual(old["type"], "drag_fill_code")
        self.assertEqual(old["id"], "piezas_estructura")
        if "explanation" in old:
            # After Work publishes this feedback, test its active authored
            # text and provenance rather than proposing it a second time.
            published_text = old["explanation"]
            self.assertTrue(isinstance(published_text, str) and published_text.strip())
            marks = [field for field in before.get("textSemantics", {}).get("fields", [])
                     if (field["block"], field["path"]) == (identity, "/explanation")]
            self.assertEqual(len(marks), 1)
            self.assertEqual(marks[0]["sourceSha256"],
                             hashlib.sha256(published_text.encode("utf-8")).hexdigest())
            self.assertTrue(any(proof["id"] == identity
                                for proof in before["stateLayout"]["editorial"]))
            tampered = deepcopy(before)
            layout._blocks(tampered)[identity]["explanation"] += " Texto sin procedencia."
            with self.assertRaises(ValueError):
                layout.validate_transition(before, tampered)
            self.assertEqual(asset.read_bytes(), original_bytes)
            return
        self.assertNotIn("explanation", old)
        old_semantics = deepcopy(before["textSemantics"]["fields"])
        explanation = ("La ficha main forma la función; los operadores "
                       "completan la instrucción y su punto y coma.")
        unsigned = deepcopy(before)
        forged_block = layout._blocks(unsigned)[identity]
        forged_block["explanation"] = explanation
        self.assertFalse(contract.preserves_previous_exercise(
            relative, *origins[position], old, forged_block,
            previous_document=before, current_document=unsigned))
        with self.assertRaises(ValueError):
            layout.validate_transition(before, unsigned)

        reviewed = layout.revise_ordinary_editorial(before, identity, {
            "explanation": {"text": explanation, "spans": []},
        })
        now = layout._blocks(reviewed)[identity]
        layout.validate_transition(before, reviewed)
        self.assertTrue(contract.preserves_previous_exercise(
            relative, *origins[position], old, now,
            previous_document=before, current_document=reviewed))
        self.assertEqual(layout.state_fingerprint(old),
                         layout.state_fingerprint(now))
        self.assertEqual(reviewed["stateLayout"]["origin"],
                         before["stateLayout"]["origin"])
        self.assertEqual(reviewed["stateLayout"]["current"],
                         before["stateLayout"]["current"])
        self.assertEqual(reviewed["stateLayout"]["editorial"][:-1],
                         before["stateLayout"]["editorial"])
        self.assertEqual(reviewed["stateLayout"]["editorial"][-1]["id"],
                         identity)
        self.assertEqual(layout.ordinary_editorial_paths(old, now),
                         ("/explanation",))
        self.assertEqual(reviewed["textSemantics"]["fields"][:-1],
                         old_semantics)
        self.assertEqual(reviewed["textSemantics"]["fields"][-1]["block"],
                         identity)
        self.assertEqual(reviewed["textSemantics"]["fields"][-1]["path"],
                         "/explanation")
        for protected in set(old):
            self.assertEqual(now[protected], old[protected])

        for field, replacement in (
            ("correctOrder", [2, 1, 0]),
            ("question", "Otra pregunta que cambiaría el objetivo."),
        ):
            with self.subTest(tampered=field):
                altered = deepcopy(reviewed)
                layout._blocks(altered)[identity][field] = replacement
                self.assertFalse(contract.preserves_previous_exercise(
                    relative, *origins[position], old,
                    layout._blocks(altered)[identity],
                    previous_document=before, current_document=altered))
                with self.assertRaises(ValueError):
                    layout.validate_transition(before, altered)

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

    def test_real_u2_question_explanation_and_hints_change_together(self):
        """The two published U2 lessons can revise all their feedback at once.

        Only isolated in-memory proposals are used. Historical responses,
        ownership and the other authored semantic markers remain unchanged.
        """
        prefix = ("app/src/main/assets/content/es/programming/"
                  "cpp_fundamentals/objetos_y_estado/")
        for filename, kind in (
                ("lesson_conversiones_y_deduccion.json", "multiple_choice"),
                ("lesson_cadenas_y_texto.json", "fill_code")):
            with self.subTest(lesson=filename):
                relative = prefix + filename
                asset = Path(__file__).resolve().parents[2] / relative
                original_bytes = asset.read_bytes()
                previous_doc = json.loads(original_bytes)
                origins = layout.validate(previous_doc)
                owners = layout._blocks(previous_doc)
                candidates = [
                    (p, b, identity)
                    for p, page in enumerate(previous_doc["stateLayout"]["current"])
                    for b, identity in enumerate(page["blocks"])
                    if owners[identity].get("type") == kind
                    and all(owners[identity].get(field)
                            for field in ("question", "explanation", "reviewFeedback"))]
                self.assertTrue(candidates)
                page, index, identity = candidates[0]
                old = owners[identity]

                def marker_spans(pointer):
                    found = [item["spans"] for item in
                             previous_doc.get("textSemantics", {}).get("fields", [])
                             if item["block"] == identity and item["path"] == pointer]
                    self.assertLessEqual(len(found), 1)
                    return deepcopy(found[0] if found else [])

                changes = {
                    field: {"text": old[field] + " Comprueba cada paso del ejemplo.",
                            "spans": marker_spans("/" + field)}
                    for field in ("question", "explanation")}
                if kind == "multiple_choice":
                    self.assertGreaterEqual(len(old["reviewFeedback"]), 3)
                    guide_paths = ["/reviewFeedback/0", "/reviewFeedback/2"]
                    changes["reviewFeedback"] = {
                        str(i): {"text": old["reviewFeedback"][i] +
                                " Revisa este caso en particular.",
                                "spans": marker_spans("/reviewFeedback/" + str(i))}
                        for i in (0, 2)}
                else:
                    guide_paths = ["/reviewFeedback"]
                    changes["reviewFeedback"] = {
                        "text": old["reviewFeedback"] + " Revisa el valor de origen.",
                        "spans": marker_spans("/reviewFeedback")}

                revised = layout.revise_ordinary_editorial(previous_doc, identity, changes)
                current = layout._blocks(revised)[identity]
                layout.validate_transition(previous_doc, revised)
                self.assertEqual(
                    layout.ordinary_editorial_paths(old, current),
                    tuple(["/question", "/explanation", *guide_paths]))
                self.assertEqual(
                    revised["stateLayout"]["editorial"][-1]["reviewPaths"], guide_paths)
                self.assertTrue(contract.preserves_previous_exercise(
                    relative, *origins[(page, index)], old, current,
                    previous_document=previous_doc, current_document=revised))
                self.assertEqual(layout.state_fingerprint(old),
                                 layout.state_fingerprint(current))
                for field in set(old) - {"question", "explanation", "reviewFeedback"}:
                    self.assertEqual(old[field], current[field])
                for field in ("origin", "current", "family", "retired",
                              "retirementHistory", "additions"):
                    self.assertEqual(previous_doc["stateLayout"].get(field),
                                     revised["stateLayout"].get(field))
                self.assertEqual(previous_doc["stateLayout"]["editorial"],
                                 revised["stateLayout"]["editorial"][:-1])

                for mutation in ("answer", "stale-semantic", "missing-proof",
                                 "unreviewed-hint"):
                    if mutation == "unreviewed-hint" and kind != "multiple_choice":
                        continue
                    forged = deepcopy(revised)
                    altered = layout._blocks(forged)[identity]
                    if mutation == "answer":
                        if kind == "multiple_choice":
                            altered["correctIndex"] = (
                                old["correctIndex"] + 1) % len(old["options"])
                        else:
                            altered["answers"][0] += "_sin_autorizacion"
                    elif mutation == "stale-semantic":
                        field = next(item for item in forged["textSemantics"]["fields"]
                                     if item["block"] == identity
                                     and item["path"] == "/explanation")
                        field["sourceSha256"] = "0" * 64
                    elif mutation == "missing-proof":
                        forged["stateLayout"]["editorial"].pop()
                    else:
                        altered["reviewFeedback"][1] += " Pista sin revisión."
                    with self.subTest(lesson=filename, mutation=mutation):
                        with self.assertRaises(ValueError):
                            layout.validate_transition(previous_doc, forged)
                        self.assertFalse(contract.preserves_previous_exercise(
                            relative, *origins[(page, index)], old, altered,
                            previous_document=previous_doc, current_document=forged))
                self.assertEqual(asset.read_bytes(), original_bytes)

    def test_published_u12l22_question_precision_preserves_prediction(self):
        """A changed prediction question is authored prose, not a new answer.

        Work owns the actual lesson update. This only verifies its exact
        published source and a temporary, independently authenticated revision.
        """
        relative = (
            "app/src/main/assets/content/es/programming/cpp_fundamentals/"
            "archivos_y_programa_completo/lesson_leer_un_grupo_con_cierre_propio.json")
        asset = Path(__file__).resolve().parents[2] / relative
        original_bytes = asset.read_bytes()
        published = json.loads(original_bytes)
        origins = layout.validate(published)
        identity = "block_0001_0002"
        location = next(
            (p, b) for p, row in enumerate(published["stateLayout"]["current"])
            for b, owner in enumerate(row["blocks"]) if owner == identity)
        old = layout._blocks(published)[identity]
        self.assertEqual(old["type"], "output_prediction")
        self.assertEqual(old["id"], "mirar_no_consume")
        self.assertEqual(old["answers"], ["1:}:fin"])
        previous_question = (
            "Predice la salida. Este ejemplo ya comprobó que hay caracteres disponibles.")
        revised_question = (
            "Predice la salida. La cadena mostrada contiene una llave de cierre "
            "después de dos espacios.")
        if old["question"] == revised_question:
            # Work may have published the authenticated correction already.
            # A regression must verify its live provenance, not require
            # the obsolete wording to remain in the active curriculum.
            self.assertTrue(any(
                edge["id"] == identity and
                edge["afterSha256"] == layout.fingerprint(old)
                for edge in published["stateLayout"]["editorial"]))
            current_marker = next(
                field for field in published["textSemantics"]["fields"]
                if field["block"] == identity and field["path"] == "/question")
            # Work can author nonempty marks for the revised question.
            # The source digest and the shared semantic validator, not a
            # forced empty span list, authenticate their boundaries.
            self.assertIsInstance(current_marker["spans"], list)
            self.assertEqual(
                current_marker["sourceSha256"],
                hashlib.sha256(revised_question.encode("utf-8")).hexdigest())
            unsigned = deepcopy(published)
            layout._blocks(unsigned)[identity]["question"] = previous_question
            with self.assertRaises(ValueError):
                layout.validate_transition(published, unsigned)
            self.assertEqual(asset.read_bytes(), original_bytes)
            return
        self.assertEqual(old["question"], previous_question)

        unsigned = deepcopy(published)
        layout._blocks(unsigned)[identity]["question"] = revised_question
        with self.assertRaises(ValueError):
            layout.validate_transition(published, unsigned)

        revised = layout.revise_ordinary_editorial(published, identity, {
            "question": {"text": revised_question, "spans": []}})
        layout.validate_transition(published, revised)
        actual = layout._blocks(revised)[identity]
        self.assertEqual(actual["question"], revised_question)
        self.assertEqual(actual["code"], old["code"])
        self.assertEqual(actual["answers"], old["answers"])
        self.assertEqual(actual["reviewFeedback"], old["reviewFeedback"])
        self.assertEqual(actual["explanation"], old["explanation"])
        self.assertEqual(layout.state_fingerprint(old),
                         layout.state_fingerprint(actual))
        self.assertEqual(layout.ordinary_editorial_paths(old, actual),
                         ("/question",))
        self.assertEqual(published["stateLayout"]["origin"],
                         revised["stateLayout"]["origin"])
        self.assertEqual(published["stateLayout"]["current"],
                         revised["stateLayout"]["current"])
        self.assertEqual(published["stateLayout"]["editorial"],
                         revised["stateLayout"]["editorial"][:-1])
        self.assertTrue(contract.preserves_previous_exercise(
            relative, *origins[location], old, actual,
            previous_document=published, current_document=revised))
        # Ordinary editorial prose remains in the current block when aligning
        # historical grading. Only the evaluated response contract rewinds.
        aligned = layout._blocks(layout.align_for_previous_validation(
            published, revised))[identity]
        self.assertEqual(aligned["question"], revised_question)
        self.assertEqual(layout.state_fingerprint(aligned),
                         layout.state_fingerprint(old))
        self.assertEqual(aligned["answers"], old["answers"])
        previous_marks = {
            entry["path"]: entry for entry in published["textSemantics"]["fields"]
            if entry["block"] == identity}
        latest_marks = {
            entry["path"]: entry for entry in revised["textSemantics"]["fields"]
            if entry["block"] == identity}
        self.assertEqual(set(latest_marks), set(previous_marks))
        self.assertEqual(latest_marks["/question"]["spans"], [])
        self.assertEqual(latest_marks["/question"]["sourceSha256"],
                         hashlib.sha256(revised_question.encode("utf-8")).hexdigest())
        for path in set(previous_marks) - {"/question"}:
            self.assertEqual(previous_marks[path], latest_marks[path])

        for name in ("new answer", "altered source", "missing review", "wrong mark"):
            forged = deepcopy(revised)
            owner = layout._blocks(forged)[identity]
            if name == "new answer":
                owner["answers"] = ["0:}:fin"]
            elif name == "altered source":
                owner["code"] += "\n// Alteración no autorizada"
            elif name == "missing review":
                forged["stateLayout"]["editorial"].pop()
            else:
                next(item for item in forged["textSemantics"]["fields"]
                     if item["block"] == identity and item["path"] == "/question")[
                         "sourceSha256"] = "0" * 64
            with self.subTest(forgery=name), self.assertRaises(ValueError):
                layout.validate_transition(published, forged)
        self.assertEqual(asset.read_bytes(), original_bytes)

    def test_published_u12_format_explanation_and_question_return_history(self):
        """Real U12 format exercise reuses its signed prior std::hex edition.

        The format question/explanation were published with explicit std::
        names and later returned to shortened names. Restoring an earlier
        complete authored block is a deliberate editorial return, never an
        anonymous text edit or a new graded question.
        """
        relative = (
            "app/src/main/assets/content/es/programming/cpp_fundamentals/"
            "archivos_y_programa_completo/"
            "lesson_estado_del_formato_y_flujos_independientes.json")
        asset = Path(__file__).resolve().parents[2] / relative
        original_bytes = asset.read_bytes()
        published = json.loads(original_bytes)
        positions = layout.validate(published)
        identity = "block_0002_0002"
        location = next(
            (p, b) for p, row in enumerate(published["stateLayout"]["current"])
            for b, owner in enumerate(row["blocks"]) if owner == identity)
        old = layout._blocks(published)[identity]
        self.assertEqual(old["type"], "multiple_choice")
        self.assertEqual(old["correctIndex"], 2)
        self.assertEqual(len(old["options"]), 3)

        abbreviations = (
            ("texto << hex << 31", "texto << std::hex << 31"),
            ("texto << dec", "texto << std::dec"))
        def complete_names(source):
            result = source
            for short, full in abbreviations:
                result = result.replace(short, full)
            return result

        question = complete_names(old["question"])
        explanation = complete_names(old["explanation"])
        if question == old["question"] and explanation == old["explanation"]:
            # Work already published this correction. Verify its provenance
            # and exact response contract instead of re-editing it.
            self.assertIn("std::hex", question)
            self.assertIn("std::dec", explanation)
            self.assertTrue(any(
                entry["id"] == identity and
                entry["afterSha256"] == layout.fingerprint(old)
                for entry in published["stateLayout"]["editorial"]))
            original_marks = {
                item["path"]: item
                for item in published["textSemantics"]["fields"]
                if item["block"] == identity}
            for path, source in (("/question", question),
                                 ("/explanation", explanation)):
                self.assertEqual(
                    original_marks[path]["sourceSha256"],
                    hashlib.sha256(source.encode("utf-8")).hexdigest())
            self.assertEqual(asset.read_bytes(), original_bytes)
            return

        self.assertNotEqual(question, old["question"])
        self.assertNotEqual(explanation, old["explanation"])
        self.assertIn("texto << std::hex << 31", question)
        self.assertIn("texto << std::dec", explanation)
        revised_block = deepcopy(old)
        revised_block["question"] = question
        revised_block["explanation"] = explanation
        target_sha = layout.fingerprint(revised_block)
        owner_edges = [
            entry for entry in published["stateLayout"]["editorial"]
            if entry["id"] == identity]
        self.assertTrue(owner_edges)
        history = [owner_edges[0]["beforeSha256"]] + [
            entry["afterSha256"] for entry in owner_edges]
        prior_versions = [
            index for index, sha in enumerate(history[:-1])
            if sha == target_sha]
        self.assertTrue(prior_versions,
            "An actual published std::hex edition was not found in the owner history")
        spans = layout.text_semantics.select
        changes = {
            "question": {
                "text": question,
                "spans": [
                    spans(question, "texto << std::hex << 31",
                          "cpp", reading="technical"),
                    spans(question, "texto << std::dec",
                          "cpp", reading="technical")]},
            "explanation": {
                "text": explanation,
                "spans": [
                    spans(explanation, "texto << std::hex << 31",
                          "cpp", reading="technical"),
                    spans(explanation, "1f", "output",
                          occurrence=1, reading="natural"),
                    spans(explanation, "texto << std::dec",
                          "cpp", reading="technical"),
                    spans(explanation, "1f", "output",
                          occurrence=2, reading="natural")]},
        }
        with self.assertRaises(ValueError):
            layout.revise_ordinary_editorial(published, identity, changes)
        updated = layout.revise_ordinary_editorial(
            published, identity, {**changes, "returnTo": prior_versions[-1]})
        layout.validate_transition(published, updated)
        current = layout._blocks(updated)[identity]
        self.assertEqual(current, revised_block)
        self.assertEqual(layout.state_fingerprint(old),
                         layout.state_fingerprint(current))
        self.assertEqual(layout.ordinary_editorial_paths(old, current),
                         ("/question", "/explanation"))
        self.assertEqual(updated["stateLayout"]["origin"],
                         published["stateLayout"]["origin"])
        self.assertEqual(updated["stateLayout"]["current"],
                         published["stateLayout"]["current"])
        self.assertEqual(updated["stateLayout"]["editorial"][:-1],
                         published["stateLayout"]["editorial"])
        self.assertEqual(updated["stateLayout"]["editorial"][-1][
            "returnTo"]["revision"], prior_versions[-1])
        self.assertTrue(contract.preserves_previous_exercise(
            relative, *positions[location], old, current,
            previous_document=published, current_document=updated))
        # An editorial return preserves the new visible words, while the
        # grading identity and historical output requirements remain stable.
        aligned = layout._blocks(layout.align_for_previous_validation(
            published, updated))[identity]
        self.assertEqual(aligned["question"], question)
        self.assertEqual(aligned["explanation"], explanation)
        self.assertEqual(layout.state_fingerprint(aligned),
                         layout.state_fingerprint(old))
        self.assertEqual(aligned["options"], old["options"])
        self.assertEqual(aligned["correctIndex"], old["correctIndex"])
        before_marks = {
            entry["path"]: entry for entry in published["textSemantics"]["fields"]
            if entry["block"] == identity}
        after_marks = {
            entry["path"]: entry for entry in updated["textSemantics"]["fields"]
            if entry["block"] == identity}
        self.assertEqual(before_marks.keys(), after_marks.keys())
        for path, source in (("/question", question),
                             ("/explanation", explanation)):
            self.assertEqual(
                after_marks[path]["sourceSha256"],
                hashlib.sha256(source.encode("utf-8")).hexdigest())
        for path in before_marks.keys() - {"/question", "/explanation"}:
            self.assertEqual(before_marks[path], after_marks[path])

        for label in ("answer-index", "option", "missing-return",
                      "forged-reading", "forged-history"):
            forged = deepcopy(updated)
            block = layout._blocks(forged)[identity]
            if label == "answer-index":
                block["correctIndex"] = 0
            elif label == "option":
                block["options"][0] += " Alteración no autorizada."
            elif label == "missing-return":
                forged["stateLayout"]["editorial"][-1].pop("returnTo")
            elif label == "forged-reading":
                next(field for field in forged["textSemantics"]["fields"]
                     if field["block"] == identity and
                     field["path"] == "/question")["sourceSha256"] = "0" * 64
            else:
                forged["stateLayout"]["editorial"][-1][
                    "returnTo"]["revision"] += 1
            with self.subTest(forgery=label), self.assertRaises(ValueError):
                layout.validate_transition(published, forged)
        self.assertEqual(asset.read_bytes(), original_bytes)

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

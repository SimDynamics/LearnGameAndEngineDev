"""Exact Work revisions preserve their historical activity contracts."""
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


PREFIX = "app/src/main/assets/content/es/programming/cpp_fundamentals/"
CURRENT = "3bd53f89ac33523f4a5505752bc3a51f0c7d6d39"
CASES = (
    ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 3, 2,
     "block_0003_0002", "order_code", "explanation", "armar_programa", "aa230c6",
     "c50562e326eb30529a30379fdc5fc521f83e6361b8d3cb2b2e4fb4bb53425bb5",
     "60d676de2ed953914099a4dc6288b7466e97a3a70bc11bcb6169b2c86f652822"),
    ("texto_interpretacion_y_flujos/lesson_lineas_y_separadores_pendientes.json", 2, 1,
     "block_0002_0001", "output_prediction", "question", None, "aa230c6",
     "731496fab5e064157fea4e0108c6990c18392e403b39628d47f556d721cb3e35",
     "6e0a38ef37e7ad479787f5e501679c00568c8f5fdfd91c6b8ecae12851fd9f5a"),
    ("texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json", 2, 2,
     "block_0002_0002", "multiple_choice", "explanation", None, "1fe0975",
     "54a1c2f05e6cf4af61cb561eb01b9938ed20931e6321cfa783538a9c640635e2",
     "c85463058609727aaf54b4ea6721db53136af0bf37068c86bcc0b8f7b198a2e2"),
    ("texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json", 6, 2,
     "block_0006_0002", "output_prediction", "explanation", None, "1fe0975",
     "5fcc83da4e6f4dcd16510343f01d4633b95ad901841af88f68b12a9c91554043",
     "eb3bd0aea79408b2146891b3116c41678e8cf098bbd56d3328dba1ba936ebadd"),
)

U11_PROPOSAL_BASE = "ab13ae4a258a68487ba4b91519baed1736252cc9"
U11_PROPOSALS = (
    ("texto_interpretacion_y_flujos/lesson_terminos_prioridad_y_division_segura.json",
     3, 3, "block_0003_0003", "output_prediction", "explanation",
     "El término obtiene el divisor 0 y lanza el diagnóstico antes de evaluar izquierda / derecha. La expresión no llega a sumar 1.",
     "El término obtiene el divisor 0 y lanza una excepción antes de evaluar `izquierda / derecha`. El bloque `catch` presenta `Error: Divisor cero`; la expresión no llega a sumar 1.",
     "5e4956eac79a3c4c02bf33e877a0e643c502c6b3e7f90327142938ceaedfc247",
     "5d3f39719517e667b19f82ae8f4ed1565e78336ab7a321051f4486f0f11dc141"),
    ("texto_interpretacion_y_flujos/lesson_signos_unarios_parentesis_y_progreso_recursivo.json",
     7, 1, "block_0007_0001", "multiple_choice", "question",
     "Una versión devuelve el mismo ( que acaba de obtener y vuelve a pedir la expresión sin consumirlo ni aumentar un límite. ¿Qué defecto tiene?",
     "Una versión devuelve el mismo ( que acaba de obtener y vuelve a pedir la expresión sin consumirlo ni incrementar la profundidad. ¿Qué defecto tiene?",
     "7f18d783ddecec3d24ee6484f02e59633c749e0a62423a19d3a78833ed090064",
     "77027e5a050155971e7d4df8a9c71e48bf6d6c8203d35d48a95cf7ecedd7126b"),
)


@lru_cache(maxsize=None)
def published_bytes(revision, lesson, locale="es"):
    relative = PREFIX.replace("/es/", "/" + locale + "/") + lesson
    return subprocess.check_output(["git", "show", revision + ":" + relative], cwd=contract.ROOT)


def published(revision, lesson, locale="es"):
    return json.loads(published_bytes(revision, lesson, locale))


def text_hash(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def blocks(case):
    lesson, pn, bn, *_ = case
    before = published(case[7], lesson)
    after = published(CURRENT, lesson)
    # Only the order representation is projected back to its original groups.
    # The current reviewed wording remains visible to the preservation guard.
    projected = layout.original_projection(after)
    return before["pages"][pn]["blocks"][bn], projected["pages"][pn]["blocks"][bn]


def preserved(case, old, new, lesson=None, page=None, block=None):
    relative = PREFIX + (case[0] if lesson is None else lesson)
    return contract.preserves_previous_exercise(
        relative, case[1] if page is None else page, case[2] if block is None else block, old, new)


class WorkExactEditorialTest(unittest.TestCase):
    def test_all_four_exact_published_revisions_are_accepted(self):
        for case in CASES:
            with self.subTest(lesson=case[0], identity=case[3]):
                old, new = blocks(case)
                self.assertEqual(case[4], old["type"])
                self.assertEqual(case[4], new["type"])
                self.assertEqual(case[6], old.get("id"))
                self.assertEqual(case[6], new.get("id"))
                self.assertEqual(case[8:], (text_hash(old[case[5]]), text_hash(new[case[5]])))
                self.assertEqual({case[5], "reviewFeedback"}, {
                    key for key in old.keys() | new.keys() if old.get(key) != new.get(key)})
                self.assertTrue(preserved(case, old, new))
                self.assertEqual(contract.without_review_feedback(old),
                                 contract.restore_approved_editorial_fields(
                                     (case[0], case[1] + 1, case[2] + 1),
                                     contract.without_review_feedback(old),
                                     contract.without_review_feedback(new)))

    def test_approval_requires_both_exact_texts_and_forward_direction(self):
        for case in CASES:
            old, new = blocks(case)
            for side in ("old", "new"):
                changed_old, changed_new = deepcopy(old), deepcopy(new)
                (changed_old if side == "old" else changed_new)[case[5]] += " Revisión distinta."
                with self.subTest(identity=case[3], side=side):
                    self.assertFalse(preserved(case, changed_old, changed_new))
            with self.subTest(identity=case[3], direction="reverse"):
                self.assertFalse(preserved(case, new, old))
            self.assertTrue(preserved(case, new, new))

    def test_approval_is_confined_to_original_slot_lesson_and_spanish_locale(self):
        for case in CASES:
            old, new = blocks(case)
            for page, block in ((case[1] + 1, case[2]), (case[1], case[2] + 1)):
                with self.subTest(identity=case[3], page=page, block=block):
                    self.assertFalse(preserved(case, old, new, page=page, block=block))
            self.assertFalse(preserved(case, old, new, lesson=case[0].replace("lesson_", "lesson_otra_")))
            for locale in ("en", "fr"):
                with self.subTest(identity=case[3], locale=locale), self.assertRaises(ValueError):
                    contract.preserves_previous_exercise(
                        (PREFIX + case[0]).replace("/es/", "/" + locale + "/"),
                        case[1], case[2], old, new)
            for kind in ("fill_code", "inline_fill_code", "drag_fill_code"):
                self.assertFalse(preserved(case, dict(old, type=kind), dict(new, type=kind)))

    def test_original_identity_or_its_absence_cannot_be_replaced(self):
        for case in CASES:
            old, new = blocks(case)
            for identity in (None, 17, "otra_actividad"):
                with self.subTest(identity=case[3], replacement=identity):
                    self.assertFalse(preserved(case, old, dict(new, id=identity)))
                    self.assertFalse(preserved(case, dict(old, id=identity), dict(new, id=identity)))
            if case[6] is not None:
                without_id = lambda value: {key: item for key, item in value.items() if key != "id"}
                self.assertFalse(preserved(case, without_id(old), without_id(new)))

    def test_code_answers_options_orders_and_every_other_field_remain_protected(self):
        for case in CASES:
            old, new = blocks(case)
            for key in new.keys() - {case[5], "reviewFeedback"}:
                changed = deepcopy(new)
                value = changed[key]
                changed[key] = value + " NO AUTORIZADO" if isinstance(value, str) else {"unreviewed": value}
                with self.subTest(identity=case[3], field=key):
                    self.assertFalse(preserved(case, old, changed))
            self.assertFalse(preserved(case, old, dict(new, campo_nuevo="NO AUTORIZADO")))
            other_text = "explanation" if case[5] == "question" else "question"
            self.assertFalse(preserved(case, old, dict(new, **{other_text: new[other_text] + " NO AUTORIZADO"})))

    def test_current_proofs_keep_original_addresses_and_response_fingerprints(self):
        for case in CASES:
            old_lesson, new_lesson = published(case[7], case[0]), published(CURRENT, case[0])
            old, new = blocks(case)
            with self.subTest(identity=case[3]):
                self.assertEqual(layout.lineage(old_lesson), layout.lineage(new_lesson))
                self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
                self.assertNotEqual(layout.fingerprint(old), layout.fingerprint(new))
                proofs = [proof for proof in new_lesson["stateLayout"]["editorial"] if proof["id"] == case[3]]
                self.assertTrue(proofs)
                self.assertEqual(layout.validate(old_lesson)[case[1], case[2]],
                                 layout.validate(new_lesson)[case[1], case[2]])
                for locale in ("en", "fr"):
                    self.assertEqual(published_bytes(case[7], case[0], locale),
                                     published_bytes(CURRENT, case[0], locale))


class U11LaterExactEditorialTest(unittest.TestCase):
    def candidate(self, case):
        previous = published(U11_PROPOSAL_BASE, case[0])
        family = {locale: published(U11_PROPOSAL_BASE, case[0], locale) for locale in ("es", "en", "fr")}
        source = deepcopy(previous)
        layout._blocks(source)[case[3]][case[5]] = case[7]
        prepared = layout.prepare_v2(source, "es", family, previous)
        return previous, prepared, family

    def preserved(self, case, old, new, **location):
        return contract.preserves_previous_exercise(PREFIX + location.get("lesson", case[0]),
            location.get("page", case[1]), location.get("block", case[2]), old, new)

    def test_complete_u11_candidates_preserve_every_current_block_and_historical_guard(self):
        for case in U11_PROPOSALS:
            with self.subTest(identity=case[3]):
                previous, prepared, family = self.candidate(case)
                old, new = (layout._blocks(doc)[case[3]] for doc in (previous, prepared))
                self.assertEqual(case[6], old[case[5]])
                self.assertEqual(case[8:], (text_hash(old[case[5]]), text_hash(new[case[5]])))
                self.assertEqual(case[4], old["type"])
                self.assertNotIn("id", old); self.assertNotIn("id", new)
                self.assertEqual({case[5]}, {key for key in old.keys() | new.keys() if old.get(key) != new.get(key)})
                self.assertEqual(old.get("reviewFeedback"), new.get("reviewFeedback"))
                self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
                self.assertTrue(self.preserved(case, old, new))
                layout.validate_transition(previous, prepared)
                layout.check_localized_family([prepared, family["en"], family["fr"]], ["es", "en", "fr"])
                for field in previous["stateLayout"]:
                    if field != "editorial": self.assertEqual(previous["stateLayout"][field], prepared["stateLayout"][field])
                self.assertEqual(previous["stateLayout"]["editorial"], prepared["stateLayout"]["editorial"][:-1])
                self.assertEqual(case[3], prepared["stateLayout"]["editorial"][-1]["id"])
                for identity, block in layout._blocks(previous).items():
                    if identity != case[3]: self.assertEqual(block, layout._blocks(prepared)[identity])
                # This is the actual whole-lesson preservation loop, including
                # mapped U11L12 p7 -> immutable historical p4 coordinates.
                aligned = layout.align_for_previous_validation(previous, prepared)
                positions = layout.validate(previous)
                for pn, page in enumerate(previous["pages"]):
                    for bn, block in enumerate(page["blocks"]):
                        current = aligned["pages"][pn]["blocks"][bn]
                        origin_p, origin_b = positions[pn, bn]
                        if block["type"] == "cpp_compiler":
                            self.assertTrue(contract.preserves_previous_compiler(PREFIX + case[0], origin_p, origin_b,
                                block, current, layout.original_projection(previous)))
                        elif block["type"] in contract.EXERCISES | {"code"}:
                            self.assertTrue(contract.preserves_previous_exercise(PREFIX + case[0], origin_p, origin_b,
                                block, current), f"{case[0]} current={pn+1},{bn+1} original={origin_p+1},{origin_b+1}")

    def test_both_exact_hashes_and_forward_direction_are_required(self):
        for case in U11_PROPOSALS:
            previous, prepared, _ = self.candidate(case)
            old, new = (layout._blocks(doc)[case[3]] for doc in (previous, prepared))
            for side in ("old", "new"):
                before, after = deepcopy(old), deepcopy(new)
                (before if side == "old" else after)[case[5]] += " Otra redacción."
                with self.subTest(identity=case[3], side=side): self.assertFalse(self.preserved(case, before, after))
            self.assertFalse(self.preserved(case, new, old))
            self.assertTrue(self.preserved(case, new, new))

    def test_type_identity_locale_lesson_and_original_coordinates_are_closed(self):
        for case in U11_PROPOSALS:
            previous, prepared, _ = self.candidate(case)
            old, new = (layout._blocks(doc)[case[3]] for doc in (previous, prepared))
            for kind in ("output_prediction", "multiple_choice", "inline_fill_code", "fill_code"):
                if kind != case[4]: self.assertFalse(self.preserved(case, dict(old, type=kind), dict(new, type=kind)))
            for identity in (None, 17, case[3], "otra_actividad"):
                self.assertFalse(self.preserved(case, old, dict(new, id=identity)))
                self.assertFalse(self.preserved(case, dict(old, id=identity), dict(new, id=identity)))
            self.assertFalse(self.preserved(case, old, new, lesson=case[0].replace("lesson_", "lesson_otra_")))
            for page, block in ((case[1]+1, case[2]), (case[1], case[2]+1)):
                self.assertFalse(self.preserved(case, old, new, page=page, block=block))
            for locale in ("en", "fr"):
                with self.assertRaises(ValueError):
                    contract.preserves_previous_exercise((PREFIX + case[0]).replace("/es/", f"/{locale}/"),
                        case[1], case[2], old, new)

    def test_other_protected_fields_cannot_borrow_the_exact_text_exception(self):
        for case in U11_PROPOSALS:
            previous, prepared, _ = self.candidate(case)
            old, new = (layout._blocks(doc)[case[3]] for doc in (previous, prepared))
            for field in new.keys() - {case[5], "reviewFeedback"}:
                changed = deepcopy(new); value = changed[field]
                changed[field] = value + " NO AUTORIZADO" if isinstance(value, str) else {"unreviewed": value}
                with self.subTest(identity=case[3], field=field): self.assertFalse(self.preserved(case, old, changed))
            self.assertFalse(self.preserved(case, old, dict(new, campo_nuevo="NO AUTORIZADO")))

    def test_state_proofs_and_owners_are_still_required_for_the_whole_candidate(self):
        for case in U11_PROPOSALS:
            previous, prepared, _ = self.candidate(case)
            for mutation in ("remove", "false_before", "false_after", "wrong_owner", "wrong_origin"):
                bad = deepcopy(prepared)
                if mutation == "remove": bad["stateLayout"]["editorial"].pop()
                elif mutation == "false_before": bad["stateLayout"]["editorial"][-1]["beforeSha256"] = "0" * 64
                elif mutation == "false_after": bad["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
                elif mutation == "wrong_owner": bad["stateLayout"]["editorial"][-1]["id"] = "block_0000_0000"
                else: bad["stateLayout"]["origin"][0]["id"] = "otra_pagina"
                with self.subTest(identity=case[3], mutation=mutation), self.assertRaises(layout.LayoutError):
                    layout.validate_transition(previous, bad)

    def test_preparation_never_writes_live_assets_or_translations(self):
        paths = [contract.ROOT / (PREFIX + case[0]).replace("/es/", f"/{locale}/")
                 for case in U11_PROPOSALS for locale in ("es", "en", "fr")]
        original = {path: path.read_bytes() for path in paths}
        for case in U11_PROPOSALS: self.candidate(case)
        self.assertEqual(original, {path: path.read_bytes() for path in paths})


if __name__ == "__main__":
    unittest.main()

"""Exact editorial approvals and shared puzzle schemas keep technical guards."""

import copy
import hashlib
import json
import subprocess
import unittest
from functools import lru_cache

import validar_json_programacion as contract
import stable_state_layout as state_layout


@lru_cache(maxsize=None)
def historical_lesson(revision, lesson):
    relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
    return json.loads(subprocess.check_output(
        ["git", "show", revision + ":" + relative], cwd=contract.ROOT))


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


@lru_cache(maxsize=1)
def original_shared_math():
    """Freeze the seven original compositions at their actual baseline locations."""
    revision = "dd6787d190fcb707a8e935abeadda64584d773d9"
    prefix = contract.CONTENT.relative_to(contract.ROOT).as_posix()
    matches = subprocess.check_output(
        ["git", "grep", "-l", "--fixed-strings", '"math_puzzle"', revision,
         "--", prefix + "/**/lesson_*.json"], cwd=contract.ROOT).decode("utf-8")
    found = []
    for match in matches.splitlines():
        relative = match.split(":", 1)[1]
        lesson = relative[len(prefix) + 1:]
        document = historical_lesson(revision, lesson)
        for pn, page in enumerate(document["pages"]):
            for bn, block in enumerate(page["blocks"]):
                if block["type"] == "math_puzzle":
                    found.append((lesson, pn, bn, block))
    return found


class EditorialAndSharedMathContractTest(unittest.TestCase):
    def setUp(self):
        contract.ERRORS.clear()

    def published_math(self):
        found = []
        originals = original_shared_math()
        self.assertEqual(7, len(originals), "The audited baseline contains exactly seven original compositions.")
        for lesson, pn, bn, previous in originals:
            path = contract.CONTENT / lesson
            document = contract.load(path)
            current = state_layout.original_projection(document)["pages"][pn]["blocks"][bn]
            self.assertEqual(previous, current,
                             f"Original shared activity changed at {lesson}, page {pn + 1}, block {bn + 1}.")
            found.append((path, pn, bn, current))
        return found

    def test_new_shared_puzzles_are_validated_separately_from_the_seven_frozen_originals(self):
        original_locations = {(lesson, pn, bn) for lesson, pn, bn, _ in original_shared_math()}
        new_ids = []
        for path in contract.CONTENT.glob("*/lesson_*.json"):
            lesson = path.relative_to(contract.CONTENT).as_posix()
            document = contract.load(path)
            positions = state_layout.validate(document)
            for pn, page in enumerate(document["pages"]):
                for bn, block in enumerate(page["blocks"]):
                    original_position = positions.get((pn, bn), (pn, bn))
                    if block["type"] != "math_puzzle" or (lesson, *original_position) in original_locations:
                        continue
                    with self.subTest(lesson=lesson, page=pn + 1, block=bn + 1):
                        previous = copy.deepcopy(block)
                        contract.ERRORS.clear()
                        contract.validate_math_puzzle_contract(block, "new shared activity")
                        self.assertEqual([], contract.ERRORS)
                        self.assertEqual(previous, block)
                        self.assertTrue(set(block) <= contract.FIELDS["math_puzzle"])
                        new_ids.append(block["id"])
        self.assertEqual(len(new_ids), len(set(new_ids)))
        self.assertFalse(set(new_ids) & {block["id"] for _, _, _, block in original_shared_math()})
        self.assertIn("cpp_fundamentals__datos_propios_y_estado_valido__valores_por_defecto_con_significado__clasificar_conversiones_implicitas", new_ids)

    def editorial_cases(self, before, after):
        cases = {}
        for key, transition in contract.APPROVED_EDITORIAL_REVISIONS.items():
            lesson, page, block, kind, field = key
            old = historical_lesson(before, lesson)["pages"][page - 1]["blocks"][block - 1]
            new = historical_lesson(after, lesson)["pages"][page - 1]["blocks"][block - 1]
            if (digest(old.get(field, "")), digest(new.get(field, ""))) == transition:
                self.assertEqual(kind, old["type"])
                self.assertEqual(kind, new["type"])
                cases[(lesson, page, block)] = (old, new)
        return cases

    def preserved(self, location, old, new):
        lesson, page, block = location
        relative = (contract.CONTENT / lesson).relative_to(contract.ROOT)
        return contract.preserves_previous_exercise(str(relative), page - 1, block - 1, old, new)

    def test_original_seventeen_apa_changes_pass_against_their_original_baseline(self):
        cases = self.editorial_cases("ad4f362", "879f5ce")
        self.assertEqual(17, len(cases))
        for location, (old, new) in cases.items():
            with self.subTest(location=location):
                self.assertEqual(["explanation"], [key for key in old.keys() | new.keys() if old.get(key) != new.get(key)])
                self.assertTrue(self.preserved(location, old, new))

    def test_current_eight_changes_pass_without_replacing_the_reference_commit(self):
        cases = self.editorial_cases("879f5ce", "14add7ae10ddac9e23d262c69b0fcb77d89272d0")
        self.assertEqual(8, len(cases))
        self.assertEqual(29, len(contract.APPROVED_EDITORIAL_REVISIONS))
        for location, (old, new) in cases.items():
            with self.subTest(location=location):
                differences = {key for key in old.keys() | new.keys() if old.get(key) != new.get(key)}
                self.assertTrue(differences <= {"question", "explanation"})
                self.assertTrue(self.preserved(location, old, new))

    def test_approved_text_never_licenses_different_code_answers_ids_or_locations(self):
        cases = self.editorial_cases("ad4f362", "879f5ce")
        cases.update(self.editorial_cases("879f5ce", "14add7ae10ddac9e23d262c69b0fcb77d89272d0"))
        for location, (old, new) in cases.items():
            for key in new.keys() - {"question", "explanation", "reviewFeedback"}:
                changed = copy.deepcopy(new)
                changed[key] = "UNAPPROVED CHANGE" if not isinstance(new[key], str) else new[key] + " UNAPPROVED"
                with self.subTest(location=location, key=key):
                    self.assertFalse(self.preserved(location, old, changed))
            changed = dict(new, addedTechnicalField="UNAPPROVED")
            self.assertFalse(self.preserved(location, old, changed))
            self.assertFalse(self.preserved((location[0], location[1], location[2] + 100), old, new))
            for key in ("question", "explanation"):
                if old.get(key) != new.get(key):
                    self.assertFalse(self.preserved(location, old, dict(new, **{key: new[key] + " UNAPPROVED"})))
                    self.assertFalse(self.preserved(location, dict(old, **{key: old[key] + " UNAPPROVED"}), new))

    def test_editorial_diagnostic_names_the_changed_field_instead_of_claiming_code_changed(self):
        message = contract.previous_exercise_difference(
            {"type": "output_prediction", "code": "same", "explanation": "old"},
            {"type": "output_prediction", "code": "same", "explanation": "new"})
        self.assertIn("explanation", message)
        self.assertNotIn("code", message)
        self.assertNotIn("respuesta", message)

    def test_all_seven_actual_shared_puzzles_pass_without_altering_content(self):
        for path, pn, bn, block in self.published_math():
            with self.subTest(lesson=path.name, page=pn + 1, block=bn + 1):
                previous = copy.deepcopy(block)
                contract.ERRORS.clear()
                contract.validate_math_puzzle_contract(block, "current original")
                self.assertEqual([], contract.ERRORS)
                self.assertEqual(previous, block)
                self.assertTrue(set(block) <= contract.FIELDS["math_puzzle"])
                self.assertIn("math_puzzle", contract.EXERCISES)

    def test_existing_math_nested_answers_ids_order_and_feedback_remain_protected(self):
        for path, pn, bn, block in self.published_math():
            relative = str(path.relative_to(contract.ROOT))
            for key in block:
                changed = copy.deepcopy(block)
                changed[key] = "UNAPPROVED"
                with self.subTest(lesson=path.name, key=key):
                    self.assertFalse(contract.preserves_previous_exercise(relative, pn, bn, block, changed))
            changed = copy.deepcopy(block)
            changed["responses"][0]["validator"]["expected"] = "UNAPPROVED"
            self.assertFalse(contract.preserves_previous_exercise(relative, pn, bn, block, changed))

    def assert_invalid(self, block):
        contract.ERRORS.clear()
        contract.validate_math_puzzle_contract(block, "malformed fixture")
        self.assertTrue(contract.ERRORS)

    def test_matching_rejects_unknown_missing_repeated_targets_and_bad_ids(self):
        block = next(block for _, _, _, block in self.published_math()
                     if block["responses"][0]["type"] == "math_matching_drag")
        response = block["responses"][0]
        expected = response["validator"]["expected"]
        first, second = list(expected)[:2]
        mutations = []
        for value in (None, [], {}, {first: expected[first]}, dict(expected, unknown="missing"),
                      dict(expected, **{first: "missing"}), dict(expected, **{first: expected[second]}),
                      dict(expected, **{first: []})):
            changed = copy.deepcopy(block)
            changed["responses"][0]["validator"]["expected"] = value
            mutations.append(changed)
        for key in ("sources", "targets"):
            for value in ([], "wrong", [None], [{"id": []}], [{"id": "same"}, {"id": "same"}]):
                changed = copy.deepcopy(block)
                changed["responses"][0][key] = value
                mutations.append(changed)
        for changed in mutations:
            self.assert_invalid(changed)

    def test_table_contract_rejects_missing_fields_wrong_validators_and_missing_dropdown_answers(self):
        block = next(block for _, _, _, block in self.published_math()
                     if block["responses"][0]["type"] == "math_multi_input")
        response = block["responses"][0]
        for key, value in (("fields", []), ("fields", "wrong"), ("validator", []),
                           ("validator", {"type": "fields", "fields": {}}),
                           ("validator", {"type": "matching", "expected": {}})):
            changed = copy.deepcopy(block)
            changed["responses"][0][key] = value
            self.assert_invalid(changed)
        field_id = response["fields"][0]["id"]
        for value in ("not a number", None, False, "NaN", "1/0", "1e400", []):
            changed = copy.deepcopy(block)
            changed["responses"][0]["validator"]["fields"][field_id] = {"type": "numeric", "expected": value}
            self.assert_invalid(changed)
        dropdown = {"type": "math_puzzle", "prompt": "Choose", "responses": [{
            "id": "a", "type": "math_multi_input", "fields": [
                {"id": "f", "type": "dropdown", "options": [{"id": "real"}]}],
            "validator": {"type": "fields", "fields": {"f": {"type": "text", "expected": "missing"}}}}]}
        self.assert_invalid(dropdown)

    def test_unknown_versions_response_families_and_bad_composition_are_not_silently_accepted(self):
        block = self.published_math()[0][3]
        for key, value in (("schemaVersion", True), ("version", 2), ("prompt", " "),
                           ("responses", []), ("responses", [None]), ("representations", "wrong"),
                           ("tools", [{}]), ("feedback", [])):
            changed = dict(block, **{key: value})
            self.assert_invalid(changed)
        for mutation in ("type", "id", "validator"):
            changed = copy.deepcopy(block)
            changed["responses"][0][mutation] = "unknown" if mutation != "id" else ""
            self.assert_invalid(changed)
        changed = copy.deepcopy(block)
        changed["responses"].append(copy.deepcopy(changed["responses"][0]))
        self.assert_invalid(changed)

    def test_finite_numeric_and_tolerance_guards_match_the_native_bounded_syntax(self):
        for value in ("12", "11.25", "-1/2", "50%", "1,5", "−3", "1e-3", 0, 1.25):
            self.assertTrue(contract.is_finite_math_number(value), value)
        for value in (None, True, {}, [], "", "1/0", "NaN", "Infinity", "1_000", "2+3", "1e400", "1e-1001"):
            self.assertFalse(contract.is_finite_math_number(value), value)
        for value in (-1, "NaN", None, True, [], "Infinity"):
            contract.ERRORS.clear()
            contract.validate_math_tolerance({"tolerance": value}, "fixture")
            self.assertTrue(contract.ERRORS)


class ReviewedV2EditorialContractTest(unittest.TestCase):
    """The two reviewed v2 edits keep their original exercise and provenance."""

    cases = (
        ("repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json", 10, 6, "question",
         "Ahora las órdenes cambian entre vueltas: 1, 2 y 3. Predice el resumen sin ejecutar primero; explica qué orden aporta a cada contador.",
         "Ahora las órdenes cambian entre vueltas: 1, 2 y 3. Predice el resumen sin ejecutar primero.",
         ["1:2:1"]),
        ("colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json", 8, 3, "explanation",
         "Copiar el vector crea otra colección con copias de sus elementos. Modificar la copia no altera el origen.",
         "Copiar el vector crea otra colección con copias de sus elementos (Köppe et al., 2023). Modificar la copia no altera el origen.",
         ["3 5"]),
    )

    def fixtures(self):
        for revision in ("329f0e4", "d24f7df"):
            for lesson, page, block, field, before_text, after_text, answers in self.cases:
                previous = historical_lesson(revision, lesson)
                current = contract.load(contract.CONTENT / lesson)
                # Exercise approvals use immutable coordinates, even when the
                # visible activity has moved to accommodate the new puzzle.
                aligned = state_layout.align_for_previous_validation(previous, current)
                pn, bn = next(position for position, origin in state_layout.validate(previous).items()
                              if origin == (page - 1, block - 1))
                old = previous["pages"][pn]["blocks"][bn]
                new = aligned["pages"][pn]["blocks"][bn]
                self.assertEqual(before_text, old[field])
                self.assertEqual(after_text, new[field])
                self.assertEqual(answers, old["answers"])
                self.assertEqual(answers, new["answers"])
                yield revision, (lesson, page, block), field, previous, current, old, new

    def preserved(self, location, old, new):
        lesson, page, block = location
        relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
        return contract.preserves_previous_exercise(relative, page - 1, block - 1, old, new)

    def test_both_reviewed_edits_pass_with_their_real_published_v2_proofs(self):
        for revision, location, field, previous, current, old, new in self.fixtures():
            with self.subTest(revision=revision, location=location):
                self.assertEqual(2, current["stateLayout"]["version"])
                state_layout.validate_transition(previous, current)
                self.assertEqual({field}, {key for key in old.keys() | new.keys()
                                          if old.get(key) != new.get(key)})
                self.assertTrue(self.preserved(location, old, new))

    def test_exact_text_approval_cannot_change_any_other_exercise_field_or_location(self):
        for revision, location, field, _, _, old, new in self.fixtures():
            for key in new.keys() - {"reviewFeedback"}:
                changed = copy.deepcopy(new)
                changed[key] = "UNAPPROVED CHANGE"
                with self.subTest(revision=revision, location=location, key=key):
                    self.assertFalse(self.preserved(location, old, changed))
            self.assertFalse(self.preserved(location, old, dict(new, addedTechnicalField="UNAPPROVED")))
            self.assertFalse(self.preserved(location, dict(old, **{field: old[field] + " UNAPPROVED"}), new))
            self.assertFalse(self.preserved((location[0], location[1], location[2] + 1), old, new))
            self.assertFalse(self.preserved((location[0], location[1] + 1, location[2]), old, new))

    def test_approved_wording_still_requires_the_exact_published_origin_and_editorial_proof(self):
        for revision, location, _, previous, current, _, _ in self.fixtures():
            for target in ("origin", "family", "editorial"):
                changed = copy.deepcopy(current)
                if target == "origin":
                    changed["stateLayout"]["origin"][0]["blocks"][0]["sha256"] = "0" * 64
                elif target == "family":
                    changed["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                else:
                    changed["stateLayout"]["editorial"] = []
                with self.subTest(revision=revision, location=location, target=target):
                    with self.assertRaises(state_layout.LayoutError):
                        state_layout.align_for_previous_validation(previous, changed)


class RetirementEditorialContractTest(unittest.TestCase):
    """The four reviewed retirement edits keep immutable exercise owners."""

    cases = (
        ("programas_y_ejecucion/lesson_primer_programa_y_salida.json", 5, 4,
         "prediccion_zona_inicial", "question",
         "3e7fa5f573c566bc17a7308398d90ee0a849aa6cb849bb89f9a2d5d643b7b01c",
         "a1299f5fcc562260414ac9136337d77199cb18e4d362eec976c201ed34a2e43f"),
        ("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json", 7, 4,
         "prediccion_informe_inicial", "question",
         "7f48db3071af57952889a2c2680f14dba906231d495638855feb71c4bf23c53a",
         "aac3dd2c9535276b6513a0adc2736bf5a5c84bc956dfa180fda9543813c524cd"),
        ("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json", 4, 6,
         "prediccion_revision_inicial", "question",
         "daf9bb92af0fb387f61efc5a0062adf550c1bdffd15ab2a4be5b5b3f6b874d34",
         "cb36f39faab36ecb05dcf50b3f0a7a2509e1d541caa43a812a7bc5174bb80d00"),
        ("programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json", 4, 6,
         "prediccion_revision_inicial", "explanation",
         "ba4e030990f073ec920b75f627c1595317a7c58d2d12a6dc946744af32acfb11",
         "0f7908a8cedad6bd6e7ed1a8b9162c6d05752373b614a85b93b1d55b7b2e1761"),
    )

    def fixtures(self):
        for lesson, page, block, identity, field, before_hash, after_hash in self.cases:
            previous = historical_lesson("d1933ed", lesson)
            current = contract.load(contract.CONTENT / lesson)
            aligned = state_layout.align_for_previous_validation(previous, current)
            position = next(position for position, origin in state_layout.validate(previous).items()
                            if origin == (page - 1, block - 1))
            pn, bn = position
            old = previous["pages"][pn]["blocks"][bn]
            new = aligned["pages"][pn]["blocks"][bn]
            self.assertEqual(identity, old["id"])
            self.assertEqual(identity, new["id"])
            self.assertEqual((before_hash, after_hash), (digest(old[field]), digest(new[field])))
            yield (lesson, page, block), field, previous, current, old, new

    def preserved(self, location, old, new):
        lesson, page, block = location
        relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
        return contract.preserves_previous_exercise(relative, page - 1, block - 1, old, new)

    def test_only_four_pinned_edits_pass_against_the_real_pre_retirement_tree(self):
        self.assertEqual(4, len(contract.RETIREMENT_EDITORIAL_REVISIONS))
        self.assertEqual(29, len(contract.APPROVED_EDITORIAL_REVISIONS))
        for location, _, previous, current, old, new in self.fixtures():
            with self.subTest(location=location):
                state_layout.validate_transition(previous, current)
                self.assertEqual(2, current["stateLayout"]["version"])
                self.assertTrue(self.preserved(location, old, new))
                self.assertEqual(old["code"], new["code"])
                self.assertEqual(old["answers"], new["answers"])
                self.assertEqual("exact", old["outputComparison"])
                self.assertEqual(old["outputComparison"], new["outputComparison"])

    def test_pinned_text_does_not_license_technical_changes_or_other_owners(self):
        for location, _, _, _, old, new in self.fixtures():
            for field in new.keys() - {"reviewFeedback"}:
                changed = copy.deepcopy(new)
                changed[field] = "UNAPPROVED CHANGE"
                with self.subTest(location=location, field=field):
                    self.assertFalse(self.preserved(location, old, changed))
            self.assertFalse(self.preserved(location, old, dict(new, addedTechnicalField="UNAPPROVED")))
            self.assertFalse(self.preserved((location[0], location[1] + 1, location[2]), old, new))
            self.assertFalse(self.preserved((location[0], location[1], location[2] + 1), old, new))
            self.assertFalse(self.preserved(("programas_y_ejecucion/lesson_otro_programa.json", *location[1:]), old, new))
            # Matching text and coordinates are insufficient for another owner,
            # including when both copied blocks carry the same replacement ID.
            self.assertFalse(self.preserved(location, dict(old, id="other_owner"), dict(new, id="other_owner")))
            self.assertFalse(self.preserved(location, dict(old, type="fill_code"), dict(new, type="fill_code")))
            no_id_before, no_id_after = dict(old), dict(new)
            del no_id_before["id"]
            del no_id_after["id"]
            self.assertFalse(self.preserved(location, no_id_before, no_id_after))

    def test_both_text_hashes_and_their_direction_remain_exact(self):
        for location, field, _, _, old, new in self.fixtures():
            with self.subTest(location=location, field=field):
                self.assertFalse(self.preserved(location, dict(old, **{field: old[field] + " "}), new))
                self.assertFalse(self.preserved(location, old, dict(new, **{field: new[field] + " "})))
                self.assertFalse(self.preserved(location, new, old))
                # The same before/after phrases in the other editorial field
                # do not inherit this field's authorization.
                other = "explanation" if field == "question" else "question"
                moved_old, moved_new = dict(old), dict(old)
                moved_old[other], moved_new[other] = old[field], new[field]
                self.assertFalse(self.preserved(location, moved_old, moved_new))

    def test_editorial_approval_cannot_rewrite_origin_family_or_retirement_history(self):
        for location, _, previous, current, _, _ in self.fixtures():
            for target in ("origin", "family", "editorial", "retired", "retirementHistory"):
                changed = copy.deepcopy(current)
                if target == "origin":
                    changed["stateLayout"]["origin"][0]["blocks"][0]["sha256"] = "0" * 64
                elif target == "family":
                    changed["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                elif target == "editorial":
                    changed["stateLayout"]["editorial"] = []
                elif target == "retired":
                    changed["stateLayout"]["retired"][-1]["block"]["initialCode"] = "UNAPPROVED SOURCE"
                else:
                    changed["stateLayout"]["retirementHistory"][0]["pages"][0]["blocks"][0]["text"] = "UNAPPROVED HISTORY"
                with self.subTest(location=location, target=target):
                    with self.assertRaises(state_layout.LayoutError):
                        state_layout.align_for_previous_validation(previous, changed)


if __name__ == "__main__":
    unittest.main()

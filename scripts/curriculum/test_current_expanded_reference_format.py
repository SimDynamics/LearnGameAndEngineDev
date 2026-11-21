"""Verify physical reformatting of already-expanded compiler references.

These current U11 lessons have source ownership paired with complete compiler
revisions. Each proposal is authored in memory with the real es/en/fr family.
No curriculum, learner draft, output oracle, receipt, or historic archive is
rewritten by this test.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "app/src/main/assets/content"
UNIT = "programming/cpp_fundamentals/texto_interpretacion_y_flujos"
CASES = (
    ("tokens_con_categoria_y_valor", "block_0007_0001"),
    ("numeros_de_varios_digitos_y_limites", "block_0008_0001"),
    ("una_gramatica_para_sumar_y_restar", "block_0008_0001"),
    ("terminos_prioridad_y_division_segura", "block_0008_0001"),
    ("signos_unarios_parentesis_y_progreso_recursivo", "block_0009_0001"),
    ("diagnosticar_y_recuperar_hasta_fin_de_operacion", "block_0008_0001"),
    ("construir_un_interprete_con_historial_y_pruebas", "block_0009_0001"),
    ("construir_un_interprete_con_historial_y_pruebas", "block_0010_0001"),
    ("resto_y_asociacion_en_el_interprete", "block_0006_0001"),
)


def published(lesson):
    relative = Path(UNIT) / ("lesson_" + lesson + ".json")
    file = ASSETS / "es" / relative
    original_bytes = file.read_bytes()
    document = json.loads(original_bytes)
    family = {
        locale: json.loads((ASSETS / locale / relative).read_bytes())
        for locale in document["stateLayout"]["family"]
    }
    return file, original_bytes, document, family


class ExpandedReferenceReflow(unittest.TestCase):
    def test_nine_current_references_accept_new_lexical_spacing(self):
        for lesson, owner in CASES:
            with self.subTest(lesson=lesson, owner=owner):
                file, original_bytes, before, family = published(lesson)
                prev = before["stateLayout"]
                compiler_refs = {
                    entry["id"]: entry["block"]
                    for record in prev.get("compilerRevisions", [])
                    for entry in record.get("references", [])
                }
                self.assertIn(owner, compiler_refs)
                self.assertFalse(any(entry["id"] == owner for entry in
                                     prev.get("sourceFormatRevisions", [])))
                original = state._blocks(before)[owner]
                self.assertEqual(original["type"], "code")
                source = original["code"]
                self.assertIn("\n\n", source)

                proposed = deepcopy(before)
                revised_reference = state._blocks(proposed)[owner]
                revised_reference["code"] = source.replace("\n\n", "\n", 1)
                self.assertTrue(state.code_format.require_source_format(
                    original, revised_reference))
                after = state.prepare_v2(proposed, "es", family, before)
                state.validate_transition(before, after)
                self.assertEqual(state.validate(before), state.validate(after))
                for key in ("origin", "current", "family", "additions",
                            "compilerRevisions", "retirementHistory"):
                    self.assertEqual(prev.get(key), after["stateLayout"].get(key))
                self.assertEqual(before.get("referenceAfter"),
                                 after.get("referenceAfter"))
                self.assertEqual(before.get("activityAfter"),
                                 after.get("activityAfter"))
                old_formats = prev.get("sourceFormatRevisions", [])
                new_formats = after["stateLayout"]["sourceFormatRevisions"]
                self.assertEqual(new_formats[:len(old_formats)], old_formats)
                self.assertEqual(new_formats[len(old_formats):],
                                 [{"id": owner, "block": original}])
                old_proofs = prev["editorial"]
                new_proofs = after["stateLayout"]["editorial"]
                self.assertEqual(new_proofs[:len(old_proofs)], old_proofs)
                self.assertEqual(len(new_proofs), len(old_proofs) + 1)
                self.assertEqual(new_proofs[-1]["id"], owner)
                self.assertEqual(new_proofs[-1]["beforeSha256"],
                                 state.fingerprint(original))
                self.assertEqual(new_proofs[-1]["afterSha256"],
                                 state.fingerprint(revised_reference))
                self.assertEqual(file.read_bytes(), original_bytes)

    def test_expanded_reference_rejects_modified_cpp_or_foreign_archive(self):
        lesson, owner = CASES[0]
        file, original_bytes, before, family = published(lesson)
        proposed = deepcopy(before)
        block = state._blocks(proposed)[owner]
        block["code"] = block["code"].replace("\n\n", "\n", 1)
        self.assertIn("#include <iostream>", block["code"])
        block["code"] = block["code"].replace(
            "#include <iostream>", "#include <fstream>", 1)
        with self.assertRaises(state.LayoutError):
            state.prepare_v2(proposed, "es", family, before)

        legitimate = deepcopy(before)
        state._blocks(legitimate)[owner]["code"] = (
            state._blocks(before)[owner]["code"].replace("\n\n", "\n", 1))
        after = state.prepare_v2(legitimate, "es", family, before)
        archive = next(entry for entry in after["stateLayout"]["sourceFormatRevisions"]
                       if entry["id"] == owner)
        archive["block"] = deepcopy(state._blocks(before)[
            next(identity for identity in state._blocks(before) if identity != owner)])
        with self.assertRaises(state.LayoutError):
            state.validate_transition(before, after)
        self.assertEqual(file.read_bytes(), original_bytes)


if __name__ == "__main__":
    unittest.main()

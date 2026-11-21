"""Verify combined source-spacing and authored feedback on real published lessons.

Proposals are in memory only. Stable block identities, previous response goals,
family lineage and complete editorial history must survive a single authorized
revision without changing Work's current curricular JSON or learner profiles.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
SCENARIOS = (
    ("errores_y_construccion_gradual/lesson_validar_antes_y_elegir_reporte.json",
     "block_0000_0003", "code", "/explanation"),
    ("datos_propios_y_estado_valido/lesson_nombrar_alternativas_con_enum_class.json",
     "block_0001_0002", "code", "/explanation"),
    ("datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json",
     "block_0001_0002", "code", "/explanation"),
    ("aleatoriedad_y_reproducibilidad/lesson_combinar_ensayos_generados_y_casos_elegidos.json",
     "block_0004_0007", "initialCode", "/outputCheck/cases/0/explanation"),
)


class CurrentCombinedFormatGuidanceTest(unittest.TestCase):
    def proposal(self, relative, identity, source_field, guidance_path):
        path = CONTENT / relative
        published_bytes = path.read_bytes()
        original = json.loads(published_bytes)
        self.assertEqual(original["stateLayout"]["version"], 2)
        self.assertEqual(original["stateLayout"]["locale"], "es")
        self.assertIn(identity, layout._blocks(original))

        proposal = deepcopy(original)
        old = layout._blocks(original)[identity]
        newer = layout._blocks(proposal)[identity]
        source = old[source_field]
        # A real physical blank line is removed, but the C++ tokens and every
        # mandatory line remain unchanged. Do not alter stdout/stderr or inputs.
        self.assertIn("\n\n", source)
        newer[source_field] = source.replace("\n\n", "\n", 1)
        self.assertNotEqual(newer[source_field], source)

        if guidance_path == "/explanation":
            newer["explanation"] += "\n\nAclaración de prueba sin modificar la respuesta."
        else:
            newer["outputCheck"]["cases"][0]["explanation"] += (
                "\n\nAclaración de prueba para este caso sin alterar su resultado.")
        fields = proposal["textSemantics"]["fields"]
        indices = [i for i, field in enumerate(fields)
                   if field["block"] == identity and field["path"] == guidance_path]
        self.assertEqual(len(indices), 1)
        slot = indices[0]
        old_spans = deepcopy(fields[slot]["spans"])
        fields[slot] = layout.text_semantics.make_field(
            proposal, identity, guidance_path, old_spans)
        return original, proposal, old, newer, published_bytes, path

    @staticmethod
    def published_family(relative, before):
        base = ROOT / "app/src/main/assets/content"
        return {
            locale: json.loads((base / locale / "programming/cpp_fundamentals"
                                / relative).read_bytes())
            for locale in before["stateLayout"]["family"]
        }

    def test_active_guidance_and_spacing_share_one_authenticated_transition(self):
        for relative, identity, source_field, guidance_path in SCENARIOS:
            with self.subTest(lesson=relative, block=identity):
                before, proposed, old, newer, bytes_before, path = self.proposal(
                    relative, identity, source_field, guidance_path)
                self.assertTrue(layout.require_source_format_with_editorial(old, newer))
                reviewed = layout.prepare_v2(proposed, "es", self.published_family(relative, before), before)
                layout.validate_transition(before, reviewed)
                self.assertEqual(layout.validate(before), layout.validate(reviewed))
                for field in ("origin", "current", "family", "additions"):
                    self.assertEqual(before["stateLayout"][field],
                                     reviewed["stateLayout"][field])
                prior_formats = before["stateLayout"].get("sourceFormatRevisions", [])
                after_formats = reviewed["stateLayout"].get("sourceFormatRevisions", [])
                self.assertEqual(after_formats[:len(prior_formats)], prior_formats)
                self.assertEqual(after_formats[len(prior_formats):],
                                 [{"id": identity, "block": old}])
                prior_editorial = before["stateLayout"]["editorial"]
                after_editorial = reviewed["stateLayout"]["editorial"]
                self.assertEqual(after_editorial[:len(prior_editorial)], prior_editorial)
                self.assertEqual(len(after_editorial), len(prior_editorial) + 1)
                self.assertEqual(after_editorial[-1]["id"], identity)
                self.assertEqual(after_editorial[-1]["beforeSha256"],
                                 layout.fingerprint(old))
                self.assertEqual(after_editorial[-1]["afterSha256"],
                                 layout.fingerprint(layout._blocks(reviewed)[identity]))
                self.assertEqual(layout.code_format.source_projection(newer, old),
                                 {**newer, source_field: source_field and old[source_field]})
                self.assertEqual(path.read_bytes(), bytes_before)

    def test_no_authorship_from_unmarked_guidance_or_changed_code(self):
        for relative, identity, source_field, guidance_path in SCENARIOS:
            with self.subTest(lesson=relative, block=identity):
                before, proposed, old, newer, bytes_before, path = self.proposal(
                    relative, identity, source_field, guidance_path)
                unmarked = deepcopy(proposed)
                unmarked["textSemantics"] = deepcopy(before["textSemantics"])
                with self.assertRaises(layout.LayoutError):
                    layout.prepare_v2(unmarked, "es", self.published_family(relative, before), before)
                forged = deepcopy(proposed)
                owner = layout._blocks(forged)[identity]
                # The U10L4 starter has no output statement yet. Change the
                # defective comparison token instead, never invent stdout.
                if source_field == "initialCode":
                    self.assertIn("dato>minimo", owner[source_field])
                    owner[source_field] = owner[source_field].replace(
                        "dato>minimo", "dato>=minimo", 1)
                else:
                    self.assertIn("std::cout", owner[source_field])
                    owner[source_field] = owner[source_field].replace(
                        "std::cout", "std::cerr", 1)
                with self.assertRaises(layout.LayoutError):
                    layout.prepare_v2(forged, "es", self.published_family(relative, before), before)
                self.assertEqual(path.read_bytes(), bytes_before)


if __name__ == "__main__":
    unittest.main()

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
from format_batch_history import Publication, active_owners, publication_bytes

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
PUBLICATIONS = {
    "first-format": "921e1df9171992ab7a9f1b3803b963b02fd45bbc",
    "coexisting-format": "946faafa37a396b6fc4485ba2b60f9931a7bbdfb",
}
PUBLICATION_PINS = {'construir_un_interprete_con_historial_y_pruebas': {'en': 'b821159b28e8165c10e4bf6b23361b59d8eba0e1213dde4c3f617952616d86b6',
                                                     'es': ('b1d0206bc4ea3e3fcb960cb0b91322a850be3ddb2ef3a34221334215e24abfe2',
                                                            '7b8704f4d738e56cb873358929b36b49ca9f159f2652fa414470395c1408f1a9'),
                                                     'fr': '853f786bf08cb1ea0434df6ac45026b5c95de6cee09c26097feda5dae92de83b'},
 'diagnosticar_y_recuperar_hasta_fin_de_operacion': {'en': '4225fef70fc80bdd8323a1e4b6f9284d0c0f6d6e6bb3366a039edefbcfa4078b',
                                                     'es': ('6ce999bc1d2ed19ce71c5f995380c51f4ea734fc2ae04ed9915fa7bdf0a4cbb8',
                                                            '65b762c8cff56c1fbf5c33094be61dc3d0e989ad1798d110cf80344b21e91393'),
                                                     'fr': '1ce3751a6d16370ac784eb783df3bf74dce26fdbb6c90b38b3bbd6cf2f7e34f7'},
 'numeros_de_varios_digitos_y_limites': {'en': 'f52fede751dcdcbf93465cf67e84704dcb871fc9197b56e552021255ffe0b4a5',
                                         'es': ('6b4187b4411467892240ae00b0311d325b48150f90fd547c706bb5e93cfc0f0d',
                                                '605e256e4bb03ce748160bac51c7d3af95191d68fff0ffe661bed34d0ffd64b2'),
                                         'fr': '66d3d0f63299004ded9d190af1d3cf04c908246d18625049fa7f1294d85bc714'},
 'resto_y_asociacion_en_el_interprete': {'en': 'dfe05ab3d615f38091aecbfd3bd8ef49cec0b489cdd55a31c21aa729ad2ec8cf',
                                         'es': ('6ca35f0d3cb348e453009a3fa8d98e8e45efd927d211598265828a3f3320895d',
                                                'b7179c7e6a1e5e0a51106878a556a08b7a7606b0552910b97c1d16651db6b205'),
                                         'fr': 'fa5eae36ab2bc32544d37839476e63ae7f687228d80ffcfbbaa7870552a93f87'},
 'signos_unarios_parentesis_y_progreso_recursivo': {'en': '22a26b0108a011becc8d718694b622c467c50693cb3f4975c4ee9a5eccd5b1ea',
                                                    'es': ('222111911e25b890ea9a8582b7d66d3394f69178b219c9c5282bd61c8fd69a49',
                                                           '88534164406bfcf3ed951e029eef924e9d1de1ff4488d3dd26b8ba791c4462b2'),
                                                    'fr': 'a394dc2158317e2c7b3ee5298a970d79b449269b0432dd452513d91dab21dd5c'},
 'terminos_prioridad_y_division_segura': {'en': 'd650e04c0b2d5ab59ec459e21c65964e5e29a9989fc99a14d94ca770f3eec631',
                                          'es': ('c5f88dcd96187ebd74e3a40221d79893a979991d073a1e9aaad0309a07d920ac',
                                                 '4f672a4b47a8a641678219709d0d28cd9daf602c2992fce8d81fe48df5c283b6'),
                                          'fr': '5a3166666b5a0f818e88dd465c9804567dff6cca8097f49add5c2a248b2a2471'},
 'tokens_con_categoria_y_valor': {'en': '067b6be16cd36e5606d0c45a99ef50fa573c7f42812617df34243b7265e3f603',
                                  'es': ('82bb7c21d96e30674b470beeed3cc19a4c4d2b0fd21409163579678ae45173db',
                                         '5cf2807129ac1b3fbb42fe0dea9576db74a7d5a47db32e89dea386f24ccbf870'),
                                  'fr': '21d0dd8dd3394b10677a08994e19f2fd18a98d19ed64b29b232c81945412a6b2'},
 'una_gramatica_para_sumar_y_restar': {'en': '482d555a8f8abc517c362acc608dac5e5a3110b058a7b72035dab940a245400b',
                                       'es': ('70772736b2b4ba184a33afbd1348fbbe5d6ff342cd96cb6f386b1b33d1d0d331',
                                              'ae347151fd875d7dc0f1b5449a6087050e13cbd7c33597641c5fa7671ac244ac'),
                                       'fr': 'f2155f5046c14bbb0827057569bb92a160c8347f48d969221227c6cac40f5bcf'}}


def published(lesson, publication="working-tree"):
    relative = Path(UNIT) / ("lesson_" + lesson + ".json")
    file = ASSETS / "es" / relative
    original_bytes = file.read_bytes()
    if publication == "working-tree":
        document = json.loads(original_bytes)
        family = {locale: json.loads((ASSETS / locale / relative).read_bytes())
                  for locale in document["stateLayout"]["family"]}
    else:
        edition = tuple(PUBLICATIONS).index(publication)
        family = {}
        for locale, pins in PUBLICATION_PINS[lesson].items():
            digest = pins[edition] if locale == "es" else pins
            path = f"app/src/main/assets/content/{locale}/{relative.as_posix()}"
            family[locale] = json.loads(publication_bytes(
                ROOT, path, Publication(PUBLICATIONS[publication], digest)))
        document = family["es"]
    return file, original_bytes, document, family


class ExpandedReferenceReflow(unittest.TestCase):
    def publications(self, lesson, owner):
        inputs = {name: published(lesson, name) for name in (*PUBLICATIONS, "working-tree")}
        first, coexisting, current = (inputs[name][2] for name in inputs)
        first_archives = {entry["id"] for entry in
                          first["stateLayout"].get("sourceFormatRevisions", [])}
        archived = {entry["id"]: entry["block"] for entry in
                    coexisting["stateLayout"]["sourceFormatRevisions"]}
        self.assertNotIn(owner, first_archives)
        self.assertEqual(archived[owner], active_owners(first)[0][owner])
        for before, after in ((first, coexisting), (coexisting, current)):
            state.validate_transition(before, after)
            for field in ("sourceFormatRevisions", "editorial"):
                previous = before["stateLayout"].get(field, [])
                self.assertEqual(after["stateLayout"][field][:len(previous)], previous)
        relative = Path(UNIT) / ("lesson_" + lesson + ".json")
        files = {locale: (ASSETS / locale / relative).read_bytes()
                 for locale in inputs["working-tree"][3]}
        for name, values in inputs.items():
            unchanged = deepcopy(values[3])
            yield name, values
            self.assertEqual(values[3], unchanged)
            for locale, payload in files.items():
                self.assertEqual((ASSETS / locale / relative).read_bytes(), payload)

    def test_nine_current_references_accept_new_lexical_spacing(self):
        for lesson, owner in CASES:
            for publication, values in self.publications(lesson, owner):
                with self.subTest(lesson=lesson, owner=owner, publication=publication):
                    file, original_bytes, before, family = values
                    prev = before["stateLayout"]
                    compiler_refs = {
                        entry["id"]: entry["block"]
                        for record in prev.get("compilerRevisions", [])
                        for entry in record.get("references", [])
                    }
                    self.assertIn(owner, compiler_refs)
                    original = active_owners(before)[0][owner]
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
                    self.assertEqual(state._blocks(after), state._blocks(proposed))
                    excluded = {"sourceFormatRevisions", "editorial"}
                    self.assertEqual({key: value for key, value in prev.items() if key not in excluded},
                                     {key: value for key, value in after["stateLayout"].items()
                                      if key not in excluded})
                    for field in ("referenceAfter", "activityAfter", "textSemantics"):
                        self.assertEqual(before.get(field), after.get(field))
                    old_formats = prev.get("sourceFormatRevisions", [])
                    old_archives = {entry["id"]: entry for entry in old_formats}
                    new_formats = after["stateLayout"]["sourceFormatRevisions"]
                    archives = {entry["id"]: entry for entry in new_formats}
                    self.assertEqual(len(old_archives), len(old_formats))
                    self.assertEqual(len(archives), len(new_formats))
                    self.assertEqual(new_formats[:len(old_formats)], old_formats)
                    addition = [] if owner in old_archives else [{"id": owner, "block": original}]
                    self.assertEqual(new_formats[len(old_formats):], addition)
                    self.assertEqual(archives[owner], old_archives.get(owner, {"id": owner, "block": original}))
                    old_proofs = prev["editorial"]
                    new_proofs = after["stateLayout"]["editorial"]
                    self.assertEqual(new_proofs[:len(old_proofs)], old_proofs)
                    self.assertEqual(new_proofs[len(old_proofs):], [{
                        "id": owner, "beforeSha256": state.fingerprint(original),
                        "afterSha256": state.fingerprint(revised_reference),
                    }])
                    self.assertEqual(file.read_bytes(), original_bytes)

    def test_expanded_reference_rejects_modified_cpp_or_foreign_archive(self):
        lesson, owner = CASES[0]
        for publication, values in self.publications(lesson, owner):
            with self.subTest(lesson=lesson, owner=owner, publication=publication):
                file, original_bytes, before, family = values
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

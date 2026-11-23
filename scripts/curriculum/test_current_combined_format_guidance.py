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
from format_batch_history import Publication, active_owners, publication_bytes

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

# Full releases cover both first formatting and formatting over an existing
# canonical snapshot. Translations have identical bytes in these two releases.
PUBLICATIONS = {
    "first-format": "921e1df9171992ab7a9f1b3803b963b02fd45bbc",
    "coexisting-format": "8b89fd4c99024e5b1b9406ac2f19fc53701213e6",
}
PUBLICATION_PINS = {
    "errores_y_construccion_gradual/lesson_validar_antes_y_elegir_reporte.json": {
        "es": ("d65de29e472e1f1518559c7e6e785d68943bf1378ff80f0b55278786bbd9f788",
               "84ae6c2c5f4bd7da82782def62d1b6adf9f6a78e4fad440c6e5a6c1206273c80"),
        "en": "0e0eab9f7fda4ea638596f1f81e5e523ef0fbd9fc59ef42646ed1e765168c4d0",
        "fr": "9ae89d6b6d74e9b65fc5b70efec82d3b80f9fd84bc384d07b97f222844a25288",
    },
    "datos_propios_y_estado_valido/lesson_nombrar_alternativas_con_enum_class.json": {
        "es": ("e8e154b4e95f36f477a1585377ea36090946d43a252617fe5017dc5646647fa4",
               "cfef31a0b8261b74ed64c55ed19604712dfb16be8e072d5d548e3a17925eaef3"),
        "en": "22bc8b1b2d4ce6410e8d9d3d265ae573f261bdbb09e3b86adf99fb1bb5bb58ec",
        "fr": "e972b4f72c9899f828405d981d0d4f9796911d8b3300feb2cd672dfc06698b76",
    },
    "datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json": {
        "es": ("7dfdff6e62f05a1ef01e8f713381ee809da07f07dc8bbaa945072a20ddf9a8a4",
               "48a2fe71fa4d28d3388964bd7bef12658d83037c282ae2df6accf5205c9c8be4"),
        "en": "1b9c3b9c7333436941743f44d188f06af6685570767984d327d8103538e65cf6",
        "fr": "975be6adf04a097923236454476f43ce4a6eb3891a587211e5751d861421e75b",
    },
    "aleatoriedad_y_reproducibilidad/lesson_combinar_ensayos_generados_y_casos_elegidos.json": {
        "es": ("26da228b3df2d13088897768a4823ff641fe7b0babe1788093c6c60216a86e6a",
               "b5319bcf554f6f5347375feee6c2ab778f50505cc6c013e2b101bffc74fbaf7d"),
        "en": "de1956f975fb4c876d95acd9502d73eb71e2b796aa34820e2fdb668e4a416194",
        "fr": "2a76dfb5c62cef396dd283b16d5c1a3a0690665406d6cbc1ae0a9b031497ec43",
    },
}


class CurrentCombinedFormatGuidanceTest(unittest.TestCase):
    def proposal(self, relative, identity, source_field, guidance_path, family):
        path = CONTENT / relative
        published_bytes = path.read_bytes()
        original = deepcopy(family["es"])
        self.assertEqual(original["stateLayout"]["version"], 2)
        self.assertEqual(original["stateLayout"]["locale"], "es")
        self.assertIn(identity, active_owners(original)[0])

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
    def published_family(relative, publication="working-tree"):
        base = ROOT / "app/src/main/assets/content"
        result = {}
        for locale, pins in PUBLICATION_PINS[relative].items():
            path = f"app/src/main/assets/content/{locale}/programming/cpp_fundamentals/{relative}"
            if publication == "working-tree":
                payload = (base / locale / "programming/cpp_fundamentals" / relative).read_bytes()
            else:
                edition = tuple(PUBLICATIONS).index(publication)
                digest = pins[edition] if locale == "es" else pins
                payload = publication_bytes(ROOT, path, Publication(PUBLICATIONS[publication], digest))
            result[locale] = json.loads(payload)
        return result

    def publications(self, relative, identity):
        first = self.published_family(relative, "first-format")
        coexisting = self.published_family(relative, "coexisting-format")
        current = self.published_family(relative)
        first_archives = {entry["id"] for entry in
                          first["es"]["stateLayout"].get("sourceFormatRevisions", [])}
        published_archives = {entry["id"]: entry["block"] for entry in
                              coexisting["es"]["stateLayout"]["sourceFormatRevisions"]}
        self.assertNotIn(identity, first_archives)
        self.assertEqual(published_archives[identity], active_owners(first["es"])[0][identity])
        layout.validate_transition(first["es"], coexisting["es"])
        layout.validate_transition(coexisting["es"], current["es"])
        for before, after in ((first["es"], coexisting["es"]),
                              (coexisting["es"], current["es"])):
            for field in ("sourceFormatRevisions", "editorial"):
                prior = before["stateLayout"].get(field, [])
                self.assertEqual(after["stateLayout"][field][:len(prior)], prior)
        files = {locale: (ROOT / "app/src/main/assets/content" / locale /
                          "programming/cpp_fundamentals" / relative).read_bytes()
                 for locale in current}
        for name, family in (("first-format", first), ("coexisting-format", coexisting),
                             ("working-tree", current)):
            unchanged = deepcopy(family)
            yield name, family
            self.assertEqual(family, unchanged)
            for locale, payload in files.items():
                self.assertEqual((ROOT / "app/src/main/assets/content" / locale /
                                  "programming/cpp_fundamentals" / relative).read_bytes(), payload)

    def assert_format_history(self, before, reviewed, identity, old):
        prior = before["stateLayout"].get("sourceFormatRevisions", [])
        after = reviewed["stateLayout"]["sourceFormatRevisions"]
        archives = {entry["id"]: entry for entry in prior}
        observed = {entry["id"]: entry for entry in after}
        self.assertEqual(len(archives), len(prior))
        self.assertEqual(len(observed), len(after))
        self.assertEqual(after[:len(prior)], prior)
        added = [] if identity in archives else [{"id": identity, "block": old}]
        self.assertEqual(after[len(prior):], added)
        self.assertEqual(observed[identity], archives.get(identity, {"id": identity, "block": old}))
        excluded = {"sourceFormatRevisions", "editorial"}
        self.assertEqual({key: value for key, value in before["stateLayout"].items()
                          if key not in excluded},
                         {key: value for key, value in reviewed["stateLayout"].items()
                          if key not in excluded})

    def test_active_guidance_and_spacing_share_one_authenticated_transition(self):
        for relative, identity, source_field, guidance_path in SCENARIOS:
            for publication, family in self.publications(relative, identity):
                with self.subTest(lesson=relative, block=identity, publication=publication):
                    before, proposed, old, newer, bytes_before, path = self.proposal(
                        relative, identity, source_field, guidance_path, family)
                    self.assertTrue(layout.require_source_format_with_editorial(old, newer))
                    reviewed = layout.prepare_v2(proposed, "es", family, before)
                    layout.validate_transition(before, reviewed)
                    self.assertEqual(layout.validate(before), layout.validate(reviewed))
                    for field in ("origin", "current", "family", "additions"):
                        self.assertEqual(before["stateLayout"][field],
                                         reviewed["stateLayout"][field])
                    self.assert_format_history(before, reviewed, identity, old)
                    prior_editorial = before["stateLayout"]["editorial"]
                    after_editorial = reviewed["stateLayout"]["editorial"]
                    self.assertEqual(after_editorial[:len(prior_editorial)], prior_editorial)
                    self.assertEqual(after_editorial[len(prior_editorial):], [{
                        "id": identity, "beforeSha256": layout.fingerprint(old),
                        "afterSha256": layout.fingerprint(layout._blocks(reviewed)[identity]),
                    }])
                    self.assertEqual(layout._blocks(reviewed), layout._blocks(proposed))
                    self.assertEqual(layout.code_format.source_projection(newer, old),
                                     {**newer, source_field: old[source_field]})
                    self.assertEqual(path.read_bytes(), bytes_before)

    def test_no_authorship_from_unmarked_guidance_or_changed_code(self):
        for relative, identity, source_field, guidance_path in SCENARIOS:
            for publication, family in self.publications(relative, identity):
                with self.subTest(lesson=relative, block=identity, publication=publication):
                    before, proposed, old, newer, bytes_before, path = self.proposal(
                        relative, identity, source_field, guidance_path, family)
                    unmarked = deepcopy(proposed)
                    unmarked["textSemantics"] = deepcopy(before["textSemantics"])
                    with self.assertRaises(layout.LayoutError):
                        layout.prepare_v2(unmarked, "es", family, before)
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
                        layout.prepare_v2(forged, "es", family, before)

                    reviewed = layout.prepare_v2(proposed, "es", family, before)
                    changed_archive = deepcopy(reviewed)
                    archive = next(entry for entry in changed_archive["stateLayout"]["sourceFormatRevisions"]
                                   if entry["id"] == identity)
                    archive["block"][source_field] += "\n"
                    with self.assertRaises(layout.LayoutError):
                        layout.validate_transition(before, changed_archive)
                    removed_history = deepcopy(reviewed)
                    self.assertTrue(before["stateLayout"]["editorial"])
                    del removed_history["stateLayout"]["editorial"][0]
                    with self.assertRaises(layout.LayoutError):
                        layout.validate_transition(before, removed_history)
                    self.assertEqual(path.read_bytes(), bytes_before)


if __name__ == "__main__":
    unittest.main()

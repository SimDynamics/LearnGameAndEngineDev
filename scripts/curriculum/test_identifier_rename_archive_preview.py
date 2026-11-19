"""Read-only authoring preview against all six real formatted P1 frequency owners."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import re
import unittest

import code_format_compat as formats
import identifier_rename_archive_preview as preview
import stable_state_layout as state

PATH = (
    Path(__file__).resolve().parents[2] /
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json"
)
OWNERS = (
    ("block_0006_0002", "etiquetas_mas_frecuentes"),
    ("recuperacion_entrada_editor", "etiquetas_mas_frecuentes"),
    ("block_0006_0009", "etiquetas_mas_frecuentes"),
    ("block_0007_0002", "etiquetas_mas_frecuentes"),
    ("block_0007_0004", "etiquetas_mas_frecuentes"),
    ("frecuencias_numericas_referencia_programa", "valores_mas_frecuentes"),
)


class FormattedOwnerRenamePreview(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_bytes = PATH.read_bytes()
        cls.lesson = json.loads(cls.original_bytes)
        state.validate(cls.lesson)
        cls.blocks = state._blocks(cls.lesson)

    @staticmethod
    def candidate(block, target):
        slot = "initialCode" if block["type"] == "cpp_compiler" else "code"
        altered = deepcopy(block)
        altered[slot] = re.sub(
            r"\bmayores\b", target,
            re.sub(r"\bmaxima\b", "frecuencia_maxima_encontrada", block[slot]))
        names = {slot: {"maxima": "frecuencia_maxima_encontrada",
                        "mayores": target}}
        counts = {slot: {"maxima": 5, "mayores": 5}}
        return slot, altered, names, counts

    def test_all_six_real_formatted_owners_have_authenticated_previews(self):
        self.assertEqual(len(OWNERS), 6)
        before = deepcopy(self.lesson)
        for identity, target in OWNERS:
            with self.subTest(owner=identity):
                source = self.blocks[identity]
                slot, changed, names, counts = self.candidate(source, target)
                result = preview.preview_published_rename(
                    self.lesson, identity, changed, names, counts)
                self.assertEqual(result["id"], identity)
                self.assertEqual(result["lessonId"], self.lesson["id"])
                self.assertEqual(result["locale"], "es")
                meta = self.lesson["stateLayout"]
                self.assertEqual(
                    result["historicalFamilySha256"],
                    meta["family"]["es"]["sha256"])
                self.assertEqual(
                    result["editorialPrefixSha256"],
                    state.fingerprint({
                        "lessonId": self.lesson["id"],
                        "locale": meta["locale"],
                        "editorial": meta["editorial"],
                    }))
                _, original_positions, original_digests = state._origin(meta["origin"])
                published_additions = [
                    item for item in meta["additions"] if item["id"] == identity]
                if published_additions:
                    self.assertEqual(len(published_additions), 1)
                    self.assertNotIn(identity, original_positions)
                    stored = published_additions[0]["storage"]
                    expected_storage = {
                        "page": stored["page"], "block": stored["block"]}
                    expected_original_digest = published_additions[0]["sha256"]
                else:
                    self.assertIn(identity, original_positions)
                    page, position = original_positions[identity]
                    expected_storage = {"page": page, "block": position}
                    expected_original_digest = original_digests[identity]
                self.assertEqual(result["stableStorage"], expected_storage)
                self.assertEqual(result["originalStateSha256"],
                                 expected_original_digest)
                self.assertEqual(result["block"], source)
                self.assertEqual(result["names"], names)
                self.assertEqual(result["occurrences"], counts)
                self.assertEqual(result["confirmed"], counts)
                self.assertEqual(result["beforeSha256"], state.fingerprint(source))
                self.assertEqual(result["afterSha256"], state.fingerprint(changed))
                self.assertNotEqual(result["beforeSha256"], result["afterSha256"])
                self.assertEqual(
                    formats.source_projection(changed, result["block"]), source)
                self.assertEqual(source["type"], changed["type"])
                self.assertIn('"Maxima:"', changed[slot])
                self.assertIn('"\\nMayores:"', changed[slot])
                self.assertEqual(source[slot].count("\n"), changed[slot].count("\n"))
                self.assertEqual(result["priorSourceFormatSha256"], state.fingerprint(
                    next(entry["block"] for entry in self.lesson["stateLayout"][
                        "sourceFormatRevisions"] if entry["id"] == identity)))
        self.assertEqual(self.lesson, before)
        self.assertEqual(PATH.read_bytes(), self.original_bytes)

    def test_wrong_operator_literal_or_archival_change_cannot_be_previewed(self):
        identity, target = OWNERS[0]
        source = self.blocks[identity]
        slot, candidate, names, counts = self.candidate(source, target)
        for before, after in (
            ("frecuencia > frecuencia_maxima_encontrada",
             "frecuencia >= frecuencia_maxima_encontrada"),
            ('"Maxima:"', '"Mayor:"'),
            ("frecuencia_maxima_encontrada", "maxima"),
        ):
            with self.subTest(change=after):
                proposal = deepcopy(candidate)
                self.assertIn(before, proposal[slot])
                proposal[slot] = proposal[slot].replace(before, after, 1)
                with self.assertRaises(preview.RenamePreviewError):
                    preview.preview_published_rename(
                        self.lesson, identity, proposal, names, counts)
        modified = deepcopy(candidate)
        modified["type"] = "paragraph"
        with self.assertRaises(preview.RenamePreviewError):
            preview.preview_published_rename(
                self.lesson, identity, modified, names, counts)

    def test_preview_does_not_appear_as_a_published_state_layout_archive(self):
        self.assertNotIn("identifierRenameRevisions", self.lesson["stateLayout"])
        identity, target = OWNERS[0]
        slot, candidate, names, counts = self.candidate(self.blocks[identity], target)
        proof = preview.preview_published_rename(
            self.lesson, identity, candidate, names, counts)
        self.assertIn("block", proof)
        self.assertIn("beforeSha256", proof)
        self.assertEqual(
            self.lesson["stateLayout"]["editorial"][-1],
            deepcopy(self.lesson)["stateLayout"]["editorial"][-1])
        self.assertEqual(PATH.read_bytes(), self.original_bytes)


if __name__ == "__main__":
    unittest.main()

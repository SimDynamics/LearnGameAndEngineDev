"""One matching-label correction must preserve its published response identity."""
from copy import deepcopy
from functools import lru_cache
import json
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "cbd9a38c19ff1f02068296f7e6cb9049e2c64212"
LESSON = "datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json"
RELATIVE = "app/src/main/assets/content/es/programming/cpp_fundamentals/" + LESSON
NEW_LABEL = "Construcción desde código y unidades que termina normalmente"


@lru_cache(maxsize=None)
def published(locale="es"):
    return json.loads(subprocess.check_output(
        ["git", "show", BASELINE + ":" + RELATIVE.replace("/es/", "/" + locale + "/")], cwd=contract.ROOT))


def family(spanish):
    return {locale: spanish if locale == "es" else published(locale)
            for locale in published()["stateLayout"]["family"]}


def revised():
    source = deepcopy(published())
    source["pages"][7]["blocks"][1]["responses"][0]["sources"][0]["label"] = NEW_LABEL
    return layout.prepare_v2(source, "es", family(published()), previous=published())


class MathLabelEditorialTest(unittest.TestCase):
    def setUp(self):
        self.old = deepcopy(published())
        self.new = revised()

    def preserved(self, old=None, new=None, relative=RELATIVE, page=7, block=1):
        return contract.preserves_previous_exercise(
            relative, page, block,
            self.old["pages"][7]["blocks"][1] if old is None else old,
            self.new["pages"][7]["blocks"][1] if new is None else new)

    def test_precise_revision_preserves_all_addresses_and_contracts(self):
        before = deepcopy(self.old)
        old_block = self.old["pages"][7]["blocks"][1]
        block = self.new["pages"][7]["blocks"][1]
        self.assertEqual(layout.MATH_LABEL_BEFORE, layout.fingerprint(old_block))
        self.assertEqual(layout.MATH_LABEL_AFTER, layout.fingerprint(block))
        self.assertEqual(layout.state_fingerprint(old_block), layout.state_fingerprint(block))
        self.assertEqual(old_block, layout.historical_math_payload(block))
        self.assertEqual(NEW_LABEL, block["responses"][0]["sources"][0]["label"])
        self.assertEqual(self.old, before)
        self.assertTrue(self.preserved())
        self.assertEqual(layout.validate(self.old), layout.validate(self.new))
        for field in ("origin", "current", "family", "additions"):
            self.assertEqual(self.old["stateLayout"][field], self.new["stateLayout"][field])
        self.assertEqual(self.new, layout.prepare_v2(self.new, "es", family(self.new)))

    def test_exact_editorial_proof_is_append_only_and_required(self):
        self.assertEqual(self.old["stateLayout"]["editorial"] + [{
            "id": layout.MATH_LABEL_IDENTITY,
            "beforeSha256": layout.MATH_LABEL_BEFORE,
            "afterSha256": layout.MATH_LABEL_AFTER,
        }], self.new["stateLayout"]["editorial"])
        for change in ("remove", "before", "after", "id"):
            with self.subTest(change=change):
                bad = deepcopy(self.new)
                if change == "remove":
                    bad["stateLayout"]["editorial"].pop()
                else:
                    key = {"before": "beforeSha256", "after": "afterSha256", "id": "id"}[change]
                    bad["stateLayout"]["editorial"][-1][key] = "0" * 64 if change != "id" else "block_0007_0000"
                with self.assertRaises(layout.LayoutError):
                    layout.validate(bad)

    def test_same_words_do_not_authorize_other_exercise_location_or_identity(self):
        old_block = self.old["pages"][7]["blocks"][1]
        new_block = self.new["pages"][7]["blocks"][1]
        self.assertFalse(self.preserved(page=8))
        self.assertFalse(self.preserved(block=2))
        self.assertFalse(self.preserved(relative=RELATIVE.replace("modelo_de_pedidos", "otro_modelo")))
        for identity in ("other", new_block["id"] + "_other"):
            self.assertFalse(self.preserved(dict(old_block, id=identity), dict(new_block, id=identity)))
        bad = deepcopy(self.new)
        bad["id"] += "_other"
        with self.assertRaises(layout.LayoutError):
            layout.validate(bad)

    def test_unmapped_and_v1_cannot_bypass_the_identity_and_proof_guards(self):
        for other_lesson in (False, True):
            for version in (None, 1):
                with self.subTest(other_lesson=other_lesson, version=version):
                    bad = deepcopy(self.new)
                    if other_lesson:
                        bad["id"] = "unrelated_lesson"
                    if version is None:
                        del bad["stateLayout"]
                    else:
                        bad["stateLayout"] = {"version": 1, "origin": self.old["stateLayout"]["origin"],
                                              "current": self.old["stateLayout"]["current"]}
                    with self.assertRaisesRegex(layout.LayoutError, "published v2"):
                        layout.validate(bad)

    def test_all_other_matching_fields_remain_protected(self):
        for change in ("pair", "target", "source", "response", "label", "feedback", "validator", "extra"):
            with self.subTest(change=change):
                bad = deepcopy(self.new)
                block = bad["pages"][7]["blocks"][1]
                response = block["responses"][0]
                if change == "pair":
                    response["validator"]["expected"]["construccion"] = "valor_completo"
                elif change == "target":
                    response["targets"][0]["id"] += "_other"
                elif change == "source":
                    response["sources"][0]["id"] += "_other"
                elif change == "response":
                    response["id"] += "_other"
                elif change == "label":
                    response["sources"][0]["label"] += "."
                elif change == "feedback":
                    block["feedback"]["correct"] += " changed"
                elif change == "validator":
                    response["validator"]["type"] = "other"
                else:
                    block["newField"] = "unauthorized"
                self.assertFalse(self.preserved(new=block))
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(self.old, bad)

    def test_old_payload_and_original_pairs_cannot_be_changed(self):
        old_block = deepcopy(self.old["pages"][7]["blocks"][1])
        old_block["responses"][0]["sources"][0]["label"] += "."
        self.assertFalse(self.preserved(old=old_block))
        self.assertEqual("objeto_valido", self.new["pages"][7]["blocks"][1]["responses"][0]["validator"]["expected"]["construccion"])

    def test_json_key_order_keeps_android_fingerprint_protected(self):
        bad = deepcopy(self.new)
        block = bad["pages"][7]["blocks"][1]
        label = block["responses"][0]["sources"][0]
        label["id"] = label.pop("id")
        # Canonical layout fingerprints sort keys; Android response fingerprints
        # deliberately do not. Transition validation must protect both contracts.
        self.assertEqual(layout.MATH_LABEL_AFTER, layout.fingerprint(block))
        with self.assertRaisesRegex(layout.LayoutError, "JSON key order"):
            layout.validate_transition(self.old, bad)

    def test_reopen_and_move_keep_bookmark_and_original_storage_slot(self):
        reopened = json.loads(json.dumps(self.new, ensure_ascii=False))
        layout.validate_transition(self.old, reopened)
        self.assertEqual((7, 1), layout.validate(reopened)[7, 1])
        plan = deepcopy(reopened["stateLayout"]["current"])
        plan[7]["blocks"].reverse()
        moved = layout.reorder(reopened, plan)
        position = next(position for position, origin in layout.validate(moved).items() if origin == (7, 1))
        self.assertEqual(layout.MATH_LABEL_IDENTITY, moved["stateLayout"]["current"][position[0]]["blocks"][position[1]])
        layout.validate_transition(self.old, moved)

    def test_translated_family_and_immutable_origin_are_not_regenerated(self):
        for field in ("origin", "family"):
            with self.subTest(field=field):
                bad = deepcopy(self.new)
                if field == "origin":
                    bad["stateLayout"]["origin"][7]["blocks"][1]["sha256"] = layout.MATH_LABEL_AFTER
                else:
                    bad["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(self.old, bad)


if __name__ == "__main__":
    unittest.main()

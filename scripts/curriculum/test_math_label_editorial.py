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


U11_BASELINE = "5cfd5514c879341216c1c34c313e34a16f60ea32"
U11_LESSON = "texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json"
U11_RELATIVE = "app/src/main/assets/content/es/programming/cpp_fundamentals/" + U11_LESSON
U11_OWNER = ("cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_interprete_con_historial_y_pruebas", "block_0008_0002")


@lru_cache(maxsize=None)
def u11_published(locale="es"):
    return json.loads(subprocess.check_output(["git", "show", U11_BASELINE + ":" +
        U11_RELATIVE.replace("/es/", "/" + locale + "/")], cwd=contract.ROOT))


class U11DomainLabelEditorialTest(unittest.TestCase):
    def setUp(self):
        self.old = deepcopy(u11_published())
        self.new = deepcopy(self.old)
        self.new["pages"][8]["blocks"][2]["responses"][0]["sources"][5]["label"] = "99+2;"
        self.family = {locale: deepcopy(u11_published(locale)) for locale in self.old["stateLayout"]["family"]}
        self.new = layout.prepare_v2(self.new, "es", self.family, self.old)

    def preserved(self, old=None, new=None, relative=U11_RELATIVE, page=8, block=2):
        return contract.preserves_previous_exercise(relative, page, block,
            self.old["pages"][8]["blocks"][2] if old is None else old,
            self.new["pages"][8]["blocks"][2] if new is None else new)

    def test_exact_original_and_final_blocks_preserve_the_complete_contract(self):
        spec = layout.MATH_LABEL_REVISIONS[U11_OWNER]
        old, new = self.old["pages"][8]["blocks"][2], self.new["pages"][8]["blocks"][2]
        self.assertEqual("fed5b6d2dcfb3321715725fe61a366c8c1f337d1b24a7e3e0b1fd43ef1be0549", layout.fingerprint(old))
        self.assertEqual("d2ec5c530dba31ba0c4d2b3575d609bfbd688fca0dbb4d94a0fd1524a9803187", layout.fingerprint(new))
        self.assertEqual(old, layout.historical_math_payload(new))
        self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
        self.assertTrue(self.preserved())
        self.assertEqual(self.old["stateLayout"]["editorial"] + [{"id": U11_OWNER[1],
            "beforeSha256": spec["before"], "afterSha256": spec["after"]}], self.new["stateLayout"]["editorial"])
        for field in ("origin", "current", "family", "additions"):
            self.assertEqual(self.old["stateLayout"][field], self.new["stateLayout"][field])
        self.assertEqual((8, 2), layout.validate(self.new)[8, 2])
        self.assertEqual(self.new, layout.prepare_v2(self.new, "es", dict(self.family, es=self.new)))
        layout.check_localized_family([self.new, self.family["en"], self.family["fr"]], ["es", "en", "fr"])
        self.assertEqual("limite", new["responses"][0]["validator"]["expected"]["dominio"])
        self.assertEqual(6, len(new["responses"][0]["sources"]))

    def test_whole_lesson_preservation_loop_uses_only_the_exact_label_exception(self):
        aligned = layout.align_for_previous_validation(self.old, self.new)
        positions = layout.validate(self.old)
        for page, record in enumerate(self.old["pages"]):
            for block, old in enumerate(record["blocks"]):
                current = aligned["pages"][page]["blocks"][block]
                pn, bn = positions[page, block]
                if old["type"] == "cpp_compiler":
                    self.assertTrue(contract.preserves_previous_compiler(U11_RELATIVE, pn, bn, old, current,
                        layout.original_projection(self.old)))
                elif old["type"] in contract.EXERCISES | {"code"}:
                    self.assertTrue(contract.preserves_previous_exercise(U11_RELATIVE, pn, bn, old, current))

    def test_owner_coordinates_language_and_complete_payload_are_closed(self):
        old, new = self.old["pages"][8]["blocks"][2], self.new["pages"][8]["blocks"][2]
        for page, block in ((7, 2), (8, 1), (8, 3)):
            self.assertFalse(self.preserved(page=page, block=block))
        self.assertFalse(self.preserved(relative=U11_RELATIVE.replace("historial", "otro_historial")))
        for field in ("id", "prompt", "feedback", "responses", "extra"):
            changed = deepcopy(new)
            if field == "responses": changed[field][0]["validator"]["expected"]["dominio"] = "completa"
            elif field == "feedback": changed[field]["correct"] += " Otra guía."
            else: changed[field] = "NO AUTORIZADO"
            self.assertFalse(self.preserved(new=changed))
            bad = deepcopy(self.new);bad["pages"][8]["blocks"][2] = changed
            with self.assertRaises(layout.LayoutError): layout.validate_transition(self.old, bad)
        changed = deepcopy(old);changed["responses"][0]["sources"][5]["label"] = "100+2;"
        self.assertFalse(self.preserved(old=changed))
        for change in ("lesson", "locale", "identity", "unmapped", "v1", "proof"):
            bad = deepcopy(self.new)
            if change == "lesson": bad["id"] += "_other"
            elif change == "locale": bad["stateLayout"]["locale"] = "en"
            elif change == "identity":
                bad["stateLayout"]["current"][8]["blocks"][1:3] = reversed(bad["stateLayout"]["current"][8]["blocks"][1:3])
            elif change == "unmapped": del bad["stateLayout"]
            elif change == "v1": bad["stateLayout"] = {"version": 1, "origin": self.old["stateLayout"]["origin"], "current": self.old["stateLayout"]["current"]}
            else: bad["stateLayout"]["editorial"].pop()
            with self.subTest(change=change), self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_serialized_order_and_origin_cannot_be_regenerated(self):
        bad = deepcopy(self.new)
        source = bad["pages"][8]["blocks"][2]["responses"][0]["sources"][5]
        source["id"] = source.pop("id")
        with self.assertRaisesRegex(layout.LayoutError, "JSON key order"): layout.validate_transition(self.old, bad)
        for change in ("origin", "family"):
            bad = deepcopy(self.new)
            if change == "origin": bad["stateLayout"]["origin"][8]["blocks"][2]["sha256"] = "0" * 64
            else: bad["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
            with self.assertRaises(layout.LayoutError): layout.validate_transition(self.old, bad)

    def test_reopen_and_reorder_keep_the_original_matching_storage_slot(self):
        reopened = json.loads(json.dumps(self.new, ensure_ascii=False))
        layout.validate_transition(self.old, reopened)
        plan = deepcopy(reopened["stateLayout"]["current"]);plan[8]["blocks"].reverse()
        moved = layout.reorder(reopened, plan)
        at = next(at for at, original in layout.validate(moved).items() if original == (8, 2))
        self.assertEqual(U11_OWNER[1], moved["stateLayout"]["current"][at[0]]["blocks"][at[1]])
        layout.validate_transition(self.old, moved)


if __name__ == "__main__":
    unittest.main()

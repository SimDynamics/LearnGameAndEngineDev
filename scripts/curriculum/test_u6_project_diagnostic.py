"""Twelve source-bound U6 project diagnostics adopt goals without rewriting history."""

from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "e8389c86c3cd3b572cb77c6308103163a1e2cf7d"
PREFIX = "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/"
SPECS = layout.PROJECT_COMPILE_DIAGNOSTICS
PINS = {
    "declaraciones_y_firmas": {
        "es": "b5c659ef75033d6fbed9ce5bc42a640b89ce3d33a8f063aaf32292d2ea9a38b7",
        "en": "8976a37af1ffd5359baf96e0ab4606b3d0b81c97b5d3d998a458df3ff348d716",
        "fr": "c4397e0426ff28750c21ff4f805a3cbd7b238823958ba8b22643c6219dd2aa2d",
    },
    "interfaces_cabeceras_y_programas_con_varios_archivos": {
        "es": "d250c29f303f8523ad74099086489565dce69699978ad57850a333439e31f595",
        "en": "d88080e6e1e2c07ffe50b9704681198a11e4532a0065458aa53dc176d495eb75",
        "fr": "220cb1602a099bdb1c91f476aa2a5fc22d4d6db938c6e4c72792e1ae6cc02833",
    },
    "construir_un_proyecto_desde_su_interfaz": {
        "es": "64b9e31d061e8d4e04e093c608b5be41a3c55367b6591028592ac82661c2bc73",
        "en": "9ee7e0187f9a6b007449a84e6adab72142b4462d83852b58f9c8637c209fd82a",
        "fr": "8a919dd16fd59bbe23f4840b4664dee78fac0176d19affe3c8236cddc014e388",
    },
}


def relative(lesson):
    return PREFIX + "lesson_" + lesson.split("__")[-1] + ".json"


@lru_cache(maxsize=9)
def published(lesson, locale="es"):
    slug = lesson.split("__")[-1]
    raw = subprocess.check_output(["git", "show", BASELINE + ":" + relative(lesson).replace("/es/", f"/{locale}/")],
                                  cwd=contract.ROOT)
    if hashlib.sha256(raw).hexdigest() != PINS[slug][locale]:
        raise AssertionError("The exact immutable pre-adoption U6 family fixture changed")
    return json.loads(raw)


def block(document, identity):
    return layout._blocks(document)[identity]


def refresh_edge(document, identity):
    proof = next(item for item in reversed(document["stateLayout"]["editorial"]) if item["id"] == identity)
    proof["afterSha256"] = layout.fingerprint(block(document, identity))


@lru_cache(maxsize=3)
def proposed_lesson(lesson):
    before = published(lesson)
    candidate = deepcopy(before)
    for (owner, identity), spec in SPECS.items():
        if owner == lesson:
            if layout.fingerprint(block(candidate, identity)) != spec["before"] or "compileCheck" in block(candidate, identity):
                raise AssertionError("The reviewed U6 project must retain its whole exploratory block")
            block(candidate, identity)["compileCheck"] = {"diagnostic": spec["diagnostic"]}
    family = {locale: deepcopy(published(lesson, locale)) for locale in ("es", "en", "fr")}
    return layout.prepare_v2(candidate, "es", family, before)


class U6ProjectDiagnosticTest(unittest.TestCase):
    def test_all_twelve_are_closed_new_owners_and_do_not_change_the_previous_five(self):
        self.assertEqual(12, len(SPECS))
        self.assertEqual(17, len(layout.COMPILE_DIAGNOSTICS))
        self.assertEqual(17, len({spec["diagnostic"] for spec in layout.COMPILE_DIAGNOSTICS.values()}))
        for (lesson, identity), spec in SPECS.items():
            with self.subTest(lesson=lesson, identity=identity):
                before, after = published(lesson), proposed_lesson(lesson)
                original, current = block(before, identity), block(after, identity)
                self.assertEqual({"type", "initialCode", "projectFiles", "sourceFiles"}, set(original))
                self.assertEqual(spec["before"], layout.fingerprint(original))
                self.assertEqual(dict(original, compileCheck={"diagnostic": spec["diagnostic"]}), current)
                self.assertEqual(spec, layout.validate_compile_check_contract(current))
                self.assertEqual(layout.state_fingerprint(original), layout.state_fingerprint(current))
                single = deepcopy(current)
                single.pop("projectFiles")
                with self.assertRaises(layout.LayoutError): layout.state_fingerprint(single)
                single = deepcopy(current)
                single.pop("sourceFiles")
                with self.assertRaises(layout.LayoutError): layout.state_fingerprint(single)

    def test_batch_prepare_preserves_every_family_map_response_source_and_prior_editorial_edge(self):
        for lesson in {owner for owner, _ in SPECS}:
            with self.subTest(lesson=lesson):
                before, after = published(lesson), proposed_lesson(lesson)
                self.assertEqual(layout.validate(before), layout.validate(after))
                layout.validate_transition(before, after)
                layout.check_localized_family([after, published(lesson, "en"), published(lesson, "fr")], ["es", "en", "fr"])
                for field in ("origin", "family", "current", "additions", "retired", "retirementHistory", "orderRefinements"):
                    self.assertEqual(before["stateLayout"].get(field), after["stateLayout"].get(field), field)
                for field in ("referenceAfter", "activityAfter", "compilerPuzzlesRequired"):
                    self.assertEqual(before.get(field), after.get(field), field)
                restored = deepcopy(after)
                expected_edges = []
                for (owner, identity), spec in SPECS.items():
                    if owner != lesson: continue
                    expected_edges.append({"id": identity, "beforeSha256": spec["before"],
                                           "afterSha256": layout.fingerprint(block(after, identity))})
                    block(restored, identity).pop("compileCheck")
                old_count = len(before["stateLayout"]["editorial"])
                self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:old_count])
                self.assertCountEqual(expected_edges, after["stateLayout"]["editorial"][old_count:])
                restored["stateLayout"]["editorial"] = deepcopy(before["stateLayout"]["editorial"])
                self.assertEqual(before, restored)

    def test_curricular_schema_accepts_each_pinned_project_and_never_weakens_runtime_goals(self):
        for (lesson, identity), spec in SPECS.items():
            with self.subTest(identity=identity, diagnostic=spec["diagnostic"]):
                before, after = published(lesson), proposed_lesson(lesson)
                contract.ERRORS.clear()
                contract.validate_cpp_compiler(block(after, identity), "U6 project diagnostic")
                contract.validate_lesson_state_contract(after, "U6 project diagnostic")
                self.assertEqual([], contract.ERRORS)
                _, page, item = identity.split("_")
                self.assertTrue(contract.preserves_previous_compiler(relative(lesson), int(page), int(item),
                    block(before, identity), block(after, identity), before))
                self.assertFalse(contract.preserves_previous_compiler(relative(lesson), int(page), int(item),
                    block(after, identity), block(before, identity), after))

    def test_no_changed_file_path_source_selection_or_build_input_can_borrow_the_goal(self):
        for (lesson, identity), spec in SPECS.items():
            for mode in ("initial", "file-source", "file-path", "selected-source", "file-order", "source-order",
                         "added-file", "missing-file", "output", "input", "cmake", "wrong-diagnostic"):
                bad = deepcopy(proposed_lesson(lesson))
                target = block(bad, identity)
                if mode == "initial": target["initialCode"] += "\n// changed"
                if mode == "file-source": target["projectFiles"][0]["initialCode"] += "\n// changed"
                if mode == "file-path": target["projectFiles"][0]["path"] = "different.h"
                if mode == "selected-source": target["sourceFiles"] = ["other.cpp"]
                if mode == "file-order": target["projectFiles"].reverse()
                if mode == "source-order": target["sourceFiles"] = list(reversed(target["sourceFiles"])) + ["other.cpp"]
                if mode == "added-file": target["projectFiles"].append({"path": "other.cpp", "initialCode": "int extra;"})
                if mode == "missing-file": target["projectFiles"].pop()
                if mode == "output": target["outputCheck"] = {"cases": [{"standardInput": "", "outputs": [""]}]}
                if mode == "input": target["initialStandardInput"] = ""
                if mode == "cmake": target["cmake"] = {"target": "main"}
                if mode == "wrong-diagnostic": target["compileCheck"]["diagnostic"] = "explicit-assignment"
                refresh_edge(bad, identity)
                with self.subTest(identity=identity, mode=mode), self.assertRaises(layout.LayoutError):
                    layout.validate(bad)

    def test_owner_locale_version_identity_and_full_before_after_proof_are_required(self):
        for lesson, identity in SPECS:
            for mode in ("owner", "locale", "unmapped", "v1", "identity", "edge", "before", "after"):
                bad = deepcopy(proposed_lesson(lesson))
                if mode == "owner": bad["id"] += "_other"
                if mode == "locale": bad["stateLayout"]["locale"] = "fr"
                if mode == "unmapped": bad.pop("stateLayout")
                if mode == "v1":
                    bad["stateLayout"] = {key: value for key, value in bad["stateLayout"].items() if key in {"origin", "current"}}
                    bad["stateLayout"]["version"] = 1
                if mode == "identity":
                    ids = next(record["blocks"] for record in bad["stateLayout"]["current"] if identity in record["blocks"])
                    first = ids.index(identity)
                    ids[first], ids[first - 1] = ids[first - 1], ids[first]
                if mode == "edge": bad["stateLayout"]["editorial"] = [item for item in bad["stateLayout"]["editorial"] if item["id"] != identity]
                if mode in {"before", "after"}:
                    proof = next(item for item in reversed(bad["stateLayout"]["editorial"]) if item["id"] == identity)
                    proof[mode + "Sha256"] = "0" * 64
                with self.subTest(identity=identity, mode=mode), self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_published_goals_cannot_be_removed_replaced_or_lost_from_retirement_history(self):
        for lesson, identity in SPECS:
            current = proposed_lesson(lesson)
            with self.subTest(identity=identity), self.assertRaises(layout.LayoutError):
                layout.validate_transition(current, published(lesson))
            for mode in ("remove", "replace"):
                bad = deepcopy(current)
                if mode == "remove": block(bad, identity).pop("compileCheck")
                else: block(bad, identity)["compileCheck"]["diagnostic"] = "const-member-call"
                family = {"es": current, "en": published(lesson, "en"), "fr": published(lesson, "fr")}
                with self.subTest(identity=identity, mode=mode), self.assertRaises(layout.LayoutError):
                    layout.prepare_v2(bad, "es", family, current)
            retired = layout.retire(current, ["block_0000_0000"])
            layout.validate_transition(current, retired)
            bad = deepcopy(retired)
            block(bad, identity).pop("compileCheck")
            with self.assertRaises(layout.LayoutError): layout.validate(bad)


if __name__ == "__main__":
    unittest.main()

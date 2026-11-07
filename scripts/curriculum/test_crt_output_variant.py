"""One exact raw Windows output spelling preserves U12L20's published state."""

from copy import deepcopy
from functools import lru_cache
import json
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "b9e32b9bff2ecc6071173abe5418fe99132b6fa6"
RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/"
            "lesson_clasificar_caracteres_y_normalizar_una_copia.json")
SPEC = layout.CRT_OUTPUT_REVISION


@lru_cache(maxsize=3)
def published(locale="es"):
    return json.loads(subprocess.check_output(
        ["git", "show", BASELINE + ":" + RELATIVE.replace("/es/", f"/{locale}/")], cwd=contract.ROOT))


def editor(document):
    return layout._blocks(document)[SPEC["identity"]]


def proposal():
    before = deepcopy(published())
    after = deepcopy(before)
    outputs = editor(after)["outputCheck"]["cases"][0]["outputs"]
    if len(outputs) != 1 or layout.fingerprint(editor(before)) != SPEC["before"]:
        raise AssertionError("CRT QA requires its exact immutable pre-adoption editor")
    outputs.append(outputs[0].replace("\n", "\r\n"))
    family = {locale: deepcopy(published(locale)) for locale in ("es", "en", "fr")}
    return before, layout.prepare_v2(after, "es", family, before)


class CrtOutputVariantTest(unittest.TestCase):
    def test_editorial_guidance_can_evolve_without_unreviewing_the_crt_execution_goal(self):
        _, variant = proposal()
        current = variant
        for count, text in enumerate(("QA empty report guide", "QA revised guidance with N=1", "QA another editorial wording"), 1):
            candidate = deepcopy(current)
            case = editor(candidate)["outputCheck"]["cases"][0]
            case["explanation"] = "QA successful complete report explanation " + str(count)
            case["reviewFeedback"] = [{"outputs": [""], "text": text},
                                      {"outputs": ["partial"], "text": "QA partial report guidance " + str(count)}]
            revised = layout.prepare_v2(candidate, "es", {"es": current, "en": published("en"), "fr": published("fr")}, current)
            layout.validate_transition(current, revised)
            self.assertTrue(layout.reviewed_crt_output_variant(editor(revised)))
            self.assertEqual(layout.without_compiler_guidance(editor(variant)), layout.without_compiler_guidance(editor(revised)))
            self.assertEqual(layout.state_fingerprint(editor(variant)), layout.state_fingerprint(editor(revised)))
            for field in ("origin", "family", "current", "additions", "retired", "retirementHistory", "orderRefinements"):
                self.assertEqual(variant["stateLayout"].get(field), revised["stateLayout"].get(field), field)
            self.assertEqual(current["stateLayout"]["editorial"], revised["stateLayout"]["editorial"][:-1])
            self.assertEqual({"id": SPEC["identity"], "beforeSha256": layout.fingerprint(editor(current)),
                              "afterSha256": layout.fingerprint(editor(revised))}, revised["stateLayout"]["editorial"][-1])
            self.assertTrue(contract.preserves_previous_compiler(RELATIVE, 5, 4, editor(current), editor(revised), current))
            contract.ERRORS.clear()
            contract.validate_cpp_compiler(editor(revised), "CRT current arbitrary guidance")
            contract.validate_lesson_state_contract(revised, "CRT current arbitrary guidance")
            self.assertEqual([], contract.ERRORS)
            current = revised

    def test_editorial_guidance_never_authorizes_other_execution_or_provenance_changes(self):
        _, variant = proposal()
        for mode in ("source", "input", "first", "second", "extra", "case", "comparison", "stderr", "exit", "identity", "owner", "lost_edge"):
            candidate = deepcopy(variant)
            case = editor(candidate)["outputCheck"]["cases"][0]
            case["reviewFeedback"] = [{"outputs": [""], "text": "QA arbitrary guidance"}]
            if mode == "source": editor(candidate)["initialCode"] += "\n// altered"
            if mode == "input": case["standardInput"] = "new input"
            if mode == "first": case["outputs"][0] += " "
            if mode == "second": case["outputs"][1] += " "
            if mode == "extra": case["outputs"].append("new output")
            if mode == "case": editor(candidate)["outputCheck"]["cases"].append(deepcopy(case))
            if mode == "comparison": editor(candidate)["outputCheck"]["inputComparison"] = "tokens"
            if mode == "stderr": case["standardError"] = "nonBlank"
            if mode == "exit": case["exitCode"] = 1
            if mode == "identity":
                ids = candidate["stateLayout"]["current"][5]["blocks"]
                ids[3], ids[4] = ids[4], ids[3]
            if mode == "owner": candidate["id"] += "_other"
            if mode == "lost_edge": candidate["stateLayout"]["editorial"].pop()
            with self.subTest(mode=mode), self.assertRaises(layout.LayoutError):
                layout.prepare_v2(candidate, "es", {"es": variant, "en": published("en"), "fr": published("fr")}, variant)

    def test_guidance_still_uses_the_existing_supported_content_schema(self):
        _, variant = proposal()
        for review in ("not a list", [{"outputs": [""], "text": ""}], [{"outputs": [""], "text": "QA", "unknown": True}],
                       [{"outputs": editor(variant)["outputCheck"]["cases"][0]["outputs"], "text": "QA"}],
                       [{"outputs": ["", ""], "text": "QA"}]):
            block = deepcopy(editor(variant));block["outputCheck"]["cases"][0]["reviewFeedback"] = review
            contract.ERRORS.clear();contract.validate_cpp_compiler(block, "invalid CRT guidance")
            self.assertTrue(contract.ERRORS)

    def test_complete_reviewed_pair_has_exact_raw_controls_and_no_other_change(self):
        before, after = proposal()
        old, current = editor(before), editor(after)
        self.assertEqual(SPEC["before"], layout.fingerprint(old))
        self.assertEqual(SPEC["after"], layout.fingerprint(current))
        first, second = current["outputCheck"]["cases"][0]["outputs"]
        self.assertEqual((802, 42, 3), (len(first.encode()), first.count("\n"), first.count("\r")))
        self.assertEqual((844, 42, 45), (len(second.encode()), second.count("\n"), second.count("\r")))
        self.assertEqual(second, first.replace("\n", "\r\n"))
        self.assertEqual(old, layout.historical_crt_output_payload(current))
        self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(current))

    def test_adoption_preserves_whole_family_layout_sources_answers_and_previous_proofs(self):
        before, after = proposal()
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        layout.check_localized_family([after, published("en"), published("fr")], ["es", "en", "fr"])
        for field in ("origin", "family", "current", "additions", "retired", "retirementHistory", "orderRefinements"):
            self.assertEqual(before["stateLayout"].get(field), after["stateLayout"].get(field), field)
        self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:-1])
        self.assertEqual({"id": SPEC["identity"], "beforeSha256": SPEC["before"], "afterSha256": SPEC["after"]},
                         after["stateLayout"]["editorial"][-1])
        restored = deepcopy(after)
        restored["stateLayout"]["editorial"].pop()
        editor(restored)["outputCheck"]["cases"][0]["outputs"].pop()
        self.assertEqual(before, restored)

    def test_curricular_validator_accepts_only_the_exact_authorized_owner_and_pair(self):
        before, after = proposal()
        old, current = editor(before), editor(after)
        self.assertTrue(contract.preserves_previous_compiler(RELATIVE, 5, 4, old, current, before))
        contract.ERRORS.clear()
        contract.validate_cpp_compiler(current, "exact CRT adoption")
        contract.validate_lesson_state_contract(after, "exact CRT adoption")
        self.assertEqual([], contract.ERRORS)
        for relative, page, block in ((RELATIVE, 5, 3), (RELATIVE, 4, 4),
                                      (RELATIVE.replace("/es/", "/fr/"), 5, 4),
                                      (RELATIVE.replace("clasificar_caracteres", "other"), 5, 4)):
            with self.subTest(relative=relative, page=page, block=block):
                self.assertFalse(contract.preserves_previous_compiler(relative, page, block, old, current, before))
        changed_owner = deepcopy(before)
        changed_owner["id"] += "_other"
        self.assertFalse(contract.preserves_previous_compiler(RELATIVE, 5, 4, old, current, changed_owner))

    def test_only_the_two_complete_raw_outputs_are_aliased(self):
        before, good = proposal()
        for mode in ("source", "input", "first", "second", "controls", "order", "extra", "exit", "case", "comparison"):
            bad = deepcopy(good)
            block = editor(bad)
            case = block["outputCheck"]["cases"][0]
            if mode == "source": block["initialCode"] += "\n// changed"
            if mode == "input": case["standardInput"] = "different"
            if mode == "first": case["outputs"][0] += " "
            if mode == "second": case["outputs"][1] += " "
            if mode == "controls": case["outputs"][1] = case["outputs"][0].replace("\r", "").replace("\n", "\r\n")
            if mode == "order": case["outputs"].reverse()
            if mode == "extra": case["outputs"].append("other")
            if mode == "exit": case["exitCode"] = 1
            if mode == "case": block["outputCheck"]["cases"].append(deepcopy(case))
            if mode == "comparison": block["outputCheck"]["inputComparison"] = "tokens"
            # Give the altered candidate its honest digest: rejection must not
            # depend merely on a stale afterSha256 string.
            bad["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(block)
            with self.subTest(mode=mode), self.assertRaises(layout.LayoutError):
                layout.validate(bad)
            self.assertFalse(contract.preserves_previous_compiler(RELATIVE, 5, 4, editor(before), block, before))

    def test_owner_language_map_and_exact_edge_are_required(self):
        _, good = proposal()
        for mode in ("owner", "locale", "identity", "edge", "wrong_before", "unmapped", "v1"):
            bad = deepcopy(good)
            if mode == "owner": bad["id"] += "_other"
            if mode == "locale": bad["stateLayout"]["locale"] = "fr"
            if mode == "identity":
                ids = bad["stateLayout"]["current"][5]["blocks"]
                ids[3], ids[4] = ids[4], ids[3]
            if mode == "edge": bad["stateLayout"]["editorial"].pop()
            if mode == "wrong_before": bad["stateLayout"]["editorial"][-1]["beforeSha256"] = "0" * 64
            if mode == "unmapped": bad.pop("stateLayout")
            if mode == "v1":
                bad["stateLayout"] = {key: value for key, value in bad["stateLayout"].items() if key in {"origin", "current"}}
                bad["stateLayout"]["version"] = 1
            with self.subTest(mode=mode), self.assertRaises(layout.LayoutError):
                layout.validate(bad)

    def test_unrelated_blocks_and_generic_output_alternatives_keep_their_exact_contracts(self):
        block = {"type": "cpp_compiler", "initialCode": "int main(){}",
                 "outputCheck": {"cases": [{"standardInput": "", "outputs": ["A\rB\n", "A\rB\r\n"]}]}}
        original = deepcopy(block)
        self.assertFalse(layout.reviewed_crt_output_variant(block))
        self.assertEqual(block, layout.historical_crt_output_payload(block))
        revised = deepcopy(block)
        revised["outputCheck"]["cases"][0]["outputs"].append("other")
        self.assertNotEqual(layout.state_fingerprint(block), layout.state_fingerprint(revised))
        self.assertFalse(contract.preserves_previous_compiler("other.json", 5, 4, block, revised, {}))
        self.assertEqual(original, block)

    def test_a_published_reviewed_variant_cannot_be_removed_by_reopening_or_transition(self):
        before, after = proposal()
        self.assertEqual(after, layout.prepare_v2(deepcopy(after), "es",
            {"es": after, "en": deepcopy(published("en")), "fr": deepcopy(published("fr"))}, after))
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(after, before)
        self.assertFalse(contract.preserves_previous_compiler(RELATIVE, 5, 4, editor(after), editor(before), after))


if __name__ == "__main__":
    unittest.main()

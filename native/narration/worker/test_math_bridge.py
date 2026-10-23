"""Windows bridge regressions against the unchanged, real mathematical curriculum."""
from __future__ import annotations

import copy
import hashlib
from fractions import Fraction
import json
import math
from pathlib import Path
import unittest

from test_worker import ROOT, Worker

CONTENT = ROOT / "app/src/main/assets/content/es/math_physics"


def blocks():
    for path in sorted(CONTENT.rglob("lesson*.json")):
        lesson = json.loads(path.read_text(encoding="utf-8-sig"))
        for page in lesson.get("pages", []):
            for block in page.get("blocks", []):
                if block.get("type") == "math_puzzle" or block.get("type", "").startswith("math_"):
                    yield path, block


def responses(block):
    return block.get("responses", [dict(block, id="response")])


def known_answer(spec):
    """Only test inputs: the bridge must never supply these answers to its UI."""
    kind = spec["type"]
    if kind == "choice":
        return {"type": "choice", "selected": spec["expected"]}
    if kind == "fields":
        return {"type": "fields", "values": {key: str(value["expected"])
                for key, value in spec["fields"].items()}}
    if kind == "order":
        return {"type": "order", "ids": spec["expected"]}
    if kind == "matching":
        return {"type": "matching", "pairs": spec["expected"]}
    if kind == "number_line":
        return {"type": "number_line", "value": float(Fraction(str(spec.get("value", spec.get("expected"))))),
                "closed": spec.get("closed", False), "direction": spec.get("direction", "none")}
    if kind == "sliders":
        return {"type": "sliders", "values": {key: float(Fraction(str(value["expected"])))
                for key, value in spec["values"].items()}}
    if kind == "coordinates":
        return {"type": "graph", "points": spec["expected"]}
    if kind == "geometry":
        area = float(Fraction(str(spec["expected"])))
        points = [{"x": 0, "y": 0}, {"x": area, "y": 0}, {"x": area, "y": 1}, {"x": 0, "y": 1}]
        return {"type": "graph", "points": points}
    if kind == "rigid_angle_copy":
        raise AssertionError("This fixture needs its explicit geometric placement")
    return {"type": "text", "value": str(spec["expected"])}


class WindowsMathBridgeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker()
        cls.blocks = list(blocks())
        cls.sandbox = [block for path, block in cls.blocks if path.name == "lesson_math_puzzle_sandbox.json"]

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def evaluate(self, block, answers=None, language="es"):
        if answers is None:
            answers = {response["id"]: known_answer(response["validator"]) for response in responses(block)}
        return self.worker.ask("validate_math", block=block, responses=answers, language=language)

    def test_01_describes_all_real_math_blocks_without_exposing_solutions(self):
        forbidden = {"validator", "expected", "correctFeedback", "incorrectFeedback", "reviewFeedback", "solution"}
        def inspect(value):
            if isinstance(value, dict):
                self.assertFalse(forbidden.intersection(value))
                for child in value.values():
                    inspect(child)
            elif isinstance(value, list):
                for child in value:
                    inspect(child)
        self.assertGreater(len(self.blocks), 100)
        for path, block in self.blocks:
            with self.subTest(lesson=path.name, block=block.get("id", block.get("prompt"))):
                result = self.worker.ask("describe_math", block=block, language="es")
                self.assertEqual(block.get("prompt", block.get("question")), result["prompt"])
                self.assertEqual(len(responses(block)), len(result["responses"]))
                inspect(result)

    def test_02_original_evaluators_accept_real_curricular_answers(self):
        for path, block in self.blocks:
            with self.subTest(lesson=path.name, block=block.get("id", block.get("prompt"))):
                result = self.evaluate(block)
                self.assertTrue(result["evaluable"], result)
                self.assertTrue(result["correct"], result)
                self.assertEqual("correct", result["feedbackKind"])

    def test_03_arithmetic_wrong_missing_and_localized_feedback(self):
        block = self.sandbox[0]
        correct = {item["id"]: known_answer(item["validator"]) for item in block["responses"]}
        for language, verdict, message in (("es", "✓ Correcto", "¡Correcto!"), ("fr", "✓ Correct", "Correct !"),
                                          ("en-US", "✓ Correct", "Correct!"), ("en-GB", "✓ Correct", "Correct!")):
            result = self.evaluate(block, correct, language)
            self.assertTrue(result["feedback"].startswith(verdict+"\n"))
            self.assertEqual(message,result["results"][0]["message"])
            self.assertEqual("math_ui_correct", result["results"][0]["code"])
        wrong = copy.deepcopy(correct)
        wrong["difference"]["value"] = "8"
        rejected = self.evaluate(block, wrong)
        self.assertTrue(rejected["evaluable"])
        self.assertFalse(rejected["correct"])
        self.assertEqual("incorrect", rejected["feedbackKind"])
        incomplete = self.evaluate(block, {})
        self.assertFalse(incomplete["evaluable"])
        self.assertEqual("error", incomplete["feedbackKind"])

    def test_04_geometry_retains_original_area_and_shape_validation(self):
        block = next(block for block in self.sandbox if any(
            item["type"] == "math_geometry_builder" for item in block["responses"]))
        self.assertTrue(self.evaluate(block)["correct"])
        response = block["responses"][0]
        wrong = {response["id"]: {"type": "graph", "points": [
            {"x": 0, "y": 0}, {"x": 3, "y": 0}, {"x": 3, "y": 3}, {"x": 0, "y": 3}]}}
        result = self.evaluate(block, wrong)
        self.assertTrue(result["evaluable"])
        self.assertFalse(result["correct"])

    def test_graph_coordinate_drafts_block_stale_points_and_recover(self):
        block = next(block for block in self.sandbox if block.get("id") == "point_and_coordinate")
        original = {response["id"]: known_answer(response["validator"])
                    for response in block["responses"]}
        self.assertTrue(self.evaluate(block, original)["correct"])
        for language in ("es", "en-US", "en-GB", "fr"):
            for partial in ("", "-", ".", "1e", "NaN", "Infinity"):
                with self.subTest(language=language, partial=partial):
                    answers = copy.deepcopy(original)
                    answers["point"]["interactionDraft"] = {"coords": [{"x": partial, "y": "3"}]}
                    before = copy.deepcopy(answers)
                    result = self.evaluate(block, answers, language)
                    self.assertFalse(result["evaluable"], result)
                    self.assertFalse(result["correct"])
                    self.assertEqual("error", result["feedbackKind"])
                    self.assertEqual("math_ui_valid_coordinates", result["results"][0]["code"])
                    self.assertEqual(before, answers)
            recovered = copy.deepcopy(original)
            recovered["point"]["interactionDraft"] = {"coords": [{"x": " 2,0 ", "y": "3e0"}]}
            result = self.evaluate(block, recovered, language)
            self.assertTrue(result["evaluable"], result)
            self.assertTrue(result["correct"], result)
        self.assertTrue(self.evaluate(block, original)["correct"])

    def test_05_algebra_domain_and_unsupported_expressions_are_not_false_successes(self):
        block = next(block for _, block in self.blocks if any(
            item.get("validator", {}).get("type") == "parametric_rational_expression"
            for item in responses(block)))
        response = responses(block)[0]
        expected = response["validator"]["expected"]
        self.assertTrue(self.evaluate(block)["correct"])
        wrong = {response["id"]: {"type": "text", "value": "0"}}
        result = self.evaluate(block, wrong)
        self.assertTrue(result["evaluable"])
        self.assertFalse(result["correct"])
        extra_domain = {response["id"]: {"type": "text", "value": f"({expected})*a/a"}}
        result = self.evaluate(block, extra_domain)
        self.assertTrue(result["evaluable"])
        self.assertFalse(result["correct"])
        self.assertEqual("math_ui_parametric_domain_mismatch", result["results"][0]["code"])
        expression = next(block for block in self.sandbox if any(
            item.get("validator", {}).get("type") == "equivalent_expression" for item in block["responses"]))
        unsupported = self.evaluate(expression, {expression["responses"][0]["id"]: {"type": "text", "value": "sin(x)"}})
        self.assertFalse(unsupported["evaluable"])
        self.assertFalse(unsupported["correct"])
        self.assertEqual("math_ui_unsupported_expression", unsupported["results"][0]["code"])

    def test_06_incorrect_choice_feedback_does_not_reveal_absent_options(self):
        block = next(block for block in self.sandbox if any(
            item["type"] == "math_multiple_choice_multi" for item in block["responses"]))
        response = block["responses"][0]
        selected = response["validator"]["expected"][:1]
        result = self.evaluate(block, {response["id"]: {"type": "choice", "selected": selected}})
        self.assertTrue(result["evaluable"])
        self.assertFalse(result["correct"])
        self.assertNotIn(block["feedback"]["correct"], result["feedback"])
        for option in response["options"]:
            if option["id"] not in selected and option.get("reviewFeedback"):
                self.assertNotIn(option["reviewFeedback"], result["feedback"])

    def test_07_original_raw_json_transport_does_not_round_large_integers(self):
        block = '{"type":"math_numeric_input","prompt":"Entero exacto.","initial":9007199254740993,"validator":{"type":"numeric","expected":9007199254740993}}'
        description = self.worker.ask("describe_math",blockJson=block,language="es")
        self.assertEqual(9007199254740993,description["responses"][0]["config"]["initial"])
        for value, expected in (("9007199254740993",True),("9007199254740992",False)):
            result = self.worker.ask("validate_math",blockJson=block,
                responsesJson=json.dumps({"response":{"type":"text","value":value}}),language="es")
            self.assertTrue(result["evaluable"])
            self.assertEqual(expected,result["correct"])

    def test_08_android_fingerprint_keeps_source_order_escaping_and_review_compatibility(self):
        # Golden serialization follows the official Android SDK35 JSONStringer:
        # insertion order, slash escaping and non-ASCII text are significant.
        legacy = {"type":"math_puzzle","prompt":"¿6 / 2?\nElige.","responses":[{
            "id":"n","type":"math_multiple_choice_single","options":[
                {"id":"three","label":"Tres"},{"id":"four","label":"Cuatro"}],
            "validator":{"type":"choice","expected":["three"]}}]}
        canonical = json.dumps(legacy,ensure_ascii=False,separators=(",",":")).replace("/","\\/")
        expected = hashlib.sha256(canonical.encode()).hexdigest()
        answers = {"n":{"type":"choice","selected":["three"]}}
        original = self.worker.ask("validate_math",blockJson=json.dumps(legacy,ensure_ascii=False),responsesJson=json.dumps(answers),language="es")
        self.assertEqual(expected,original["contentFingerprint"])
        enhanced = copy.deepcopy(legacy)
        enhanced["responses"][0]["options"][0]["reviewFeedback"]="Revisa la división."
        current = self.worker.ask("validate_math",blockJson=json.dumps(enhanced,ensure_ascii=False),responsesJson=json.dumps(answers),language="es")
        self.assertEqual(expected,current["contentFingerprint"])
        reordered = {"prompt":legacy["prompt"],"type":legacy["type"],"responses":legacy["responses"]}
        changed = self.worker.ask("validate_math",blockJson=json.dumps(reordered,ensure_ascii=False),responsesJson=json.dumps(answers),language="es")
        self.assertNotEqual(expected,changed["contentFingerprint"],"A sorted-key replacement silently changed Android's historical hash contract")


if __name__ == "__main__":
    unittest.main(verbosity=2)

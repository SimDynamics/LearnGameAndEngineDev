"""Windows bridge regressions against the unchanged, real mathematical curriculum."""
from __future__ import annotations

import copy
import hashlib
from fractions import Fraction
import json
import math
from pathlib import Path
import subprocess
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

    def test_10_rigid_copy_reuses_original_placement_layout_and_validator(self):
        # Same technical geometry as Android RigidAngleCopyTest; no lesson added.
        def figure(vertex, first, second, x, y, angle, first_distance, second_distance):
            points = [{"id": vertex, "x": x, "y": y, "labels": [vertex]}]
            for name, direction, distance in ((first, angle, first_distance), (second, angle + 1, second_distance)):
                points.append({"id": name, "x": x + math.cos(direction) * distance,
                               "y": y + math.sin(direction) * distance, "labels": [name]})
            return {"vertex": vertex, "sides": ["first", "second"], "region": "counterclockwise",
                    "diagram": {"points": points, "objects": [
                        {"id": name, "kind": "ray", "through": [vertex, endpoint], "contains": [vertex, endpoint]}
                        for name, endpoint in (("first", first), ("second", second))]}}
        spec = {"type": "rigid_angle_copy", "reference": figure("O", "A", "B", 1, 2, 0, 4, 5),
                "copy": figure("V", "C", "D", 0, 0, .7, 6, 3), "initial": {"x": 7, "y": 6, "rotation": .2},
                "relation": "congruent", "positionTolerance": .1, "directionTolerance": .01}
        original = copy.deepcopy(spec)
        def draw(**fields):
            return self.worker.ask("math_editor", action="rigid_layout", config=spec,
                                   width=850, height=600, unit=1, textSize=15, **fields)
        initial = draw()
        self.assertEqual({"type": "rigid_angle", **spec["initial"]}, initial["placement"])
        self.assertEqual(6, len(initial["points"]))
        self.assertEqual(4, len(initial["arrows"]))
        self.assertEqual({"reference:", "copy:"}, {part["id"] for part in initial["regions"]})
        fixed = {part["id"]: part for part in initial["points"] if part["id"].startswith("reference:")}
        moved = draw(placement=initial["placement"], gesture={"mode": "move", "previous": {"x": 300, "y": 300}, "current": {"x": 320, "y": 310}})
        self.assertNotEqual(initial["placement"], moved["placement"])
        self.assertEqual(fixed, {part["id"]: part for part in moved["points"] if part["id"].startswith("reference:")})
        self.assertEqual(initial["placement"]["rotation"], moved["placement"]["rotation"])
        view = draw(placement=initial["placement"], gesture={"mode": "view", "previous": {"x": 300, "y": 300}, "current": {"x": 320, "y": 310}})
        self.assertEqual(initial["placement"], view["placement"])
        center = initial["copyCenter"]
        rotated = draw(placement=initial["placement"], gesture={"mode": "rotate",
            "previous": {"x": center["x"] + 80, "y": center["y"]},
            "current": {"x": center["x"] + 75, "y": center["y"] + 20}})
        self.assertEqual(initial["placement"]["x"], rotated["placement"]["x"])
        self.assertEqual(initial["placement"]["y"], rotated["placement"]["y"])
        self.assertNotEqual(initial["placement"]["rotation"], rotated["placement"]["rotation"])
        far = draw(placement=initial["placement"], gesture={"mode": "move", "previous": {"x": 0, "y": 0}, "current": {"x": 100000, "y": -100000}})
        self.assertTrue(all(0 <= point["center"]["x"] <= 850 and 0 <= point["center"]["y"] <= 600
                            for point in far["points"] if point["id"].startswith("copy:")))
        stored = {"x": 10000, "y": -10000, "rotation": 1.2}
        self.assertEqual({"type": "rigid_angle", **stored}, draw(placement=stored)["placement"])
        block = {"type": "math_puzzle", "prompt": "Traslada y gira la copia conservando su forma.", "responses": [{"id": "copy", "type": "math_rigid_angle_copy", "validator": spec}]}
        self.assertTrue(self.evaluate(block, {"copy": {"type": "rigid_angle", "x": 1, "y": 2, "rotation": -.7}})["correct"])
        self.assertFalse(self.evaluate(block, {"copy": initial["placement"]})["correct"])
        self.assertEqual(original, spec)

    def test_11_original_auto_typesetting_preserves_utf16_source_and_operators(self):
        def fragments(source, mode="auto"):
            result = self.worker.ask("math_editor", action="text_layout", text=source, mode=mode)
            encoded = source.encode("utf-16-le")
            previous = 0
            for part in result["fragments"]:
                self.assertTrue(previous <= part["start"] < part["end"] <= len(encoded)//2)
                part["original"] = encoded[part["start"]*2:part["end"]*2].decode("utf-16-le")
                previous = part["end"]
            return result["fragments"]

        source = "🙂 Punto (1,2) y fracción 3/4, con x²."
        result = fragments(source)
        self.assertEqual(["(1,2)", "3/4", "x²"], [part["original"] for part in result])
        self.assertEqual([r"(1\mathrm{,}2)", r"\frac{3}{4}", "x^{2}"], [part["formula"] for part in result])
        self.assertFalse(any(part["literal"] for part in result))
        self.assertEqual([], fragments(source, "text"))
        for expression in ("2 × 3 = 6", "4 − 1 = 3"):
            result = fragments(expression)
            self.assertEqual(expression, result[0]["original"])
            self.assertEqual(expression, result[0]["formula"], "AST serialization changed an original visible operator")
        explicit = fragments(r"Antes \(\frac{1}{2}\) después.")
        self.assertEqual(r"\(\frac{1}{2}\)", explicit[0]["original"])
        self.assertEqual(r"\frac{1}{2}", explicit[0]["formula"])
        self.assertEqual([], fragments(r"C++23; std::cout; C:/ruta/1/2; 2026/10/23; `1/2`."))
        fallback = fragments(r"Antes \(\notacommand{a}\) después.")
        self.assertTrue(fallback[0]["literal"])
        self.assertEqual(r"\notacommand{a}", fallback[0]["formula"])

    def test_12_current_curricular_fractions_keep_complete_utf16_units(self):
        def current(relative, identifier, feedback=False):
            path = CONTENT / relative
            original = path.read_bytes()
            lesson = json.loads(original.decode("utf-8-sig"))
            matching = [block for page in lesson["pages"] for block in page["blocks"]
                        if block.get("id") == identifier]
            self.assertEqual(1, len(matching))
            block = matching[0]
            source = block["feedback"]["correct"] if feedback else block["prompt"]
            return path, hashlib.sha256(original).digest(), source

        def inspect(source, expected):
            reply = self.worker.ask("math_editor", action="text_layout", text=source, mode="auto")
            encoded = source.encode("utf-16-le")
            previous = 0
            for fragment in reply["fragments"]:
                self.assertTrue(previous <= fragment["start"] < fragment["end"] <= len(encoded)//2)
                fragment["original"] = encoded[fragment["start"]*2:fragment["end"]*2].decode("utf-16-le")
                previous = fragment["end"]
            for spelling, formula in expected.items():
                with self.subTest(fraction=spelling):
                    byte = source.index(spelling)
                    first = len(source[:byte].encode("utf-16-le"))//2
                    last = first + len(spelling.encode("utf-16-le"))//2
                    owners = [fragment for fragment in reply["fragments"]
                              if fragment["start"] <= first and fragment["end"] >= last]
                    self.assertEqual(1, len(owners), (spelling, reply))
                    self.assertFalse(owners[0]["literal"])
                    self.assertIn(formula, owners[0]["formula"])
            return reply["fragments"]

        recovery = current(
            "math_algebra_elemental/recuperacion_y_transferencia/"
            "lesson_elegir_relaciones_y_recuperar_el_razonamiento_algebraico.json",
            "dos_exigencias_y_dos_dominios")
        rational = current(
            "math_algebra_elemental/sistemas_lineales/"
            "lesson_formulas_con_datos_y_una_condicion_no_nula.json",
            "resolver_con_datos_fraccionarios")
        # In the current asset the historic parametric slash expressions belong
        # to correct feedback, not to the revised construction prompt.
        parametric = current(
            "math_algebra_elemental/sistemas_lineales/"
            "lesson_formulas_con_datos_y_una_condicion_no_nula.json",
            "construir_el_criterio_con_datos", feedback=True)
        observed = inspect(recovery[2], {"5/2": r"\frac{5}{2}"})
        relation = next(part for part in observed if "5/2" in part["original"])
        self.assertEqual("2 ≥ 5/2", relation["original"])
        self.assertEqual(r"2 ≥ \frac{5}{2}", relation["formula"])
        inspect(rational[2], {
            "x/2": r"\frac{x}{2}", "2y/3": r"\frac{2y}{3}",
            "−3x/4": r"\frac{−3x}{4}", "y/5": r"\frac{y}{5}", "21/10": r"\frac{21}{10}",
        })
        inspect(parametric[2], {
            "(du − bv)/D": r"\frac{du − bv}{D}",
            "(av − cu)/D": r"\frac{av − cu}{D}",
        })
        # Supplementary characters before the same real source must shift all
        # offsets by two UTF-16 units, not by Python/Kotlin code point counts.
        shifted = inspect("🙂 " + recovery[2], {"5/2": r"\frac{5}{2}"})
        self.assertEqual([(part["start"]+3, part["end"]+3, part["formula"]) for part in observed],
                         [(part["start"], part["end"], part["formula"]) for part in shifted])
        for path, digest, _ in (recovery, rational, parametric):
            self.assertEqual(digest, hashlib.sha256(path.read_bytes()).digest())

    def test_13_windows_fraction_repair_preserves_protected_sources(self):
        excluded = (
            "https://example.test/x/2?y=1",
            "http://example.test/5/2",
            "C:/ruta/x/2 = 1", r"C:\ruta\x\2 = 1",
            "/tmp/x/2 = 1", "docs/x/2 = 1", "folder/(du-bv)/D = 1",
            "x/2", "x/2=1", "archivo x/2 y carpeta a/b",
            "x = a/2.txt", "x = a/2-archive", "x = a/2/child",
            "x = (av − cu)/D.txt", "x = (av − cu)/D-archive", "x = (av − cu)/D/child",
            "2026/10/28 = fecha", "28/10/2026",
            "C++23; std::cout; " + chr(96) + "x/2 + 2y/3 = 1" + chr(96) + ".",
            chr(96)*3 + "x/2 + 2y/3 = 1" + chr(96)*3,
        )
        for source in excluded:
            with self.subTest(source=source):
                result = self.worker.ask("math_editor", action="text_layout", text=source, mode="auto")
                self.assertEqual([], result["fragments"])
        for closing in (".", ",", ";", ")"):
            source = "El par queda forzado a y = (av − cu)/D" + closing + " Fin."
            with self.subTest(closing=closing):
                result = self.worker.ask("math_editor", action="text_layout", text=source, mode="auto")["fragments"]
                self.assertTrue(any(part["formula"] == r"\frac{av − cu}{D}" for part in result))
        for source, formula, literal in (
            (r"Antes \(\frac{x}{2}\) después.", r"\frac{x}{2}", False),
            (r"Antes \(\notacommand{x/2 = 1}\) después.", r"\notacommand{x/2 = 1}", True),
            (r"Antes \(x/2 = 1\) después.", "x/2 = 1", False),
        ):
            with self.subTest(explicit=source):
                result = self.worker.ask("math_editor", action="text_layout", text=source, mode="auto")["fragments"]
                self.assertEqual(1, len(result))
                self.assertEqual(formula, result[0]["formula"])
                self.assertEqual(literal, result[0]["literal"])
        for mode in ("text", "inline", "block"):
            result = self.worker.ask("math_editor", action="text_layout", text="x/2 + y/5 = 1", mode=mode)
            if mode == "text":
                self.assertEqual([], result["fragments"])
            else:
                self.assertEqual(1, len(result["fragments"]))
                self.assertEqual("x/2 + y/5 = 1", result["fragments"][0]["formula"])

    def test_14_fraction_repair_keeps_binary_minus_outside_and_unary_minus_inside(self):
        def displayed_formula(source):
            fragments = self.worker.ask("math_editor", action="text_layout", text=source, mode="auto")["fragments"]
            encoded = source.encode("utf-16-le")
            pieces = []
            previous = 0
            for fragment in fragments:
                self.assertTrue(previous <= fragment["start"] < fragment["end"] <= len(encoded)//2)
                pieces.append(encoded[previous*2:fragment["start"]*2].decode("utf-16-le"))
                pieces.append(fragment["formula"])
                previous = fragment["end"]
            pieces.append(encoded[previous*2:].decode("utf-16-le"))
            return "".join(pieces)

        for minus in ("−", "-"):
            for spacing in (" ", ""):
                source = "En 2" + spacing + minus + "3/4 = 5/4."
                with self.subTest(binary=source):
                    self.assertEqual(source.replace("3/4", r"\frac{3}{4}").replace("5/4", r"\frac{5}{4}"),
                                     displayed_formula(source))
            for prose in ("La fracción ", "igual a "):
                source = prose + minus + "3/4."
                with self.subTest(unary=source):
                    self.assertEqual(prose + r"\frac{" + minus + r"3}{4}.", displayed_formula(source))
        # A coefficient's existing unary sign in the real current rational
        # system remains part of its numerator (also exercised by test_12).
        source = "Resuelve x/2 + 2y/3 = 1 y −3x/4 + y/5 = 21/10."
        self.assertIn(r"\frac{−3x}{4}", displayed_formula(source))
        self.assertNotIn(r"−\frac{3x}{4}", displayed_formula(source))

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

    def test_08b_exact_pedido_label_revision_preserves_response_identity_only(self):
        # Freeze the published source, independently of Work's later integration.
        path = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                "datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json")
        lesson = json.loads(subprocess.check_output(
            ["git", "show", "cbd9a38c19ff1f02068296f7e6cb9049e2c64212:" + path], cwd=ROOT))
        legacy = lesson["pages"][7]["blocks"][1]
        self.assertEqual("construccion", legacy["responses"][0]["sources"][0]["id"])
        self.assertEqual("Construcción que termina normalmente", legacy["responses"][0]["sources"][0]["label"])
        revised = copy.deepcopy(legacy)
        revised["responses"][0]["sources"][0]["label"] = "Construcción desde código y unidades que termina normalmente"
        def serialized(block):
            return json.dumps(block, ensure_ascii=False, separators=(",", ":"))
        def golden(block):
            return hashlib.sha256(serialized(block).replace("/", "\\/").encode("utf-8")).hexdigest()
        old_hash = "978c6526c69d056658f4ee8455eaa4c37f5e87d97d9feebd5ecf93bac352ed13"
        self.assertEqual(old_hash, golden(legacy))
        self.assertEqual("5a55c3f981833aabcc7f047765a3db66d1f9e6993bf69a14d69e8ae08c190095", golden(revised))
        answers = {"contratos": known_answer(legacy["responses"][0]["validator"])}
        incorrect = copy.deepcopy(answers)
        pairs = incorrect["contratos"]["pairs"]
        pairs["construccion"], pairs["transicion"] = pairs["transicion"], pairs["construccion"]
        partial = copy.deepcopy(answers)
        del partial["contratos"]["pairs"]["construccion"]
        for submitted, correct in ((answers, True), (incorrect, False), (partial, False)):
            for block in (legacy, revised):
                before = copy.deepcopy(block)
                state = copy.deepcopy(submitted)
                result = self.worker.ask("validate_math", blockJson=serialized(block),
                                         responsesJson=serialized(submitted), language="es")
                self.assertEqual(correct, result["correct"])
                self.assertEqual(old_hash, result["contentFingerprint"])
                self.assertEqual(state, submitted, "Validation rewrote a saved matching selection")
                self.assertEqual(before, block, "Fingerprint compatibility rewrote the visible source")
        described = self.worker.ask("describe_math", blockJson=serialized(revised), language="es")
        self.assertEqual(revised["responses"][0]["sources"], described["responses"][0]["config"]["sources"])
        mutations = []
        changed = copy.deepcopy(revised)
        changed["responses"][0]["sources"][0]["label"] += " extra"
        mutations.append(changed)
        changed = copy.deepcopy(revised)
        changed["id"] += "_other"
        mutations.append(changed)
        changed = copy.deepcopy(revised)
        expected = changed["responses"][0]["validator"]["expected"]
        expected["construccion"], expected["transicion"] = expected["transicion"], expected["construccion"]
        mutations.append(changed)
        changed = copy.deepcopy(revised)
        changed["responses"][0]["targets"][0]["label"] += " extra"
        mutations.append(changed)
        changed = copy.deepcopy(revised)
        changed["feedback"]["incorrect"] += " extra"
        mutations.append(changed)
        # Android intentionally treats the complete source key order as historical.
        mutations.append({"id": revised["id"], **{key: value for key, value in revised.items() if key != "id"}})
        for changed in mutations:
            result = self.worker.ask("validate_math", blockJson=serialized(changed),
                                     responsesJson=serialized(answers), language="es")
            self.assertEqual(golden(changed), result["contentFingerprint"])
            self.assertNotEqual(old_hash, result["contentFingerprint"],
                                "The approved label licensed an unrelated content/answer/order change")

    def test_09_all_authored_euclidean_drawings_use_the_original_layout(self):
        def representations(value):
            if isinstance(value, dict):
                if value.get("type") == "euclidean_diagram":
                    yield value.get("config", value)
                for key, child in value.items():
                    if key not in {"validator", "feedback", "reviewFeedback"}:
                        yield from representations(child)
            elif isinstance(value, list):
                for child in value:
                    yield from representations(child)

        configs = [config for _, block in self.blocks for config in representations(block)]
        self.assertGreaterEqual(len(configs), 37, "The complete authored Euclidean inventory lost known drawings")
        for index, config in enumerate(configs):
            original = copy.deepcopy(config)
            for width, height in ((400, 286), (850, 607)):
                for scale in (.75, 1., 1.5):
                    with self.subTest(diagram=index, width=width, scale=scale):
                        drawing = self.worker.ask("math_editor", action="diagram_layout", config=config,
                                                  width=width, height=height, textSize=15*scale)
                        self.assertEqual({p["id"] for p in config["points"]}, {p["id"] for p in drawing["points"]})
                        self.assertEqual({p["id"] for p in config.get("arcs", [])}, {p["id"] for p in drawing["arcs"]})
                        self.assertEqual({p["id"]: p.get("filled", False) for p in config.get("circles", [])},
                                         {p["id"]: p["filled"] for p in drawing["circles"]})
                        labels = [label for p in config["points"] for label in p.get("labels", [])]
                        labels += [p["label"] for key in ("objects", "circles", "arcs") for p in config.get(key, []) if "label" in p]
                        self.assertCountEqual(labels, [label["text"] for label in drawing["labels"]])
                        for label in drawing["labels"]:
                            bounds = label["bounds"]
                            self.assertEqual(label["text"], "".join(label["lines"]))
                            self.assertTrue(0 <= bounds["left"] < bounds["right"] <= width)
                            self.assertTrue(0 <= bounds["top"] < bounds["bottom"] <= height)
            self.assertEqual(original, config)


class WindowsRigidAngleLayoutTest(unittest.TestCase):
    """Presentation bounds must not narrow the unchanged rigid-angle model."""

    @classmethod
    def setUpClass(cls):
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    @staticmethod
    def fixture(scale=1.0):
        def figure(vertex, first, second, x, y, angle, first_distance, second_distance):
            points = [{"id": vertex, "x": x * scale, "y": y * scale, "labels": [vertex]}]
            for name, direction, distance in ((first, angle, first_distance),
                                              (second, angle + 1, second_distance)):
                points.append({"id": name, "x": (x + math.cos(direction) * distance) * scale,
                               "y": (y + math.sin(direction) * distance) * scale, "labels": [name]})
            return {"vertex": vertex, "sides": ["first", "second"], "region": "counterclockwise",
                    "diagram": {"points": points, "objects": [
                        {"id": name, "kind": "ray", "through": [vertex, endpoint],
                         "contains": [vertex, endpoint]}
                        for name, endpoint in (("first", first), ("second", second))]}}

        return {"type": "rigid_angle_copy",
                "reference": figure("O", "A", "B", 1, 2, 0, 4, 5),
                "copy": figure("V", "C", "D", 0, 0, .7, 6, 3),
                "initial": {"x": 7 * scale, "y": 6 * scale, "rotation": .2},
                "relation": "congruent", "positionTolerance": .1 * scale,
                "directionTolerance": .01}

    def draw(self, spec, **fields):
        return self.worker.ask("math_editor", action="rigid_layout", config=spec,
                               width=850, height=600, unit=1, textSize=15, **fields)

    def evaluate(self, spec, placement):
        block = {"type": "math_puzzle", "prompt": "Traslada y gira la copia sin deformarla.",
                 "responses": [{"id": "copy", "type": "math_rigid_angle_copy", "validator": spec}]}
        return self.worker.ask("validate_math", block=block, language="es",
                               responses={"copy": {"type": "rigid_angle", **placement}})

    def test_01_homogeneous_micro_scale_preserves_layout_and_geometric_verdict(self):
        normal, tiny = self.fixture(), self.fixture(1e-6)
        originals = copy.deepcopy((normal, tiny))
        large = self.draw(normal)
        small = self.draw(tiny)
        self.assertEqual(large["viewport"], small["viewport"])
        self.assertEqual(large["plot"], small["plot"])

        def compare_points(first, second):
            self.assertEqual(set(first), set(second))
            for key in first:
                self.assertAlmostEqual(first[key]["x"], second[key]["x"], delta=1e-6, msg=str(key))
                self.assertAlmostEqual(first[key]["y"], second[key]["y"], delta=1e-6, msg=str(key))

        compare_points({p["id"]: p["center"] for p in large["points"]},
                       {p["id"]: p["center"] for p in small["points"]})
        for field, coordinates in (("paths", ("start", "end")),
                                   ("arrows", ("tip", "wingA", "wingB"))):
            compare_points({(part["id"], key): part[key] for part in large[field] for key in coordinates},
                           {(part["id"], key): part[key] for part in small[field] for key in coordinates})
        compare_points({part["id"]: part["center"] for part in large["regions"]},
                       {part["id"]: part["center"] for part in small["regions"]})
        for large_region, small_region in zip(large["regions"], small["regions"]):
            for key in ("radius", "startDegrees", "sweepDegrees"):
                self.assertAlmostEqual(large_region[key], small_region[key], delta=1e-6)
        self.assertCountEqual([label["text"] for label in large["labels"]],
                              [label["text"] for label in small["labels"]])
        for spec, scale in ((normal, 1), (tiny, 1e-6)):
            correct = {"x": 1 * scale, "y": 2 * scale, "rotation": -.7}
            self.assertTrue(self.evaluate(spec, correct)["correct"])
            self.assertFalse(self.evaluate(spec, spec["initial"])["correct"])
        self.assertEqual(originals, (normal, tiny), "Layout mutated an authored figure or tolerance")

    def test_02_far_saved_placement_keeps_model_and_only_exposes_safe_render_primitives(self):
        spec = self.fixture()
        original = copy.deepcopy(spec)
        normal = self.draw(spec)
        stored = {"x": 1e9, "y": -1e9, "rotation": 1.2}
        drawing = self.draw(spec, placement=stored)
        self.assertEqual({"type": "rigid_angle", **stored}, drawing["placement"])
        self.assertFalse(self.evaluate(spec, stored)["correct"])
        viewport = drawing["viewport"]
        margin = 32

        def safe_pixel(point):
            self.assertTrue(math.isfinite(point["x"]) and math.isfinite(point["y"]), point)
            self.assertTrue(-margin <= point["x"] <= viewport["right"] + margin, point)
            self.assertTrue(-margin <= point["y"] <= viewport["bottom"] + margin, point)

        for field, coordinates in (("points", ("center",)), ("paths", ("start", "end")),
                                   ("arrows", ("tip", "wingA", "wingB")),
                                   ("circles", ("center",)), ("arcs", ("center",)),
                                   ("regions", ("center",))):
            for part in drawing[field]:
                for key in coordinates:
                    safe_pixel(part[key])
        for label in drawing["labels"]:
            bounds = label["bounds"]
            self.assertTrue(0 <= bounds["left"] < bounds["right"] <= viewport["right"], label)
            self.assertTrue(0 <= bounds["top"] < bounds["bottom"] <= viewport["bottom"], label)
        self.assertFalse(any(point["id"].startswith("copy:") for point in drawing["points"]),
                         "An offscreen naming marker appeared in the visible viewport")
        self.assertFalse(any(label["id"].startswith("copy:") for label in drawing["labels"]),
                         "A hidden copy acquired a clamped floating name")
        reference = {p["id"]: p for p in normal["points"] if p["id"].startswith("reference:")}
        self.assertEqual(reference, {p["id"]: p for p in drawing["points"] if p["id"].startswith("reference:")})
        recovered = self.draw(spec, placement=stored,
                              gesture={"mode": "move", "previous": {"x": 0, "y": 0},
                                       "current": {"x": 100000, "y": -100000}})
        self.assertEqual(reference, {p["id"]: p for p in recovered["points"] if p["id"].startswith("reference:")})
        self.assertEqual(3, sum(point["id"].startswith("copy:") for point in recovered["points"]))
        self.assertEqual(stored["rotation"], recovered["placement"]["rotation"])
        self.assertNotEqual(stored["x"], recovered["placement"]["x"])
        for point in recovered["points"]:
            safe_pixel(point["center"])
        self.assertEqual(original, spec)

    def test_03_extreme_finite_saved_placement_and_rotation_remain_recoverable(self):
        spec = self.fixture()
        stored = {"x": 1e308, "y": -1e308, "rotation": 1.2}
        original = copy.deepcopy(stored)
        drawing = self.draw(spec, placement=stored)
        self.assertEqual({"type": "rigid_angle", **stored}, drawing["placement"])
        rotated = self.draw(spec, placement=stored,
                            gesture={"mode": "rotate", "previous": {"x": 300, "y": 300},
                                     "current": {"x": 320, "y": 310}})
        self.assertEqual(stored["x"], rotated["placement"]["x"])
        self.assertEqual(stored["y"], rotated["placement"]["y"])
        self.assertTrue(math.isfinite(rotated["placement"]["rotation"]))
        for snapshot in (drawing, rotated):
            for field, coordinates in (("points", ("center",)), ("paths", ("start", "end")),
                                       ("arrows", ("tip", "wingA", "wingB")),
                                       ("circles", ("center",)), ("arcs", ("center",)),
                                       ("regions", ("center",))):
                for part in snapshot[field]:
                    for key in coordinates:
                        point = part[key]
                        self.assertTrue(math.isfinite(point["x"]) and math.isfinite(point["y"]), point)
                        self.assertTrue(-32 <= point["x"] <= 882 and -32 <= point["y"] <= 632, point)
            self.assertFalse(any(point["id"].startswith("copy:") for point in snapshot["points"]))
            self.assertFalse(any(label["id"].startswith("copy:") for label in snapshot["labels"]))
        self.assertEqual(original, stored)


if __name__ == "__main__":
    unittest.main(verbosity=2)

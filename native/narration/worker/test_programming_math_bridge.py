"""Headless Windows math validation of Programación 1's formatting activities.

These tests send only describe_math/validate_math to the real JVM bridge. They
neither create presentation controls nor synthesize or play narration. The
oracles below are independent of the authored validators; no curriculum, saved
student state, compiler receipts, or audio cache is written.
"""
from __future__ import annotations

import copy
from fractions import Fraction
import hashlib
import json
import unittest

from test_math_bridge import known_answer
from test_worker import ROOT, Worker


PROGRAMMING = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
UNIT = PROGRAMMING / "archivos_y_programa_completo"
PREFIX = "cpp_fundamentals__archivos_y_programa_completo__"


def fields(**values):
    return {"type": "fields", "values": {key: str(value) for key, value in values.items()}}


def matching(**pairs):
    return {"type": "matching", "pairs": pairs}


def choice(*selected):
    return {"type": "choice", "selected": list(selected)}


# Lesson name, zero-based page/block, stable puzzle suffix, and independently
# calculated answers. Keep field and option IDs: positions are not identities.
CASES = (
    ("representar_enteros_en_distintas_bases", (1, 5), "descomponer_203_octal", {
        "aportes": fields(sesenta_y_cuatro=128, ocho=0, unidades=3, total=131)}),
    ("representar_enteros_en_distintas_bases", (2, 5), "misma_escritura_bases_distintas", {
        "relaciones": matching(decimal="veinte", octal="dieciseis", hexadecimal="treinta_y_dos")}),
    ("estado_del_formato_y_flujos_independientes", (3, 3), "ajustes_independientes_del_formato", {
        "efectos": matching(a_decimal="decimal_con_peticion", a_minusculas="hex_minusculas_con_prefijo",
                            sin_prefijo="hex_sin_prefijo")}),
    ("estado_del_formato_y_flujos_independientes", (5, 3), "separar_base_fallo_y_flujo", {
        "efectos": matching(base_entrada="configurar_interpretacion", lectura="fallo_sin_cambio_base",
                            base_salida="otro_flujo")}),
    ("precision_y_presentacion_de_double", (1, 2), "interpretar_exponentes_distintos", {
        "valores": matching(positivo="grande", negativo="pequeno", cero="sin_cambio")}),
    ("precision_y_presentacion_de_double", (2, 3), "elegir_precision_para_el_mismo_dato", {
        "precisiones": fields(fija=5, cientifica=3, general=4)}),
    ("ancho_relleno_y_alineacion_de_campos", (3, 2), "seguir_ancho_pendiente", {
        "anchos": fields(solicitar=6, relleno=6, etiqueta=0, numero=0)}),
    ("ancho_relleno_y_alineacion_de_campos", (4, 4), "relacionar_orden_y_alineacion", {
        "salidas": matching(numero="relleno_delante", izquierda="relleno_detras", etiqueta="con_etiqueta")}),
    ("componer_informes_sin_cambiar_los_datos", (1, 2), "prediccion_del_informe", {
        "resumen": fields(total=12, suma_medidas="11.25", media="3.75"),
        "filas": choice("campos_conservados")}),
    ("componer_informes_sin_cambiar_los_datos", (3, 4), "contrastar_media_antes_de_formatear", {
        "sumas": fields(original="0.4375", redondeada="0.43"),
        "media_presentada": choice("original")}),
    ("texto_con_campos_y_patrones_literales", (3, 4), "contar_campos_y_argumentos_necesarios", {
        "conteos": fields(campos_repetidos=2, argumentos_repetidos=3, campos_rodeados=1,
                          argumentos_rodeados=2, campos_automaticos=1, argumentos_automaticos=1)}),
    ("bases_y_anchuras_en_cada_campo", (4, 5), "distinguir_prefijo_y_relleno_por_posicion", {
        "opciones_y_anchos": fields(funcion_primera="prefijo", relleno_primero=2,
                                    funcion_segunda="relleno", relleno_segundo=4)}),
    ("precision_por_campo_y_resumenes_formateados", (3, 5), "contar_escritura_y_relleno_tras_precision", {
        "longitudes_y_relleno": fields(longitud_general=1, relleno_general=5, longitud_fija=5,
                                       relleno_fija=1, longitud_cientifica=9, relleno_cientifica=0)}),
    ("compartir_flujos_sin_acoplar_el_calculo", (1, 5), "seguir_formato_del_destino_prestado", {
        "contenidos": matching(flujo_a="contenido_hexadecimal", flujo_b="contenido_decimal")}),
)


def selection_options(response):
    if response["type"] in {"math_multiple_choice_single", "math_multiple_choice_multi"}:
        return response.get("options", [])
    return [option for field in response.get("fields", []) if field["type"] == "dropdown"
            for option in field.get("options", [])]


def without_reviews(block):
    value = copy.deepcopy(block)
    for response in value["responses"]:
        for option in selection_options(response):
            option.pop("reviewFeedback", None)
    return value


class ProgrammingFormattingMathBridgeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = {}
        cls.lessons = {}
        cls.cases = []
        cls.guides = []
        for path in sorted(PROGRAMMING.rglob("lesson*.json")):
            raw = path.read_bytes()
            lesson = json.loads(raw.decode("utf-8-sig"))
            guided = [block for page in lesson.get("pages", []) for block in page.get("blocks", [])
                      if block.get("type") == "math_puzzle" and any(
                          "reviewFeedback" in option for response in block["responses"]
                          for option in selection_options(response))]
            if path.parent == UNIT or guided:
                cls.sources[path] = (raw, path.stat().st_mtime_ns)
                cls.lessons[path] = lesson
                cls.guides.extend((path, block) for block in guided)
        for name, (page, position), suffix, answers in CASES:
            path = UNIT / ("lesson_" + name + ".json")
            block = cls.lessons[path]["pages"][page]["blocks"][position]
            if block.get("id") != PREFIX + name + "__" + suffix:
                raise AssertionError(f"The formatting fixture moved or changed identity: {path}:{page}/{position}")
            cls.cases.append((path, block, answers))
        cls.before = copy.deepcopy(cls.lessons)
        cls.worker = Worker()

    @classmethod
    def tearDownClass(cls):
        try:
            cls.worker.close()
        finally:
            if cls.before != cls.lessons:
                raise AssertionError("Validation mutated the loaded curricular documents")
            for path, (raw, modified) in cls.sources.items():
                if path.read_bytes() != raw or path.stat().st_mtime_ns != modified:
                    raise AssertionError(f"Headless validation changed a curricular source: {path}")

    def evaluate(self, block, answers):
        before = copy.deepcopy((block, answers))
        result = self.worker.ask("validate_math", block=block, responses=answers, language="es")
        self.assertEqual(before, (block, answers), "Transport changed a caller's saved answer or source")
        return result

    def assert_correct(self, block, answers):
        result = self.evaluate(block, answers)
        self.assertTrue(result["evaluable"], result)
        self.assertTrue(result["correct"], result)
        self.assertEqual("correct", result["feedbackKind"])
        self.assertIn(block["feedback"]["correct"], result["feedback"])
        self.assertTrue(all(item["correct"] for item in result["results"]))
        return result

    def assert_rejected(self, block, answers, *, evaluable=None):
        result = self.evaluate(block, answers)
        self.assertFalse(result["correct"], result)
        if evaluable is not None:
            self.assertEqual(evaluable, result["evaluable"], result)
        self.assertEqual("incorrect" if result["evaluable"] else "error", result["feedbackKind"])
        self.assertNotIn(block["feedback"]["correct"], result["feedback"])
        return result

    def test_01_fourteen_real_activities_keep_typed_tables_and_stable_ids(self):
        self.assertEqual(14, len(self.cases))
        for path, block, answers in self.cases:
            with self.subTest(lesson=path.name, puzzle=block["id"]):
                self.assertEqual("math_puzzle", block["type"])
                self.assertEqual(set(answers), {item["id"] for item in block["responses"]})
                for response in block["responses"]:
                    answer = answers[response["id"]]
                    self.assertEqual(answer, known_answer(response["validator"]),
                                     "The authored validator differs from the independent oracle")
                    if answer["type"] == "fields":
                        self.assertEqual("math_multi_input", response["type"])
                        self.assertEqual("table", response["layout"])
                        definitions = {field["id"]: field for field in response["fields"]}
                        cells = [cell["field"] for row in response["rows"] for cell in row
                                 if isinstance(cell, dict) and "field" in cell]
                        self.assertEqual(set(answer["values"]), set(definitions))
                        self.assertCountEqual(definitions, cells)
                        self.assertEqual(len(cells), len(set(cells)))
                        for key, definition in definitions.items():
                            self.assertNotIn("initial", definition)
                            self.assertNotIn("value", definition)
                            if definition["type"] == "dropdown":
                                self.assertEqual("text", response["validator"]["fields"][key]["type"])
                                self.assertIn(answer["values"][key], {item["id"] for item in definition["options"]})
                            else:
                                self.assertEqual("numeric", definition["type"])
                    elif answer["type"] == "matching":
                        self.assertEqual("math_matching_drag", response["type"])
                        self.assertEqual(set(answer["pairs"]), {item["id"] for item in response["sources"]})
                        self.assertEqual(set(answer["pairs"].values()), {item["id"] for item in response["targets"]})
                    else:
                        self.assertEqual("math_multiple_choice_single", response["type"])

    def test_02_public_description_has_no_solution_or_preselected_answer(self):
        forbidden = {"validator", "expected", "correctFeedback", "incorrectFeedback", "reviewFeedback", "solution"}
        def inspect(value):
            if isinstance(value, dict):
                self.assertFalse(forbidden.intersection(value))
                for child in value.values():
                    inspect(child)
            elif isinstance(value, list):
                for child in value:
                    inspect(child)
        for path, block, _ in self.cases:
            with self.subTest(lesson=path.name, puzzle=block["id"]):
                result = self.worker.ask("describe_math", block=block, language="es")
                self.assertEqual(block["prompt"], result["prompt"])
                self.assertEqual([item["id"] for item in block["responses"]],
                                 [item["id"] for item in result["responses"]])
                inspect(result)

    def test_03_independent_answers_accept_and_empty_responses_never_succeed(self):
        for path, block, answers in self.cases:
            with self.subTest(lesson=path.name, puzzle=block["id"]):
                self.assert_correct(block, answers)
                self.assert_rejected(block, {}, evaluable=False)
                for response in block["responses"]:
                    identity = response["id"]
                    absent = copy.deepcopy(answers)
                    del absent[identity]
                    self.assert_rejected(block, absent, evaluable=False)
                    empty = copy.deepcopy(answers)
                    answer = empty[identity]
                    if answer["type"] == "fields":
                        answer["values"] = {key: "" for key in answer["values"]}
                    elif answer["type"] == "matching":
                        answer["pairs"] = {}
                    else:
                        answer["selected"] = []
                    self.assert_rejected(block, empty, evaluable=False)
                    invalid_type = copy.deepcopy(answers)
                    invalid_type[identity] = {"type": "text", "value": "0"}
                    self.assert_rejected(block, invalid_type, evaluable=False)

    def test_04_every_field_is_required_wrong_values_reject_and_numeric_equivalents_accept(self):
        for path, block, answers in self.cases:
            for response in block["responses"]:
                identity = response["id"]
                if answers[identity]["type"] != "fields":
                    continue
                definitions = {item["id"]: item for item in response["fields"]}
                for key, expected in answers[identity]["values"].items():
                    with self.subTest(lesson=path.name, response=identity, field=key):
                        missing = copy.deepcopy(answers)
                        del missing[identity]["values"][key]
                        self.assert_rejected(block, missing, evaluable=False)
                        wrong = copy.deepcopy(answers)
                        if definitions[key]["type"] == "dropdown":
                            wrong[identity]["values"][key] = next(item["id"] for item in
                                definitions[key]["options"] if item["id"] != expected)
                        else:
                            wrong[identity]["values"][key] = str(Fraction(expected) + 1)
                        result = self.assert_rejected(block, wrong, evaluable=True)
                        failed = next(item for item in result["results"] if item["id"] == identity)
                        self.assertFalse(failed["correct"])
                        self.assertEqual(key, failed.get("fieldId"))
                        if definitions[key]["type"] != "dropdown":
                            value = Fraction(expected)
                            equivalent = copy.deepcopy(answers)
                            equivalent[identity]["values"][key] = f"{value.numerator * 2}/{value.denominator * 2}"
                            self.assert_correct(block, equivalent)
                extra = copy.deepcopy(answers)
                extra[identity]["values"]["unrelated_field"] = "0"
                self.assert_rejected(block, extra, evaluable=False)

    def test_05_every_matching_tile_rejects_swaps_missing_and_duplicate_targets(self):
        for path, block, answers in self.cases:
            for identity, answer in answers.items():
                if answer["type"] != "matching":
                    continue
                keys = list(answer["pairs"])
                for index, key in enumerate(keys):
                    with self.subTest(lesson=path.name, response=identity, source=key):
                        other = keys[(index + 1) % len(keys)]
                        missing = copy.deepcopy(answers)
                        del missing[identity]["pairs"][key]
                        self.assert_rejected(block, missing, evaluable=False)
                        duplicate = copy.deepcopy(answers)
                        duplicate[identity]["pairs"][key] = duplicate[identity]["pairs"][other]
                        self.assert_rejected(block, duplicate, evaluable=False)
                        swapped = copy.deepcopy(answers)
                        pairs = swapped[identity]["pairs"]
                        pairs[key], pairs[other] = pairs[other], pairs[key]
                        self.assert_rejected(block, swapped, evaluable=True)

    def test_06_mixed_tables_do_not_accept_only_the_correct_choice_or_only_the_table(self):
        for path, block, answers in self.cases:
            for response in block["responses"]:
                identity = response["id"]
                if answers[identity]["type"] != "choice":
                    continue
                for option in response["options"]:
                    if option["id"] in answers[identity]["selected"]:
                        continue
                    with self.subTest(lesson=path.name, option=option["id"]):
                        wrong = copy.deepcopy(answers)
                        wrong[identity] = choice(option["id"])
                        self.assert_rejected(block, wrong, evaluable=True)
                        # Reuse a serialized draft after another failed attempt.
                        self.assert_correct(block, json.loads(json.dumps(answers)))

    def test_07_authored_selected_option_guides_preserve_legacy_answers_and_fingerprints(self):
        self.assertGreaterEqual(len(self.guides), 6)
        for path, block in self.guides:
            with self.subTest(lesson=path.name, puzzle=block["id"]):
                original = without_reviews(block)
                answers = {response["id"]: known_answer(response["validator"]) for response in block["responses"]}
                current = self.assert_correct(block, answers)
                legacy = self.assert_correct(original, json.loads(json.dumps(answers)))
                self.assertEqual(legacy["contentFingerprint"], current["contentFingerprint"])
                self.assertEqual(legacy["results"], current["results"])
                self.assertEqual(legacy["feedback"], current["feedback"])
                self.assertEqual(self.worker.ask("describe_math", block=original, language="es")["responses"],
                                 self.worker.ask("describe_math", block=block, language="es")["responses"])
                for response in block["responses"]:
                    if response["type"] not in {"math_multiple_choice_single", "math_multiple_choice_multi"}:
                        continue
                    for option in response.get("options", []):
                        if not option.get("reviewFeedback"):
                            continue
                        wrong = copy.deepcopy(answers)
                        wrong[response["id"]] = choice(option["id"])
                        if wrong[response["id"]] == answers[response["id"]]:
                            continue
                        result = self.assert_rejected(block, wrong, evaluable=True)
                        self.assertIn(option["reviewFeedback"], result["feedback"])
                        for absent in response["options"]:
                            review = absent.get("reviewFeedback")
                            if absent["id"] != option["id"] and review and review != option["reviewFeedback"]:
                                self.assertNotIn(review, result["feedback"])

    def test_08_curated_puzzle_text_and_data_remain_byte_exact(self):
        self.assertEqual(self.before, self.lessons)
        for path, (raw, modified) in self.sources.items():
            with self.subTest(source=path.name):
                self.assertEqual(hashlib.sha256(raw).digest(), hashlib.sha256(path.read_bytes()).digest())
                self.assertEqual(modified, path.stat().st_mtime_ns)


if __name__ == "__main__":
    unittest.main()

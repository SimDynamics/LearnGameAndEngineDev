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
import importlib.util
import itertools
import json
import subprocess
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


INSERTION_EXTRACTION_CASES = (
    ("insertar_un_tipo_y_encadenar_la_salida", (4, 4), "seguir_base_y_ancho_al_insertar_recuento", {
        "contenidos": matching(flujo_a="base_hexadecimal", flujo_b="ancho_en_apertura")}),
    ("extraer_un_tipo_sin_cambiarlo_en_fallo", (6, 3), "separar_objeto_indicadores_y_texto_restante", {
        "recorridos": matching(registro_y_resto="aceptado", dominio_rechazado="fuera_de_dominio",
                               cierre_ausente="truncado")}),
)


LOADING_STATE_CASES = (
    ("leer_y_validar_una_linea_completa", (1, 4), "comparar_indicadores_tras_omitir_blancos", {
        "indicadores": fields(eof_a=1, fail_a=1, eof_b=1, fail_b=0, eof_c=0, fail_c=0)}),
    ("leer_y_validar_una_linea_completa", (4, 6), "distinguir_obtener_linea_y_aceptar_registro", {
        "salida": choice("obtener_y_validar_por_separado")}),
    ("aceptar_registros_y_detectar_duplicados", (2, 4), "aplicar_primera_valida_sin_reservar_rechazos", {
        "contadores": fields(guardados=3, descartadas=3), "registros": choice("primera_valida_en_orden")}),
    ("actualizar_todo_o_conservar_el_estado_anterior", (4, 3), "seguir_estado_actual_entre_recargas", {
        "informes": matching(primera="rechazo_estado_inicial", segunda="coleccion_nueva",
                             tercera="rechazo_estado_actual", cuarta="vacio_aceptado")}),
    ("representar_presencia_y_cambiar_la_organizacion", (4, 2), "conservar_resultado_entre_consultas", {
        "informes": matching(consulta_dos="presente_con_valores", consulta_tres="ausente_conserva_anterior",
                             consulta_siete="presente_con_ceros", consulta_negativa="identificador_rechazado")}),
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


class ProgrammingInsertionExtractionMathBridgeTest(unittest.TestCase):
    """Only U12L14/L15 additions; selectable without rerunning the earlier 14."""

    @classmethod
    def setUpClass(cls):
        cls.sources = {}
        cls.lessons = {}
        cls.cases = []
        cls.fingerprints = {}
        cls.previous_lessons = {}
        # Published origin of the unchanged ordinary answers and stable slots,
        # already present locally. This is provenance, not another active asset.
        previous = "7e90cea77242b0a7c6228a45249e6f6eadcda041"
        for name, (page, position), identity, answers in INSERTION_EXTRACTION_CASES:
            path = UNIT / ("lesson_" + name + ".json")
            raw = path.read_bytes()
            lesson = json.loads(raw.decode("utf-8-sig"))
            cls.sources[path] = (raw, path.stat().st_mtime_ns)
            cls.lessons[path] = lesson
            old = subprocess.check_output(["git", "show", previous + ":" + path.relative_to(ROOT).as_posix()], cwd=ROOT)
            cls.previous_lessons[path] = json.loads(old.decode("utf-8-sig"))
            block = lesson["pages"][page]["blocks"][position]
            if (lesson["stateLayout"]["current"][page]["blocks"][position] != identity or
                    block.get("id") != PREFIX + name + "__" + identity):
                raise AssertionError(f"The insertion/extraction addition changed its stable identity: {path}")
            cls.cases.append((path, block, answers))
            canonical = json.dumps(block, ensure_ascii=False, separators=(",", ":")).replace("/", "\\/")
            cls.fingerprints[block["id"]] = hashlib.sha256(canonical.encode()).hexdigest()
        cls.before = copy.deepcopy(cls.lessons)
        cls.worker = Worker()

    tearDownClass = classmethod(ProgrammingFormattingMathBridgeTest.tearDownClass.__func__)
    assert_correct = ProgrammingFormattingMathBridgeTest.assert_correct
    assert_rejected = ProgrammingFormattingMathBridgeTest.assert_rejected

    def evaluate(self, block, answers):
        result = ProgrammingFormattingMathBridgeTest.evaluate(self, block, answers)
        self.assertEqual(self.fingerprints[block["id"]], result["contentFingerprint"],
                         "Attempt state changed the exact Android-compatible content identity")
        return result

    def test_01_new_additions_have_typed_empty_matching_controls_and_independent_answers(self):
        self.assertEqual(2, len(self.cases))
        for path, block, answers in self.cases:
            with self.subTest(lesson=path.name):
                self.assertEqual("math_puzzle", block["type"])
                self.assertEqual(set(answers), {response["id"] for response in block["responses"]})
                for response in block["responses"]:
                    self.assertEqual("math_matching_drag", response["type"])
                    self.assertEqual(answers[response["id"]], known_answer(response["validator"]))
                    self.assertNotIn("initial", response)
                    self.assertNotIn("pairs", response)
                    self.assertEqual(set(answers[response["id"]]["pairs"]),
                                     {source["id"] for source in response["sources"]})
                    self.assertEqual(set(answers[response["id"]]["pairs"].values()),
                                     {target["id"] for target in response["targets"]})
                self.assertTrue(block["feedback"]["correct"].strip())
                self.assertTrue(block["feedback"]["incorrect"].strip())

    test_02_public_description_has_no_solution_or_preselected_answer = (
        ProgrammingFormattingMathBridgeTest.test_02_public_description_has_no_solution_or_preselected_answer)
    test_03_independent_answers_accept_and_empty_responses_never_succeed = (
        ProgrammingFormattingMathBridgeTest.test_03_independent_answers_accept_and_empty_responses_never_succeed)
    test_04_every_matching_tile_rejects_swaps_missing_and_duplicate_targets = (
        ProgrammingFormattingMathBridgeTest.test_05_every_matching_tile_rejects_swaps_missing_and_duplicate_targets)

    def test_05_all_matching_permutations_and_reordered_answer_maps_keep_their_verdict(self):
        for path, block, answers in self.cases:
            identity, answer = next(iter(answers.items()))
            expected = answer["pairs"]
            for targets in itertools.permutations(expected.values()):
                attempt = {identity: matching(**dict(zip(expected, targets)))}
                with self.subTest(lesson=path.name, pairs=attempt):
                    if attempt == answers:
                        self.assert_correct(block, attempt)
                    else:
                        result = self.assert_rejected(block, attempt, evaluable=True)
                        self.assertIn(block["feedback"]["incorrect"], result["feedback"])
            # Serialization order is not the meaning of a matching answer.
            reordered = {identity: matching(**dict(reversed(list(expected.items()))))}
            self.assert_correct(block, json.loads(json.dumps(reordered)))
            unknown = {identity: matching(**expected, obsolete_source="obsolete_target")}
            self.assert_rejected(block, unknown, evaluable=False)
            for key in expected:
                blank = copy.deepcopy(answers)
                blank[identity]["pairs"][key] = ""
                self.assert_rejected(block, blank, evaluable=False)

    def test_06_output_cards_keep_all_spaces_capitalization_and_final_separators(self):
        labels = (
            {"base_hexadecimal": "Salida: `[a;2]|a`", "ancho_en_apertura": "Salida: `|   [10;2]|`"},
            {"truncado": "Salida: `1:1|[6;2]|`", "aceptado": "Salida: `0:0|[2;1]|resto`",
             "fuera_de_dominio": "Salida: `1:0|[6;2]|RESTO`"},
        )
        for (_, block, _), expected in zip(self.cases, labels):
            response = block["responses"][0]
            self.assertEqual(expected, {target["id"]: target["label"] for target in response["targets"]})
            for target in response["targets"]:
                self.assertEqual([{"type": "text", "text": expected[target["id"]], "mathMode": "text"}],
                                 target["content"])
            description = self.worker.ask("describe_math", block=block, language="es")
            visible = description["responses"][0]["config"]["targets"]
            self.assertEqual(expected, {target["id"]: target["label"] for target in visible})

    def test_07_new_requirement_paragraph_is_preserved_without_becoming_an_answer(self):
        path = self.cases[0][0]
        lesson = self.lessons[path]
        self.assertEqual("requisitos_bateria_de_recuentos", lesson["stateLayout"]["current"][3]["blocks"][4])
        block = lesson["pages"][3]["blocks"][4]
        self.assertEqual("paragraph", block["type"])
        for absent in ("responses", "validator", "correctIndex", "answers"):
            self.assertNotIn(absent, block)
        for literal in ("`int probar(const Recuento& resumen)`", "`{3, 2}`", "`{0, 0}`", "`{10, 10}`",
                        "`Memoria=[3;2];fin|Guardado`\n`Memoria=[0;0];fin|Guardado`\n`Memoria=[10;10];fin|Guardado`",
                        "`[10;10]`, sin salto final"):
            self.assertIn(literal, block["text"])

    def test_08_nine_ordinary_guides_preserve_published_answer_contracts_and_storage_slots(self):
        # These are native programming exercises, not math responses. Check
        # their preservation here; native evaluator tests own their verdicts.
        protected = {"type", "id", "question", "code", "initialCode", "language", "options", "answers",
                     "correctIndex", "correctOrder", "fragments", "explanation"}
        path = self.cases[1][0]
        current, old = self.lessons[path], self.previous_lessons[path]
        old_blocks = {identity: block for page, slots in zip(old["pages"], old["stateLayout"]["current"])
                      for identity, block in zip(slots["blocks"], page["blocks"])}
        guided = []
        for page, slots in zip(current["pages"], current["stateLayout"]["current"]):
            for identity, block in zip(slots["blocks"], page["blocks"]):
                if "reviewFeedback" not in block:
                    continue
                guided.append(identity)
                with self.subTest(identity=identity):
                    self.assertIn(identity, old_blocks)
                    self.assertEqual({key: value for key, value in old_blocks[identity].items() if key in protected},
                                     {key: value for key, value in block.items() if key in protected})
                    review = block["reviewFeedback"]
                    if block["type"] == "multiple_choice":
                        self.assertEqual(len(block["options"]), len(review))
                        self.assertTrue(all(isinstance(text, str) and text.strip() for text in review))
                    else:
                        self.assertIsInstance(review, str)
                        self.assertTrue(review.strip())
        self.assertEqual(9, len(guided))
        self.assertEqual(old["stateLayout"]["origin"], current["stateLayout"]["origin"])
        self.assertEqual(old["stateLayout"]["family"], current["stateLayout"]["family"])
        self.assertEqual(old["stateLayout"]["orderRefinements"], current["stateLayout"]["orderRefinements"])

    test_09_curated_puzzle_text_and_data_remain_byte_exact = (
        ProgrammingFormattingMathBridgeTest.test_08_curated_puzzle_text_and_data_remain_byte_exact)


class ProgrammingLoadingStateMathBridgeTest(unittest.TestCase):
    """Only the five current U12L16–L19 additions and U12L16–L20 provenance."""

    @classmethod
    def setUpClass(cls):
        cls.sources, cls.lessons, cls.published, cls.fingerprints = {}, {}, {}, {}
        cls.cases = []
        names = tuple(dict.fromkeys(case[0] for case in LOADING_STATE_CASES)) + (
            "clasificar_caracteres_y_normalizar_una_copia",)
        for name in names:
            path = UNIT / ("lesson_" + name + ".json")
            raw = path.read_bytes()
            cls.sources[path] = (raw, path.stat().st_mtime_ns)
            cls.lessons[path] = json.loads(raw.decode("utf-8-sig"))
            cls.published[path] = {}
            for locale in ("es", "en", "fr"):
                localized = ROOT / path.relative_to(ROOT).as_posix().replace("/es/", "/" + locale + "/")
                old = subprocess.check_output(["git", "show", "690dc98092abf3108dd8f712a074fab1d0ac8f4b:" +
                                               localized.relative_to(ROOT).as_posix()], cwd=ROOT)
                cls.published[path][locale] = json.loads(old.decode("utf-8-sig"))
                if locale != "es":
                    original = localized.read_bytes()
                    cls.sources[localized] = (original, localized.stat().st_mtime_ns)
        for name, (page, position), identity, answers in LOADING_STATE_CASES:
            path = UNIT / ("lesson_" + name + ".json")
            lesson = cls.lessons[path]
            block = lesson["pages"][page]["blocks"][position]
            if (lesson["stateLayout"]["current"][page]["blocks"][position] != identity or
                    block.get("id") != PREFIX + name + "__" + identity):
                raise AssertionError(f"A loading-state math fixture changed identity or position: {path}")
            cls.cases.append((path, block, answers))
            canonical = json.dumps(without_reviews(block), ensure_ascii=False, separators=(",", ":")).replace("/", "\\/")
            cls.fingerprints[block["id"]] = hashlib.sha256(canonical.encode()).hexdigest()
        cls.before = copy.deepcopy(cls.lessons)
        cls.worker = Worker(missing_resources=True)

    tearDownClass = classmethod(ProgrammingFormattingMathBridgeTest.tearDownClass.__func__)
    assert_correct = ProgrammingFormattingMathBridgeTest.assert_correct
    assert_rejected = ProgrammingFormattingMathBridgeTest.assert_rejected
    evaluate = ProgrammingInsertionExtractionMathBridgeTest.evaluate

    def test_01_new_loading_activities_keep_exact_response_types_and_independent_answers(self):
        self.assertEqual(5, len(self.cases))
        numeric, matching_tiles, selections = 0, 0, 0
        for path, block, answers in self.cases:
            with self.subTest(lesson=path.name, puzzle=block["id"]):
                self.assertEqual("math_puzzle", block["type"])
                self.assertEqual(set(answers), {response["id"] for response in block["responses"]})
                for response in block["responses"]:
                    answer = answers[response["id"]]
                    self.assertEqual(answer, known_answer(response["validator"]))
                    self.assertNotIn("initial", response)
                    if answer["type"] == "fields":
                        self.assertEqual("math_multi_input", response["type"])
                        self.assertEqual("table", response["layout"])
                        definitions = response["fields"]
                        self.assertEqual(set(answer["values"]), {field["id"] for field in definitions})
                        cells = [cell["field"] for row in response["rows"] for cell in row if isinstance(cell, dict)]
                        self.assertCountEqual(answer["values"], cells)
                        for field in definitions:
                            self.assertEqual("numeric", field["type"])
                            self.assertNotIn("initial", field)
                            self.assertNotIn("value", field)
                            numeric += 1
                    elif answer["type"] == "matching":
                        self.assertEqual("math_matching_drag", response["type"])
                        self.assertEqual(set(answer["pairs"]), {item["id"] for item in response["sources"]})
                        self.assertEqual(set(answer["pairs"].values()), {item["id"] for item in response["targets"]})
                        matching_tiles += len(answer["pairs"])
                    else:
                        self.assertEqual("math_multiple_choice_single", response["type"])
                        self.assertTrue(set(answer["selected"]) <= {option["id"] for option in response["options"]})
                        selections += 1
        self.assertEqual((8, 8, 2), (numeric, matching_tiles, selections))

    test_02_public_description_has_no_solution_or_preselected_answer = (
        ProgrammingFormattingMathBridgeTest.test_02_public_description_has_no_solution_or_preselected_answer)
    test_03_independent_answers_accept_and_empty_responses_never_succeed = (
        ProgrammingFormattingMathBridgeTest.test_03_independent_answers_accept_and_empty_responses_never_succeed)
    test_04_every_numeric_field_is_required_and_accepts_rational_equivalents = (
        ProgrammingFormattingMathBridgeTest.test_04_every_field_is_required_wrong_values_reject_and_numeric_equivalents_accept)
    test_05_every_matching_tile_rejects_swaps_missing_and_duplicate_targets = (
        ProgrammingFormattingMathBridgeTest.test_05_every_matching_tile_rejects_swaps_missing_and_duplicate_targets)
    test_06_every_wrong_choice_rejects_even_with_correct_numeric_fields = (
        ProgrammingFormattingMathBridgeTest.test_06_mixed_tables_do_not_accept_only_the_correct_choice_or_only_the_table)
    def test_07_all_permutations_preserve_meaning_without_relying_on_map_order(self):
        for path, block, answers in self.cases:
            for identity, answer in answers.items():
                if answer["type"] != "matching":
                    continue
                expected = answer["pairs"]
                for targets in itertools.permutations(expected.values()):
                    attempt = {identity: matching(**dict(zip(expected, targets)))}
                    with self.subTest(lesson=path.name, pairs=attempt):
                        if attempt == answers:
                            self.assert_correct(block, attempt)
                        else:
                            result = self.assert_rejected(block, attempt, evaluable=True)
                            self.assertIn(block["feedback"]["incorrect"], result["feedback"])
                self.assert_correct(block, {identity: matching(**dict(reversed(list(expected.items()))))})
                self.assert_rejected(block, {identity: matching(**expected, obsolete_source="obsolete_target")}, evaluable=False)

    def test_08_zero_is_a_valid_answer_but_nan_infinity_and_partial_numbers_are_not(self):
        for path, block, answers in self.cases:
            for identity, answer in answers.items():
                if answer["type"] != "fields":
                    continue
                for key, value in answer["values"].items():
                    if value == "0":
                        for equivalent in ("0/2", "0.0", "-0"):
                            with self.subTest(lesson=path.name, field=key, equivalent=equivalent):
                                changed = copy.deepcopy(answers)
                                changed[identity]["values"][key] = equivalent
                                self.assert_correct(block, changed)
                    for invalid in ("", "-", "NaN", "Infinity", "-Infinity", "1e309", "0/0"):
                        with self.subTest(lesson=path.name, field=key, invalid=invalid):
                            changed = copy.deepcopy(answers)
                            changed[identity]["values"][key] = invalid
                            self.assert_rejected(block, changed, evaluable=False)

    def test_09_selected_guides_do_not_change_answer_identity_or_disclose_other_options(self):
        guided = 0
        for path, block, answers in self.cases:
            for response in block["responses"]:
                if response["type"] != "math_multiple_choice_single":
                    continue
                guided += 1
                baseline = without_reviews(block)
                current = self.assert_correct(block, answers)
                previous = self.assert_correct(baseline, answers)
                for field in ("contentFingerprint", "results", "feedback"):
                    self.assertEqual(previous[field], current[field])
                self.assertEqual(self.worker.ask("describe_math", block=block, language="es")["responses"],
                                 self.worker.ask("describe_math", block=baseline, language="es")["responses"])
                for option in response["options"]:
                    self.assertTrue(option.get("reviewFeedback", "").strip())
                    if option["id"] in answers[response["id"]]["selected"]:
                        continue
                    changed = copy.deepcopy(answers)
                    changed[response["id"]] = choice(option["id"])
                    result = self.assert_rejected(block, changed, evaluable=True)
                    self.assertIn(option["reviewFeedback"], result["feedback"])
                    for absent in response["options"]:
                        if absent["id"] != option["id"]:
                            self.assertNotIn(absent["reviewFeedback"], result["feedback"])
        self.assertEqual(2, guided)

    def test_10_original_storage_and_localized_families_match_the_published_positional_origin(self):
        # Use the existing state-contract encoder against independent published
        # sources. This verifies provenance, not native migration or user state.
        spec = importlib.util.spec_from_file_location("loading_state_contracts", ROOT / "scripts/curriculum/stable_state_layout.py")
        schema = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(schema)
        protected = {"type", "id", "question", "code", "initialCode", "language", "options", "answers", "correctIndex",
                     "correctOrder", "fragments", "explanation", "outputCheck", "projectFiles", "sourceFiles", "cmake"}
        for path, current in self.lessons.items():
            with self.subTest(lesson=path.name):
                published = self.published[path]
                expected_origin = schema.prepare(published["es"])["stateLayout"]["origin"]
                self.assertEqual(expected_origin, current["stateLayout"]["origin"])
                self.assertEqual({locale: {"sha256": schema.fingerprint(old),
                                          "origin": schema.prepare(old)["stateLayout"]["origin"]}
                                  for locale, old in published.items()}, current["stateLayout"]["family"])
                current_blocks = {identity: block for page, slots in zip(current["pages"], current["stateLayout"]["current"])
                                  for identity, block in zip(slots["blocks"], page["blocks"])}
                for page, old_page in enumerate(published["es"]["pages"]):
                    for position, old in enumerate(old_page["blocks"]):
                        identity = f"block_{page:04d}_{position:04d}"
                        self.assertIn(identity, current_blocks)
                        self.assertEqual({key: value for key, value in old.items() if key in protected},
                                         {key: value for key, value in current_blocks[identity].items() if key in protected}, identity)
                for locale in ("en", "fr"):
                    localized = ROOT / path.relative_to(ROOT).as_posix().replace("/es/", "/" + locale + "/")
                    self.assertEqual(published[locale], json.loads(localized.read_bytes().decode("utf-8-sig")))

    test_11_curated_puzzle_text_and_data_remain_byte_exact = (
        ProgrammingFormattingMathBridgeTest.test_08_curated_puzzle_text_and_data_remain_byte_exact)


if __name__ == "__main__":
    unittest.main()

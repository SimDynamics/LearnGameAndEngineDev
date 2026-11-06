"""Windows output-data speech plans, without synthesis, playback or GUI access.

The examples distinguish a program's displayed data from source statements.
Their expected meanings are independent of presentation colors and of the
implementation's token detectors. Only plan/curriculum_plan/identity/shutdown
are sent. Reconstructing a historical plan never creates listening credit.
"""
from __future__ import annotations

import json
import re
import unittest

from test_worker import ROOT, Worker


UNIT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo"
ASSETS = (
    UNIT / "lesson_compartir_flujos_sin_acoplar_el_calculo.json",
    UNIT / "lesson_insertar_un_tipo_y_encadenar_la_salida.json",
    UNIT / "lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json",
)


def units(text):
    return len(text.encode("utf-16-le")) // 2


def piece(text, start, end):
    return text.encode("utf-16-le")[2 * start:2 * end].decode("utf-16-le")


def speech(parts):
    return " ".join(part["spokenText"] for part in parts)


class U12DisplayedOutputSpeechTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in ASSETS}
        cls.labels = []
        for path, (raw, _) in cls.original.items():
            lesson = json.loads(raw.decode("utf-8-sig"))
            for page in lesson["pages"]:
                for block in page["blocks"]:
                    if block.get("type") != "math_puzzle":
                        continue
                    for response in block["responses"]:
                        if response["type"] == "math_matching_drag":
                            for target in response["targets"]:
                                if target.get("label", "").startswith("Salida: "):
                                    cls.labels.append(target["label"])
        required = {"Salida: `N=1a|1b`", "Salida: `|   [10;2]|`", "Salida: `[a;2]|a`",
                    "Salida: `0:0|[2;1]|resto`", "Salida: `1:0|[6;2]|RESTO`", "Salida: `1:1|[6;2]|`"}
        if not required.issubset(cls.labels):
            raise AssertionError("The current U12 output cards changed; inspect their authored data")
        # A plan requires no model. Missing resources also guard against an
        # accidental dependency on model installation during this test.
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.worker.close()
        finally:
            for path, (raw, modified) in cls.original.items():
                if path.read_bytes() != raw or path.stat().st_mtime_ns != modified:
                    raise AssertionError(f"Speech planning changed an original curricular asset: {path}")

    def plan(self, source, *, mode="auto", context="auto", voice="ef_dora", previous_policy=None):
        policy = {} if previous_policy is None else {"previousOutputDataPolicy": previous_policy}
        parts = self.worker.ask("plan", text=source, language="es", voice=voice,
                                mathMode=mode, readingContext=context, **policy)["segments"]
        self.assertTrue(parts, source)
        cursor = 0
        for part in parts:
            start, end = part["sourceStart"], part["sourceEnd"]
            self.assertTrue(cursor <= start < end <= units(source), part)
            self.assertEqual(piece(source, start, end), part["text"])
            self.assertRegex(part["key"], r"^[0-9a-f]{64}$")
            self.assertNotIn("`", part["preparedText"])
            self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))
            self.assertTrue(part["spokenRanges"], "A spoken segment lost all original-source word owners")
            for spoken, shown in zip(part["spokenRanges"], part["displayedRanges"]):
                self.assertTrue(0 <= spoken["start"] < spoken["end"] <= units(part["spokenText"]), spoken)
                self.assertTrue(0 <= shown["start"] < shown["end"] <= units(part["text"]), shown)
                # Decoding the ranges also rejects splitting a surrogate pair.
                self.assertTrue(piece(part["spokenText"], spoken["start"], spoken["end"]))
                self.assertTrue(piece(part["text"], shown["start"], shown["end"]))
            for match in re.finditer(r"[^\W_]", part["spokenText"], re.UNICODE):
                first, last = units(part["spokenText"][:match.start()]), units(part["spokenText"][:match.end()])
                self.assertTrue(any(word["start"] <= first and word["end"] >= last
                                    for word in part["spokenRanges"]),
                                "An audible letter or digit has no original-source owner")
            cursor = end
        return parts

    def assert_literal_equality(self, source, **options):
        parts = self.plan(source, **options)
        spoken = speech(parts).casefold()
        self.assertIn("igual", spoken)
        self.assertNotIn("asigna", spoken)
        return parts

    def assert_symbol_owner(self, source, parts, at, expected):
        first, last = units(source[:at]), units(source[:at + 1])
        words = []
        for part in parts:
            for spoken, shown in zip(part["spokenRanges"], part["displayedRanges"]):
                start, end = part["sourceStart"] + shown["start"], part["sourceStart"] + shown["end"]
                if start <= first and end >= last:
                    words.append(piece(part["spokenText"], spoken["start"], spoken["end"]))
        self.assertIn(expected, " ".join(words).casefold(),
                      f"The spoken symbol lost ownership of its exact original glyph: {source}")

    def test_01_displayed_equals_are_literal_data_not_assignment(self):
        for source in ("La salida es `N=1a|1b`.", "La salida esperada es `Total:12;Media=3.75`.",
                       "El resultado esperado es `N=26`.", "Salida: `Balance=5`", "Salida: `N=1a|1b`"):
            for prefix in ("", "𐐀 "):
                with self.subTest(source=source, prefix=prefix):
                    text = prefix + source
                    parts = self.assert_literal_equality(text)
                    for at, value in enumerate(text):
                        if value == "=":
                            self.assert_symbol_owner(text, parts, at, "igual")

    def test_02_actual_output_cards_keep_numeric_colons_but_pause_the_output_heading(self):
        for label in self.labels:
            for source in (label, "𐐀 " + label):
                with self.subTest(source=source):
                    parts = self.plan(source, mode="text")
                    spoken = speech(parts).casefold()
                    self.assertNotIn("asigna", spoken)
                    numeric = list(re.finditer(r"(?<=\d):(?=\d)", source))
                    self.assertEqual(len(numeric), spoken.count("dos puntos"),
                                     "A prose heading was named or a numeric data separator was omitted")
                    for match in numeric:
                        self.assert_symbol_owner(source, parts, match.start(), "dos puntos")
                    if "=" in source:
                        self.assertIn("igual", spoken)
                    self.assertEqual(parts, self.plan(source, mode="text"))

    def test_03_output_labels_and_prose_colons_pause_without_technical_symbol_names(self):
        for source in ("Resultado: continúa con cuidado.", "La salida es `Entrada: datos`.",
                       "La consola muestra la etiqueta `Inicio:`.", "La salida incluye `Media:3.75`.",
                       "El programa muestra `Inicio:` y `Ahora:` en dos líneas."):
            with self.subTest(source=source):
                spoken = speech(self.plan(source)).casefold()
                self.assertNotIn("dos puntos", spoken)
                self.assertNotIn("asigna", spoken)

    def test_04_source_statements_strings_and_technical_colons_keep_their_meaning(self):
        statements = ("`total = 3;`", "La instrucción `total = 3;` cambia el valor.",
                      "El código `std::string texto = \"Error: fin\";` construye una cadena.",
                      "La salida de `std::cout` no modifica la instrucción `total = 3;`.")
        for source in statements:
            with self.subTest(source=source):
                self.assertIn("asigna", speech(self.plan(source)).casefold())
        for source in ("La cadena `\"Inicio: 7\"` conserva sus caracteres.",
                       "`std::cout << \"Entrada: datos\";`", "El signo `:` separa los campos.",
                       "La etiqueta C++ `salida:` precede a una sentencia.", "La salida es `7:3`."):
            with self.subTest(source=source):
                self.assertIn("dos puntos", speech(self.plan(source)).casefold())
        scoped = speech(self.plan("El objetivo es `std::cout << 11;`.")).casefold()
        self.assertIn("operador de resolución de ámbito", scoped)

    def test_05_one_output_span_cannot_change_a_later_source_statement_or_prose_tail(self):
        source = "La salida es `N=1a|1b`. La instrucción `total = 3;` es una actualización del valor."
        parts = self.plan(source)
        spoken = speech(parts).casefold()
        self.assertEqual(1, spoken.count("asigna"))
        self.assertIn("igual", spoken)
        self.assertIn("es una actualización del valor", spoken)
        self.assertEqual(parts, self.plan(source))
        # A cue inside an earlier displayed sample cannot classify later code.
        source = "El identificador `salida` precede a la instrucción `total = 3;`."
        self.assertIn("asigna", speech(self.plan(source)).casefold())

    def test_06_ordinary_conjunction_remains_natural_while_an_explicit_letter_is_named(self):
        for source in ("Letras y palabras.", "El texto contiene letras y palabras.",
                       "La salida es `letras y palabras`.", "Cada letra y las palabras permanecen."):
            with self.subTest(source=source):
                spoken = speech(self.plan(source)).casefold()
                self.assertNotRegex(spoken, r"(?:i|y) griega")
                self.assertIn(" y ", " " + spoken + " ")
        self.assertRegex(speech(self.plan("El símbolo `y` es una letra.")).casefold(), r"(?:i|y) griega")

    def test_07_foreground_background_and_repeat_plans_share_preparation_and_recording_keys(self):
        samples = [(label, "text") for label in self.labels]
        samples += [("La salida es `N=1a|1b`.", "auto"),
                    ("La salida es `Entrada: datos`. Después conserva letras y palabras.", "auto"),
                    ("La instrucción `total = 3;` es una actualización del valor.", "auto"),
                    ("La cadena `\"Inicio: 7\"` conserva sus caracteres.", "auto")]
        for source, mode in samples:
            for voice in ("ef_dora", "em_santa"):
                with self.subTest(source=source, mode=mode, voice=voice):
                    foreground = self.plan(source, mode=mode, voice=voice)
                    lesson = {"pages": [{"blocks": [{"type": "paragraph", "text": source, "mathMode": mode}]}]}
                    background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson, ensure_ascii=False),
                        page=0, language="es", uiLanguage="es", voice=voice)["segments"]
                    for field in ("preparedText", "key"):
                        self.assertEqual([part[field] for part in foreground], [part[field] for part in background], field)
                    self.assertEqual(foreground, self.plan(source, mode=mode, voice=voice))

    def test_08_previous_output_policy_reconstructs_old_speech_without_contaminating_current_plans(self):
        affected = (("La salida es `N=1a|1b`.", "asigna", "igual"),
                    ("La salida es `letras y palabras`.", "griega", " y "))
        unchanged = ("Primero conserva el dato.", "Después comprueba el resultado.",
                     "La instrucción `total = 3;` actualiza el valor.",
                     "La cadena `\"Inicio: 7\"` conserva sus caracteres.",
                     "La salida es `Llaves: 11`.", "La salida es `7:3`.")
        for mode in ("auto", "text"):
            for context in ("auto", "cpp", "cpp_previous_parentheses"):
                options = {"mode": mode, "context": context}
                for source, old_word, current_word in affected:
                    with self.subTest(source=source, mode=mode, context=context):
                        current = self.plan(source, **options)
                        old = self.plan(source, previous_policy=True, **options)
                        self.assertIn(old_word, speech(old).casefold())
                        self.assertNotIn(old_word, speech(current).casefold())
                        self.assertIn(current_word, speech(current).casefold())
                        self.assertNotEqual([part["preparedText"] for part in old],
                                            [part["preparedText"] for part in current])
                        self.assertNotEqual([part["key"] for part in old], [part["key"] for part in current])
                        self.assertEqual(current, self.plan(source, **options))
                        self.assertEqual(current, self.plan(source, previous_policy=False, **options))
                        self.assertEqual(old, self.plan(source, previous_policy=True, **options))
                for source in unchanged:
                    with self.subTest(unaffected=source, mode=mode, context=context):
                        current = self.plan(source, **options)
                        self.assertEqual(current, self.plan(source, previous_policy=True, **options))
                        self.assertEqual(current, self.plan(source, **options))

    def test_09_historical_plan_identity_preserves_neighbor_recordings_without_inventing_a_bookmark(self):
        source = "Primero conserva el dato. La salida es `N=1a|1b`. Después comprueba el resultado."
        for mode in ("auto", "text"):
            with self.subTest(mode=mode):
                current = self.plan(source, mode=mode)
                old = self.plan(source, mode=mode, previous_policy=True)
                self.assertEqual(3, len(current))
                self.assertEqual(3, len(old))
                for position in (0, 2):
                    self.assertEqual(current[position], old[position],
                                     "A changed output phrase invalidated an unaffected neighboring recording")
                self.assertEqual((current[1]["sourceStart"], current[1]["sourceEnd"], current[1]["text"]),
                                 (old[1]["sourceStart"], old[1]["sourceEnd"], old[1]["text"]))
                self.assertIn("asigna", old[1]["spokenText"])
                self.assertIn("igual", current[1]["spokenText"])
                # Authenticate the page identity through its real existing API.
                # No bookmark slots, offsets or heard certificates are written.
                common = {"page": "qa-u12-output-provenance", "language": "es", "voice": "ef_dora", "engine": "KOKORO"}
                def identity(parts):
                    return self.worker.ask("identity", texts=[part["preparedText"] for part in parts], **common)["key"]
                previous_key = identity(old)
                current_key = identity(current)
                self.assertRegex(previous_key, r"^[0-9a-f]{64}$")
                self.assertNotEqual(previous_key, current_key)
                self.assertEqual(previous_key, identity(self.plan(source, mode=mode, previous_policy=True)))
                self.assertEqual(current_key, identity(self.plan(source, mode=mode)))


if __name__ == "__main__":
    unittest.main()

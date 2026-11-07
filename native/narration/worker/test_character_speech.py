"""Explicit character intent over the real Windows frontend, without synthesis."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import unittest

from test_text_semantics import descriptor, units
from test_worker import Worker

ROOT = Path(__file__).resolve().parents[3]
CURRENT = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json"


def selected(source, first, last):
    return source.encode("utf-16-le")[2*first:2*last].decode("utf-16-le")


class CharacterSpeechTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def plan(self, source, value=None, language="es", **extra):
        request = dict(text=source, language=language, voice="ef_dora", mathMode="text", readingContext="auto")
        if value is not None:
            request["textSemantics"] = value
        return self.worker.ask("plan", **dict(request, **extra))["segments"]

    @staticmethod
    def spoken(parts):
        return " ".join(part["spokenText"] for part in parts)

    def owners(self, source, parts):
        result = []
        boundaries = {0}; at = 0
        for char in source:
            at += units(char); boundaries.add(at)
        for part in parts:
            self.assertEqual(part["text"], selected(source, part["sourceStart"], part["sourceEnd"]))
            self.assertEqual(len(part["displayedRanges"]), len(part["spokenRanges"]))
            for item in part["displayedRanges"]:
                first = part["sourceStart"]+item["start"]
                last = part["sourceStart"]+item["end"]
                self.assertIn(first, boundaries)
                self.assertIn(last, boundaries)
                self.assertLess(first, last)
                result.append((first, last))
        return result

    def test_character_names_are_independent_of_role_and_leave_other_reading_intents_unchanged(self):
        natural = self.plan("!", descriptor("!", role="data", reading="natural"))
        technical = self.plan("!", descriptor("!", role="data", reading="technical"))
        self.assertEqual("!", self.spoken(natural))
        self.assertIn("negación lógica", self.spoken(technical))
        for role in ("prose", "cpp", "string", "data", "math", "output", "citation"):
            with self.subTest(role=role):
                parts = self.plan("!", descriptor("!", role=role, reading="characters"))
                self.assertEqual("signo de exclamación", self.spoken(parts))
                self.assertEqual({(0, 1)}, set(self.owners("!", parts)))
                self.assertNotEqual(natural[0]["key"], parts[0]["key"])
                self.assertNotEqual(technical[0]["key"], parts[0]["key"])
        self.assertEqual(natural, self.plan("!", descriptor("!", role="data", reading="natural")))
        self.assertEqual(technical, self.plan("!", descriptor("!", role="data", reading="technical")))

    def test_letters_keep_case_and_ascii_digits_are_individual_data_characters_in_each_language(self):
        expected = {
            "es": "ele mayúscula a minúscula ge mayúscula o minúscula signo de exclamación",
            "fr": "elle majuscule a minuscule gé majuscule o minuscule point d’exclamation",
            "en-US": "uppercase el lowercase ay uppercase gee lowercase oh exclamation mark",
        }
        value = descriptor("LaGo!", role="data", reading="characters")
        for language, text in expected.items():
            with self.subTest(language=language):
                parts = self.plan("LaGo!", value, language)
                self.assertEqual(text, self.spoken(parts))
                self.assertEqual({(at, at+1) for at in range(5)}, set(self.owners("LaGo!", parts)))
        self.assertEqual("uno dos", self.spoken(self.plan("12", descriptor("12", role="output", reading="characters"))))
        self.assertEqual("uppercase zed lowercase zed", self.spoken(self.plan("Zz", descriptor("Zz", reading="characters"), "en-GB")))
        self.assertEqual("eñe mayúscula eñe minúscula", self.spoken(self.plan("Ññ", descriptor("Ññ", reading="characters"))))

    def test_literal_punctuation_and_whitespace_are_named_without_operator_meaning(self):
        source = "!+=*/ \t\r\n"
        expected = "signo de exclamación signo más signo igual asterisco barra inclinada espacio tabulación retorno de carro salto de línea"
        parts = self.plan(source, descriptor(source, role="data", reading="characters"))
        self.assertEqual(expected, self.spoken(parts))
        self.assertNotIn("negación lógica", self.spoken(parts))
        self.assertNotIn("asigna", self.spoken(parts))
        self.assertEqual({(at, at+1) for at in range(units(source))}, set(self.owners(source, parts)))
        for language, expected in (("fr", "point d’exclamation espace tabulation"), ("en-US", "exclamation mark space tab")):
            self.assertEqual(expected, self.spoken(self.plan("! \t", descriptor("! \t", reading="characters"), language)))

    def test_unknown_unicode_scalars_keep_exact_u_plus_identity_and_surrogate_owners(self):
        for source in ("😀", "é", "a\u0301"):
            with self.subTest(source=source):
                parts = self.plan(source, descriptor(source, role="data", reading="characters"))
                self.assertIn("Unicode U más", self.spoken(parts))
                at = 0; expected = set()
                for char in source:
                    last = at+units(char); expected.add((at,last)); at = last
                self.assertEqual(expected, set(self.owners(source, parts)))
        parts = self.plan("😀", descriptor("😀", reading="characters"), "en-US")
        self.assertEqual("Unicode U plus one ef six zero zero", self.spoken(parts))
        self.assertEqual({(0,2)}, set(self.owners("😀", parts)))

    def test_only_the_selected_character_changes_and_unmarked_requests_are_not_contaminated(self):
        source = "letras y palabras: ! y más texto."
        first = units(source[:source.index("!")])
        before = self.plan(source)
        value = descriptor(source, role="data", reading="characters", start=first, end=first+1)
        parts = self.plan(source, value)
        spoken = self.spoken(parts)
        self.assertIn("letras y palabras:", spoken)
        self.assertIn("signo de exclamación y más texto.", spoken)
        self.assertNotIn("griega", spoken)
        self.assertNotIn("negación lógica", spoken)
        self.assertEqual(before, self.plan(source))
        with self.assertRaisesRegex(AssertionError, "original semantic projection never supported"):
            self.plan("!", descriptor("!", reading="characters"), semanticProjectionPolicy="original")
        self.assertEqual(before, self.plan(source))

    def test_current_u12l20_option_can_name_its_data_characters_without_changing_the_asset(self):
        raw = CURRENT.read_bytes(); original = json.loads(raw.decode("utf-8-sig")); lesson = deepcopy(original)
        entry = next(item for item in lesson["textSemantics"]["fields"]
                     if item["block"] == "block_0007_0004" and item["path"] == "/options/0")
        owner = next(block for page, layout in zip(lesson["pages"], lesson["stateLayout"]["current"])
                     for block, identity in zip(page["blocks"], layout["blocks"]) if identity == entry["block"])
        source = owner["options"][0]
        self.assertIn("LaGo! conserva !", source)
        positions = {units(source[:at]) for at, char in enumerate(source) if char == "!"}
        spans = []
        for span in entry["spans"]:
            cuts = sorted(at for at in positions if span["start"] <= at < span["end"])
            first = span["start"]
            for at in cuts:
                self.assertEqual("data", span["role"])
                if first < at:
                    spans.append(dict(span, start=first, end=at, text=selected(source,first,at)))
                spans.append(dict(span, start=at, end=at+1, text="!", reading="characters"))
                first = at+1
            if first < span["end"]:
                spans.append(dict(span, start=first, text=selected(source,first,span["end"])))
        entry["spans"] = spans
        value = dict(spans=spans)
        foreground = self.plan(source, value)
        self.assertEqual(2, self.spoken(foreground).count("signo de exclamación"))
        page_index = next(at for at, layout in enumerate(lesson["stateLayout"]["current"]) if entry["block"] in layout["blocks"])
        reply = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=page_index,
            language="es", voice="ef_dora", uiLanguage="es", contentLanguage="es", readingContext="cpp")
        keys = {item["key"] for item in reply["segments"]}
        for part in foreground:
            self.assertIn(part["key"], keys)
        self.assertEqual(original["pages"], lesson["pages"])
        self.assertEqual(original["stateLayout"], lesson["stateLayout"])
        self.assertEqual(hashlib.sha256(raw).hexdigest(), hashlib.sha256(CURRENT.read_bytes()).hexdigest())

    def test_long_character_sequences_split_at_words_without_losing_character_owners(self):
        source = "!" * 512
        parts = self.plan(source, descriptor(source, role="data", reading="characters"))
        self.assertGreater(len(parts), 1)
        self.assertEqual(512, self.spoken(parts).count("signo de exclamación"))
        self.assertEqual({(at, at+1) for at in range(512)}, set(self.owners(source, parts)))


if __name__ == "__main__":
    unittest.main()

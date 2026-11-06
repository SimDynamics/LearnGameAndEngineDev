"""Three exact Windows output speech generations; plan only, no audio or GUI."""
from __future__ import annotations

import hashlib
import json
import unittest

from test_worker import Worker
import test_u12_output_speech as output_speech

UNIT = output_speech.UNIT
speech = output_speech.speech


def digest(parts):
    return hashlib.sha256(json.dumps(parts, ensure_ascii=False, sort_keys=True,
                                    separators=(",", ":")).encode()).hexdigest()


class OutputDataPolicyVersionsTest(unittest.TestCase):
    plan = output_speech.U12DisplayedOutputSpeechTest.plan
    assert_symbol_owner = output_speech.U12DisplayedOutputSpeechTest.assert_symbol_owner

    @classmethod
    def setUpClass(cls):
        cls.paths = [UNIT / ("lesson_" + name + ".json") for name in
                     ("aceptar_registros_y_detectar_duplicados", "clasificar_caracteres_y_normalizar_una_copia")]
        cls.original = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in cls.paths}
        lessons = [json.loads(raw.decode("utf-8-sig")) for raw, _ in cls.original.values()]
        cls.samples = [lessons[0]["pages"][3]["blocks"][2]["text"],
                       lessons[1]["pages"][5]["blocks"][3]["text"],
                       lessons[1]["pages"][6]["blocks"][2]["text"]]
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.worker.close()
        finally:
            for path, (raw, modified) in cls.original.items():
                if path.read_bytes() != raw or path.stat().st_mtime_ns != modified:
                    raise AssertionError(f"Speech planning changed the curriculum: {path}")

    def test_01_b42_and_original_reproduce_measured_full_historical_plans(self):
        # Digests were measured BEFORE this repair against the installed B42
        # worker. They include every prepared string, recording key, source
        # range and word owner, rather than accepting an approximate old voice.
        pins = (("2898babff6408d5818169890ac3827b7e12a4bfa0d80f3301af55f070c3a8436",
                 "2898babff6408d5818169890ac3827b7e12a4bfa0d80f3301af55f070c3a8436"),
                ("84f809460f4aed6469d2335a8ec944d7638e66c53ed85aabf3d583ee0270a8eb",
                 "63316c4323d9cbb89e8056c81e0b7ae5bea3cf367baaa699bce6387ee668eaa3"),
                ("150f2712e1f1662aca1f50cb2e9a6a1d218d50f46d669193da849540344ed7df",
                 "150f2712e1f1662aca1f50cb2e9a6a1d218d50f46d669193da849540344ed7df"))
        for source, expected in zip(self.samples, pins):
            for policy, pin in zip(("b42", "original"), expected):
                with self.subTest(policy=policy, source=source[:80]):
                    parts = self.plan(source, output_policy=policy)
                    self.assertEqual(pin, digest(parts))
                    if policy == "original":
                        self.assertEqual(parts, self.plan(source, previous_policy=True))
                    self.assertEqual(parts, self.plan(source, output_policy=policy))

    def test_02_real_report_equalities_are_data_and_templates_pause_label_colons(self):
        report = self.plan(self.samples[0])
        before = self.plan(self.samples[0], output_policy="b42")
        current_report = next(p for p in report if "Guardados=2;Descartados=2" in p["text"])
        old_report = next(p for p in before if "Guardados=2;Descartados=2" in p["text"])
        self.assertIn("asigna", old_report["spokenText"])
        self.assertEqual(2, current_report["spokenText"].count("igual a"))
        self.assertNotIn("asigna", current_report["spokenText"])
        for source in self.samples[1:]:
            with self.subTest(source=source[:80]):
                parts = self.plan(source)
                relevant = [p for p in parts if "Original:<" in p["text"]]
                self.assertEqual(1, len(relevant))
                self.assertNotIn("dos puntos", relevant[0]["spokenText"])
                # The quoted input remains an actual C++ string literal.
                if '`"CaJa 2."`' in relevant[0]["text"]:
                    self.assertIn("comilla doble CaJa espacio dos punto comilla doble", relevant[0]["spokenText"])
                # Existing raw-value labels keep their exact old preparation.
                old = self.plan(source, output_policy="b42")
                self.assertEqual(len(old), len(parts))
                for a, b in zip(parts, old):
                    self.assertEqual((a["sourceStart"], a["sourceEnd"], a["text"]),
                                     (b["sourceStart"], b["sourceEnd"], b["text"]))
                self.assertEqual(parts, self.plan(source, previous_policy=False))

    def test_03_three_distinct_page_keys_keep_unaffected_neighbor_recordings(self):
        source = ("Primero conserva el dato. La salida es `N=1a|1b`. "
                  "El informe empieza con `Guardados=2;Descartados=2`. Después comprueba el resultado.")
        for mode in ("auto", "text"):
            for context in ("auto", "cpp", "cpp_previous_parentheses"):
                with self.subTest(mode=mode, context=context):
                    versions = {policy: self.plan(source, mode=mode, context=context, output_policy=policy)
                                for policy in ("current", "b42", "original")}
                    self.assertTrue(all(len(parts) == 4 for parts in versions.values()))
                    for index in (0, 3):
                        self.assertEqual(versions["current"][index], versions["b42"][index])
                        self.assertEqual(versions["current"][index], versions["original"][index])
                    for index in (1, 2):
                        owners = {(parts[index]["text"], parts[index]["sourceStart"], parts[index]["sourceEnd"])
                                  for parts in versions.values()}
                        self.assertEqual(1, len(owners))
                    self.assertEqual(versions["current"][1], versions["b42"][1])
                    self.assertEqual(versions["b42"][2], versions["original"][2])
                    keys = {self.worker.ask("identity", page="qa-three-output-generations", language="es",
                                          voice="ef_dora", engine="KOKORO",
                                          texts=[p["preparedText"] for p in parts])["key"]
                            for parts in versions.values()}
                    self.assertEqual(3, len(keys), "A historical policy lost its authentic page identity")
                    self.assertEqual(versions["original"], self.plan(source, mode=mode, context=context, previous_policy=True))

    def test_04_new_contexts_cannot_turn_source_literals_or_numeric_separators_into_prose(self):
        unchanged = ("La instrucción `total = 3;` actualiza el valor.",
                     "El informe presenta la instrucción `total = 3;`.",
                     "El informe presenta la variable `total = 3`.",
                     "El informe presenta `std::cout << 3;`.",
                     "El informe presenta la cadena `\"Inicio: 7\"`.",
                     "El identificador `presenta` precede a la instrucción `total = 3;`.",
                     "El informe empieza con `R:r:R:#`.",
                     "El informe empieza con `3:2:1`.",
                     "En líneas separadas, Entero: 7 y Decimal: 7.5.",
                     "La salida es `Llaves: 11`.")
        for source in unchanged:
            with self.subTest(source=source):
                current = self.plan(source)
                self.assertEqual(current, self.plan(source, output_policy="b42"))
                self.assertEqual(current, self.plan(source, output_policy="original"))
                if source.startswith("En líneas separadas"):
                    self.assertNotIn("dos puntos", speech(current).casefold(),
                                     "An ordinary result enumeration named its label punctuation as code")
                    self.assertIn("Entero:", speech(current))
                    self.assertIn("Decimal:", speech(current))
        self.assertIn("dos puntos", speech(self.plan("El signo `:` separa los campos.")).casefold(),
                      "An explicit lesson about the colon lost the sign's technical name")
        for source in ("El informe empieza con `Guardados=2;Descartados=2`.",
                       "El archivo contiene `Guardados=2;Descartados=2`.",
                       "La cadena resultante es `Guardados=2;Descartados=2`."):
            with self.subTest(data=source):
                for prefix in ("", "𐐀 "):
                    parts = self.plan(prefix + source)
                    self.assertNotIn("asigna", speech(parts))
                    for at, value in enumerate(prefix + source):
                        if value == "=":
                            self.assert_symbol_owner(prefix + source, parts, at, "igual")

    def test_05_new_preparation_matches_background_and_does_not_escape_request_scope(self):
        long_source = ("𐐀 Conserva " + "cada dato validado y su orden " * 100 +
                       "sin cambios. El informe empieza con `Guardados=2;Descartados=2`.")
        self.assertGreater(len(long_source), 2000)
        manifest = self.worker.ask("first_unit_manifest")["manifest"]
        for source in (self.samples[0], self.samples[1], self.samples[2], long_source):
            current = self.plan(source)
            for policy in ("original", "b42", "current"):
                self.plan(source, output_policy=policy)
                self.assertEqual(current, self.plan(source))
            lesson = {"pages": [{"blocks": [{"type": "paragraph", "text": source, "mathMode": "auto"}]}]}
            background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson, ensure_ascii=False),
                                         page=0, language="es", uiLanguage="es", voice="ef_dora")["segments"]
            self.assertEqual([(p["preparedText"], p["key"]) for p in current],
                             [(p["preparedText"], p["key"]) for p in background])
        self.assertEqual(manifest, self.worker.ask("first_unit_manifest")["manifest"],
                         "A background-only segmentation scope escaped into historical pack export")

    def test_06_protocol_rejects_unknown_conflicting_and_non_plan_policy(self):
        invalid = ({"outputDataPolicy": "unknown"}, {"outputDataPolicy": ""}, {"outputDataPolicy": True},
                   {"outputDataPolicy": None}, {"previousOutputDataPolicy": "true"},
                   {"outputDataPolicy": "b42", "previousOutputDataPolicy": True})
        for fields in invalid:
            with self.subTest(fields=fields):
                identity = self.worker.send("plan", text="Dato.", language="es", **fields)
                reply = self.worker.receive()
                self.assertEqual(identity, reply["id"])
                self.assertIn("error", reply)
        for field in ("outputDataPolicy", "previousOutputDataPolicy"):
            identity = self.worker.send("identity", page="qa-reject-policy", texts=["Dato."],
                                        language="es", voice="ef_dora", **{field: "b42" if field == "outputDataPolicy" else True})
            reply = self.worker.receive()
            self.assertEqual(identity, reply["id"])
            self.assertIn("error", reply)
        self.assertEqual(self.plan("La salida es `N=1a|1b`."),
                         self.plan("La salida es `N=1a|1b`.", output_policy="current"))


if __name__ == "__main__":
    unittest.main()

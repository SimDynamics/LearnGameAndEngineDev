"""Background preparation uses the original script and unchanged cache keys."""
import json
import unittest
from test_worker import Worker, CACHE


class BackgroundPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker()

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def test_script_excludes_validator_code_and_incorrect_feedback(self):
        lesson = {"title": "Test", "pages": [{"blocks": [
            {"type": "paragraph", "text": "Une phrase continue."},
            {"type": "code", "code": "NEVER_SPEAK_CODE"},
            {"type": "multiple_choice", "question": "Choisissez une réponse.",
             "options": ["Première", "Deuxième"], "correctAnswer": "NEVER_SPEAK_ANSWER",
             "explanation": "Une explication correcte.", "reviewFeedback": "NEVER_SPEAK_INCORRECT"}]}]}
        plan = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                               language="fr", voice="ff_siwis", uiLanguage="fr")
        spoken = " ".join(s["text"] for s in plan["segments"])
        self.assertNotIn("NEVER_SPEAK", spoken)
        self.assertLess(spoken.index("Première"), spoken.index("Deuxième"))
        self.assertIn("explication correcte", spoken)

    def test_real_audio_cache_shared_with_visible_source_plan(self):
        for language, voice, source in [
            ("es", "em_santa", "Esta página conserva la voz seleccionada."),
            ("fr", "ff_siwis", "Cette page conserve la voix choisie."),
            ("en-US", "af_heart", "This page keeps the selected voice."),
            ("en-GB", "bf_emma", "This page keeps the selected voice.")]:
            with self.subTest(language=language):
                lesson = {"pages": [{"blocks": [{"type": "paragraph", "text": source}]}]}
                background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                                             language=language, voice=voice, uiLanguage=language)["segments"]
                self.assertEqual(1, len(background))
                before = self.worker.ask("prepare", text=background[0]["text"],
                    preparedText=background[0]["preparedText"], language=language, voice=voice,
                    outputDirectory=str(CACHE), threads=2, audioOnly=True)
                from pathlib import Path
                import hashlib
                wav = Path(before["wav"])
                digest = hashlib.sha256(wav.read_bytes()).digest()
                stamp = wav.stat().st_mtime_ns
                visible = self.worker.ask("plan", text=source, language=language, voice=voice)["segments"][0]
                self.assertEqual(background[0]["key"], visible["key"])
                after = self.worker.ask("prepare", text=visible["text"],preparedText=visible["preparedText"],
                    language=language,voice=voice,outputDirectory=str(CACHE),threads=2)
                self.assertTrue(after["cacheHit"])
                self.assertEqual(digest,hashlib.sha256(wav.read_bytes()).digest())
                self.assertEqual(stamp,wav.stat().st_mtime_ns)
                self.assertGreater(len(after["timings"]),2)

    def test_technical_and_math_scripts_keep_visible_reader_cache_identity(self):
        for language,voice,source in [
            ("es","em_santa","La línea #include <iostream> permite usar std::cout; el resultado es 6 × 100 = 600."),
            ("fr","ff_siwis","La ligne #include <iostream> permet d'utiliser std::cout ; le résultat est 6 × 100 = 600."),
            ("en-US","af_heart","The line #include <iostream> allows std::cout; the result is 6 × 100 = 600."),
            ("en-GB","bf_emma","The line #include <iostream> allows std::cout; the result is 6 × 100 = 600.")]:
            with self.subTest(language=language):
                lesson={"pages":[{"blocks":[{"type":"paragraph","text":source}]}]}
                background=self.worker.ask("curriculum_plan",lessonJson=json.dumps(lesson),page=0,
                    language=language,voice=voice,uiLanguage=language)["segments"]
                visible=self.worker.ask("plan",text=source,language=language,voice=voice)["segments"]
                self.assertEqual([s["key"] for s in visible],[s["key"] for s in background])


if __name__ == "__main__":
    unittest.main()

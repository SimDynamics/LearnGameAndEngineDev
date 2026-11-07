"""Exercise typed feedback provenance through the real Windows bridge, without audio/UI."""
import copy
import json
import unittest
from test_worker import Worker


def selected(source, first, last):
    return source.encode("utf-16-le")[first * 2:last * 2].decode("utf-16-le")


class MathFeedbackSourcesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def block(self):
        return dict(type="math_puzzle", prompt="Elige.", responses=[dict(id="q",
            type="math_multiple_choice_single", label="  😀 Pregunta 1  ",
            options=[dict(id="a", label="A", reviewFeedback="  Revisa 3:2  "),
                     dict(id="b", label="B", reviewFeedback="HIDDEN_REVIEW")],
            validator=dict(type="choice", expected=["b"]))],
            feedback=dict(correct="Resultado: 7", incorrect="Guía general"))

    def evaluate(self, block, answer):
        before = copy.deepcopy(block)
        reply = self.worker.ask("validate_math", blockJson=json.dumps(block, ensure_ascii=False),
            responsesJson=json.dumps(answer), language="es")
        self.assertEqual(before, block)
        return reply

    def origins(self, reply):
        sources = reply["feedbackSources"]
        previous = 0
        for item in sources:
            self.assertGreaterEqual(item["start"], previous)
            self.assertGreater(item["end"], item["start"])
            previous = item["end"]
        return [(item["path"], selected(reply["feedback"], item["start"], item["end"])) for item in sources]

    def test_incorrect_review_retains_only_typed_selected_sources_with_unicode(self):
        reply = self.evaluate(self.block(), {"q": {"type": "choice", "selected": ["a"]}})
        self.assertTrue(reply["evaluable"])
        self.assertFalse(reply["correct"])
        self.assertEqual([("/responses/0/label", "😀 Pregunta 1"),
                         ("/responses/0/options/0/reviewFeedback", "Revisa 3:2")], self.origins(reply))
        self.assertNotIn("HIDDEN_REVIEW", reply["feedback"])
        self.assertNotIn("Guía general", reply["feedback"])

    def test_correct_guidance_origins_keep_the_canonical_raw_block_fingerprint(self):
        block = self.block()
        answer = {"q": {"type": "choice", "selected": ["b"]}}
        reply = self.evaluate(block, answer)
        self.assertTrue(reply["correct"])
        self.assertEqual([("/feedback/correct", "Resultado: 7")], self.origins(reply))
        # This bridge still fingerprints the original raw block. Editorial
        # changes are not a semantic overlay and retain their previous rules.
        self.assertEqual("4bcc15fbd2356f7fdb1c859f08327a62486d010c174dbdf1c820246ea9e076f9",
                         reply["contentFingerprint"])
        changed = copy.deepcopy(block)
        changed["responses"][0]["label"] = "Otra pregunta"
        changed["feedback"]["correct"] = "Otro resultado"
        updated = self.evaluate(changed, answer)
        self.assertNotEqual(reply["contentFingerprint"], updated["contentFingerprint"])
        self.assertEqual([("/feedback/correct", "Otro resultado")], self.origins(updated))

    def test_standalone_response_uses_original_field_paths(self):
        source = self.block()
        block = source["responses"][0]
        block.pop("id")
        block["question"] = "Elige."
        block["feedback"] = source["feedback"]
        reply = self.evaluate(block, {"response": {"type": "choice", "selected": ["a"]}})
        self.assertFalse(reply["correct"])
        self.assertEqual([("/label", "😀 Pregunta 1"), ("/options/0/reviewFeedback", "Revisa 3:2")], self.origins(reply))

    def test_incomplete_answer_exposes_typed_label_without_any_selected_review(self):
        block = self.block()
        reply = self.evaluate(block, {})
        self.assertFalse(reply["evaluable"])
        self.assertFalse(reply["correct"])
        self.assertEqual([("/responses/0/label", "😀 Pregunta 1")], self.origins(reply))
        self.assertNotIn("Revisa 3:2", reply["feedback"])
        self.assertNotIn("HIDDEN_REVIEW", reply["feedback"])
        self.assertNotIn("Resultado: 7", reply["feedback"])
        # This descriptor also serves the native incomplete-answer branch.
        # It changes presentation/reading, never the response or verdict.
        label = reply["feedbackSources"][0]
        marked = {"spans": [dict(start=label["start"], end=label["end"],
            text="😀 Pregunta 1", role="output", reading="natural")]}
        plan = self.worker.ask("plan", text=reply["feedback"], language="es", mathMode="auto",
            readingContext="cpp", textSemantics=marked)
        self.assertTrue(plan["segments"])
        for segment in plan["segments"]:
            self.assertEqual(segment["text"], selected(reply["feedback"], segment["sourceStart"], segment["sourceEnd"]))


if __name__ == "__main__":
    unittest.main()

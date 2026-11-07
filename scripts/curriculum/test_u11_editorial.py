"""The two U11L1 editorial revisions must not license another response contract."""
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BEFORE = "aa230c6575210c2154e59dee60ff87cdeb8e615b"
AFTER = "b3d9a796d0d4536b99a22ce88987f95cb0d922d8"
RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
            "texto_interpretacion_y_flujos/lesson_palabras_y_lecturas_condicionadas.json")
QUESTION_HASHES = (
    "5c5e64db87dbf931030218aa78252bc0a030589e3caa563ab561728525cd94e6",
    "ea7b37861cd558227733f8d151dabcaccbe3c65c0f3b593f33c66eb99a13f207",
)
OUTPUT_HASHES = (
    "692e125a7d8b75cdf3abe9c2fddcb1ad462ffc93a6d261cce761a43a45bd747a",
    "b27ec986ffb0d8f18c2b84772251abd95f9d49e5e01fe391f54245ad1ccf16b5",
)
EXPLANATION = ("Los seis informes distinguen texto vacío, sólo separadores y palabras con o sin espacios entre ellas. "
               "El contador registra únicamente las lecturas válidas de cada flujo nuevo.")


@lru_cache(maxsize=None)
def published_bytes(revision, locale="es"):
    relative = RELATIVE.replace("/es/", "/" + locale + "/")
    return subprocess.check_output(["git", "show", revision + ":" + relative], cwd=contract.ROOT)


def published(revision, locale="es"):
    # Parse afresh: a mutation in one negative cannot contaminate a later case.
    return json.loads(published_bytes(revision, locale))


def text_hash(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class U11EditorialTest(unittest.TestCase):
    def setUp(self):
        self.before, self.after = published(BEFORE), published(AFTER)
        self.old_question = deepcopy(self.before["pages"][1]["blocks"][1])
        self.new_question = deepcopy(self.after["pages"][1]["blocks"][1])
        self.old_compiler = deepcopy(self.before["pages"][4]["blocks"][3])
        self.new_compiler = deepcopy(self.after["pages"][4]["blocks"][3])

    def question_preserved(self, old=None, new=None, relative=RELATIVE, page=1, block=1):
        return contract.preserves_previous_exercise(
            relative, page, block, self.old_question if old is None else old,
            self.new_question if new is None else new)

    def compiler_preserved(self, old=None, new=None, relative=RELATIVE, page=4, block=3):
        return contract.preserves_previous_compiler(
            relative, page, block, self.old_compiler if old is None else old,
            self.new_compiler if new is None else new, self.before)

    def test_exact_published_question_changes_only_reviewed_words_and_optional_guidance(self):
        self.assertEqual(QUESTION_HASHES, tuple(text_hash(block["question"])
                                              for block in (self.old_question, self.new_question)))
        self.assertEqual({"question", "reviewFeedback"}, {
            key for key in self.old_question.keys() | self.new_question.keys()
            if self.old_question.get(key) != self.new_question.get(key)})
        self.assertNotIn("id", self.old_question)
        self.assertNotIn("id", self.new_question)
        self.assertTrue(self.question_preserved())
        for field in ("type", "language", "code", "answers", "explanation"):
            self.assertEqual(self.old_question[field], self.new_question[field])
        question = self.new_question["question"]
        self.assertIn(r"`\t`", question)
        self.assertIn(r"`\n`", question)
        self.assertNotIn("\t", question)
        self.assertNotIn("\n", question)
        self.assertIn(r'entrada{"\tbrisa\npuente camino"}', self.new_question["code"])
        self.assertEqual(["1:<brisa>\n2:<puente>"], self.new_question["answers"])

    def test_question_requires_both_reviewed_texts_and_transition_direction(self):
        for side in ("old", "new"):
            old, new = deepcopy(self.old_question), deepcopy(self.new_question)
            (old if side == "old" else new)["question"] += " Another wording."
            with self.subTest(side=side):
                self.assertFalse(self.question_preserved(old, new))
        self.assertFalse(self.question_preserved(self.new_question, self.old_question))

    def test_question_is_confined_to_its_original_lesson_slot_and_type(self):
        for relative, page, block in (
            (RELATIVE.replace("lesson_palabras", "lesson_otras_palabras"), 1, 1),
            (RELATIVE, 0, 1), (RELATIVE, 2, 1), (RELATIVE, 1, 0), (RELATIVE, 1, 2),
        ):
            with self.subTest(relative=relative, page=page, block=block):
                self.assertFalse(self.question_preserved(relative=relative, page=page, block=block))
        for kind in ("fill_code", "code"):
            old, new = dict(self.old_question, type=kind), dict(self.new_question, type=kind)
            self.assertFalse(self.question_preserved(old, new))
        # The helper's path projection rejects paths outside the Spanish root.
        with self.assertRaises(ValueError):
            self.question_preserved(relative=RELATIVE.replace("/es/", "/en/"))

    def test_question_cannot_borrow_the_words_for_an_added_or_null_identity(self):
        for identity in ("different_prediction", None, 17):
            with self.subTest(identity=identity):
                self.assertFalse(self.question_preserved(new=dict(self.new_question, id=identity)))
                self.assertFalse(self.question_preserved(dict(self.old_question, id=identity),
                                                         dict(self.new_question, id=identity)))

    def test_question_source_answers_comparison_and_existing_explanation_stay_protected(self):
        for field, value in (
            ("code", self.new_question["code"] + "\n// different source"),
            ("answers", ["different answer"]), ("language", "cmake"),
            ("outputComparison", "exact"),
            ("explanation", self.new_question["explanation"] + " Another explanation."),
            ("unknownField", "unreviewed"),
        ):
            with self.subTest(field=field):
                self.assertFalse(self.question_preserved(new=dict(self.new_question, **{field: value})))

    def test_exact_compiler_revision_adds_only_the_first_optional_explanation(self):
        self.assertEqual(OUTPUT_HASHES, tuple(layout.fingerprint(block["outputCheck"])
                                            for block in (self.old_compiler, self.new_compiler)))
        self.assertNotIn("id", self.old_compiler)
        self.assertNotIn("id", self.new_compiler)
        self.assertEqual(self.old_compiler["initialCode"], self.new_compiler["initialCode"])
        old_case, new_case = (block["outputCheck"]["cases"][0]
                              for block in (self.old_compiler, self.new_compiler))
        self.assertNotIn("explanation", old_case)
        self.assertEqual(EXPLANATION, new_case["explanation"])
        self.assertEqual({**old_case, "explanation": EXPLANATION}, new_case)
        expected = deepcopy(self.old_compiler)
        expected["outputCheck"]["cases"][0]["explanation"] = EXPLANATION
        self.assertEqual(expected, self.new_compiler)
        # Six reports are in ONE literal stdout answer, not six test cases.
        self.assertEqual(1, len(self.old_compiler["outputCheck"]["cases"]))
        self.assertEqual(1, len(self.new_compiler["outputCheck"]["cases"]))
        self.assertEqual(6, new_case["outputs"][0].count("Palabras:"))
        self.assertEqual(old_case["standardInput"], new_case["standardInput"])
        self.assertEqual(old_case["outputs"], new_case["outputs"])
        self.assertTrue(self.compiler_preserved())

    def test_compiler_guidance_is_general_but_cannot_change_existing_identity_or_type(self):
        for relative, page, block in (
            (RELATIVE.replace("/es/", "/fr/"), 4, 3),
            (RELATIVE.replace("lesson_palabras", "lesson_otras_palabras"), 4, 3),
            (RELATIVE, 3, 3), (RELATIVE, 5, 3), (RELATIVE, 4, 2), (RELATIVE, 4, 4),
        ):
            with self.subTest(relative=relative, page=page, block=block):
                self.assertTrue(self.compiler_preserved(relative=relative, page=page, block=block))
        for identity in ("different_editor", None, 17):
            self.assertFalse(self.compiler_preserved(new=dict(self.new_compiler, id=identity)))
        self.assertTrue(self.compiler_preserved(dict(self.old_compiler, id="same_editor"),
                                                dict(self.new_compiler, id="same_editor")))
        self.assertFalse(self.compiler_preserved(new=dict(self.new_compiler, type="code")))

    def test_compiler_explanations_can_be_revised_or_removed_without_changing_execution(self):
        for original_explanation in ("different old text",):
            old = deepcopy(self.old_compiler)
            old["outputCheck"]["cases"][0]["explanation"] = original_explanation
            self.assertTrue(self.compiler_preserved(old=old))
        for explanation in (EXPLANATION + ".", "different new text"):
            new = deepcopy(self.new_compiler)
            new["outputCheck"]["cases"][0]["explanation"] = explanation
            self.assertTrue(self.compiler_preserved(new=new))
        self.assertTrue(self.compiler_preserved(self.new_compiler, self.old_compiler))
        self.assertTrue(self.compiler_preserved(self.new_compiler, self.new_compiler))

    def test_compiler_source_stdout_stdin_and_comparisons_remain_exact(self):
        self.assertFalse(self.compiler_preserved(new=dict(self.new_compiler, initialCode="int main() {}")))
        for field, value in (
            ("standardInput", "different input"), ("outputs", ["different stdout"]),
            ("outputs", self.new_compiler["outputCheck"]["cases"][0]["outputs"] + ["extra accepted output"]),
            ("target", "another_target"), ("standardError", "nonEmpty"),
        ):
            with self.subTest(field=field, value=value):
                new = deepcopy(self.new_compiler)
                new["outputCheck"]["cases"][0][field] = value
                self.assertFalse(self.compiler_preserved(new=new))
        for field, value in (("inputComparison", "tokens"), ("comparison", "trimmed"), ("unknownField", "unreviewed")):
            new = deepcopy(self.new_compiler)
            new["outputCheck"][field] = value
            self.assertFalse(self.compiler_preserved(new=new))

    def test_compiler_guidance_can_follow_another_case_without_adding_or_removing_cases(self):
        first = deepcopy(self.old_compiler["outputCheck"]["cases"][0])
        first["standardInput"] = "different prepared input"
        for index in (0, 1):
            old, new = deepcopy(self.old_compiler), deepcopy(self.new_compiler)
            old["outputCheck"]["cases"].insert(index, deepcopy(first))
            new["outputCheck"]["cases"].insert(index, deepcopy(first))
            self.assertTrue(self.compiler_preserved(old, new))
        for cases in ([], self.new_compiler["outputCheck"]["cases"] * 2):
            new = deepcopy(self.new_compiler)
            new["outputCheck"]["cases"] = deepcopy(cases)
            self.assertFalse(self.compiler_preserved(new=new))

    def test_v2_adoption_preserves_addresses_origins_answers_and_original_translations(self):
        self.assertNotIn("stateLayout", self.before)
        layout.validate_transition(self.before, self.after)
        self.assertEqual(layout.validate(self.before), layout.validate(self.after))
        prepared = layout.prepare(self.before)
        for field in ("origin", "current"):
            self.assertEqual(prepared["stateLayout"][field], self.after["stateLayout"][field])
        self.assertEqual([], self.after["stateLayout"]["additions"])
        for locale in ("es", "en", "fr"):
            source = published(BEFORE, locale)
            manifest = self.after["stateLayout"]["family"][locale]
            self.assertEqual(layout.prepare(source)["stateLayout"]["origin"], manifest["origin"])
            self.assertEqual(layout.fingerprint(layout._without_layout(source)), manifest["sha256"])
            if locale != "es":
                self.assertEqual(published_bytes(BEFORE, locale), published_bytes(AFTER, locale))
        for old, new in ((self.old_question, self.new_question), (self.old_compiler, self.new_compiler)):
            self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
            self.assertNotEqual(layout.fingerprint(old), layout.fingerprint(new))

    def test_both_revisions_still_need_their_complete_editorial_proof(self):
        for identity in ("block_0001_0001", "block_0004_0003"):
            new = deepcopy(self.after)
            old_proofs = new["stateLayout"]["editorial"]
            new["stateLayout"]["editorial"] = [proof for proof in old_proofs if proof["id"] != identity]
            self.assertLess(len(new["stateLayout"]["editorial"]), len(old_proofs))
            with self.subTest(identity=identity), self.assertRaises(layout.LayoutError):
                layout.validate_transition(self.before, new)

    def test_state_layout_rejects_changed_answer_source_origin_or_language_family(self):
        for change in ("answer", "source", "stdout", "origin", "family"):
            with self.subTest(change=change):
                new = deepcopy(self.after)
                if change == "answer":
                    new["pages"][1]["blocks"][1]["answers"][0] = "different answer"
                elif change == "source":
                    new["pages"][4]["blocks"][3]["initialCode"] += "\n// different source"
                elif change == "stdout":
                    new["pages"][4]["blocks"][3]["outputCheck"]["cases"][0]["outputs"][0] += "different output"
                elif change == "origin":
                    new["stateLayout"]["origin"][1]["blocks"][1]["sha256"] = "0" * 64
                else:
                    new["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(self.before, new)


if __name__ == "__main__":
    unittest.main()

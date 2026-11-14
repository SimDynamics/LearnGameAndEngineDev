"""Real U12L6/U12L22 ordinary editorial authoring, never editing curriculum.

Work owns the exact wording and technical narration spans. Both candidate
updates are prepared only in memory; the published sources and state remain
byte-exact. The usual curriculum validator must accept their old addresses.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import stable_state_layout as state
import validar_json_programacion as contract

ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content"
CASES = {
    "formatting": {
        "relative": ("archivos_y_programa_completo/"
                     "lesson_estado_del_formato_y_flujos_independientes.json"),
        "identity": "block_0002_0002",
    },
    "group": {
        "relative": ("archivos_y_programa_completo/"
                     "lesson_leer_un_grupo_con_cierre_propio.json"),
        "identity": "block_0001_0002",
    },
}


def family(name: str) -> dict[str, dict]:
    lesson = "programming/cpp_fundamentals/" + CASES[name]["relative"]
    return {
        locale: json.loads((CONTENT / locale / lesson).read_text(encoding="utf-8"))
        for locale in ("es", "en", "fr")
    }


def authored_fields(document: dict, name: str) -> dict:
    identity = CASES[name]["identity"]
    owner = state._blocks(document)[identity]
    if name == "group":
        previous = "Predice la salida. Este ejemplo ya comprobó que hay caracteres disponibles."
        replacement = ("Predice la salida. La cadena mostrada contiene una llave "
                       "de cierre después de dos espacios.")
        assert owner["question"] == previous
        return {"question": {"text": replacement, "spans": []}}

    assert name == "formatting"
    changes = {
        "texto << hex << 31": "texto << std::hex << 31",
        "texto << dec": "texto << std::dec",
    }
    edits = {}
    for field in ("question", "explanation"):
        original = owner[field]
        revised = original
        for before, after in changes.items():
            assert revised.count(before) == 1
            revised = revised.replace(before, after, 1)
        markers = [
            state.text_semantics.select(revised, "texto << std::hex << 31",
                                        "cpp", reading="technical"),
            state.text_semantics.select(revised, "texto << std::dec",
                                        "cpp", reading="technical"),
        ]
        if field == "explanation":
            markers.extend([
                state.text_semantics.select(revised, "1f", "output",
                                            reading="natural", occurrence=1),
                state.text_semantics.select(revised, "1f", "output",
                                            reading="natural", occurrence=2),
            ])
        edits[field] = {"text": revised, "spans": sorted(markers, key=lambda s: s["start"])}
    # The complete fully-qualified block already appeared as revision 3 of
    # this same real U12L6 identity. It was later returned to revision 2.
    # Restore it explicitly instead of forging a new editorial cycle.
    edits["returnTo"] = 3
    return edits


def proposal(name: str) -> tuple[dict, dict, dict]:
    documents = family(name)
    before = documents["es"]
    identity = CASES[name]["identity"]
    state.validate(before)
    after = state.revise_ordinary_editorial(before, identity, authored_fields(before, name))
    return before, after, documents


class PublishedU12QuestionTest(unittest.TestCase):
    def test_both_authored_real_edits_keep_their_original_answers_and_narration(self):
        for name in CASES:
            with self.subTest(lesson=name):
                before, after, documents = proposal(name)
                identity = CASES[name]["identity"]
                old, revised = state._blocks(before)[identity], state._blocks(after)[identity]
                self.assertEqual({k: v for k, v in old.items()
                                  if k not in ("question", "explanation")},
                                 {k: v for k, v in revised.items()
                                  if k not in ("question", "explanation")})
                edits = authored_fields(before, name)
                self.assertEqual({field: revised[field] for field in edits if field != "returnTo"},
                                 {field: spec["text"] for field, spec in edits.items()
                                  if field != "returnTo"})
                self.assertEqual(state.validate(before), state.validate(after))
                state.validate_transition(before, after)
                self.assertEqual(before["stateLayout"]["origin"], after["stateLayout"]["origin"])
                self.assertEqual(before["stateLayout"]["current"], after["stateLayout"]["current"])
                self.assertEqual(before["stateLayout"]["family"], after["stateLayout"]["family"])
                self.assertEqual(before["stateLayout"]["editorial"],
                                 after["stateLayout"]["editorial"][:-1])
                proof = {"id": identity, "beforeSha256": state.fingerprint(old),
                         "afterSha256": state.fingerprint(revised)}
                if "returnTo" in edits:
                    proof["returnTo"] = {
                        "revision": edits["returnTo"],
                        "historySha256": state.fingerprint({
                            "lessonId": before["id"], "locale": "es",
                            "editorial": before["stateLayout"]["editorial"]}),
                        "previousBlock": old}
                self.assertEqual(after["stateLayout"]["editorial"][-1], proof)
                for path, spec in edits.items():
                    if path == "returnTo":
                        continue
                    result = state.text_semantics.validate(after)[identity]["/" + path]
                    self.assertEqual(result["source"], spec["text"])
                    self.assertEqual(result["spans"], spec["spans"])
                current = after["stateLayout"]["current"]
                position = next((p, b) for p, page in enumerate(current)
                                for b, known in enumerate(page["blocks"]) if known == identity)
                historical = state.validate(before)[position]
                asset = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                         + CASES[name]["relative"])
                self.assertTrue(contract.preserves_previous_exercise(
                    asset, *historical, old, revised,
                    previous_document=before, current_document=after))
                state.check_localized_family([after, documents["en"], documents["fr"]],
                                             ["es", "en", "fr"])
                for locale in ("es", "en", "fr"):
                    self.assertEqual(documents[locale], family(name)[locale])

    def test_reject_changes_to_answer_other_guides_identity_and_authored_markers(self):
        for name in CASES:
            before, after, _ = proposal(name)
            identity = CASES[name]["identity"]
            source = state._blocks(before)[identity]
            new = state._blocks(after)[identity]
            asset = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                     + CASES[name]["relative"])
            current = after["stateLayout"]["current"]
            position = next((p, b) for p, page in enumerate(current)
                            for b, known in enumerate(page["blocks"]) if known == identity)
            historical = state.validate(before)[position]
            alterations = []
            answer = deepcopy(after)
            key = "correctIndex" if name == "formatting" else "answers"
            state._blocks(answer)[identity][key] = (
                0 if name == "formatting" else ["FALSO"])
            alterations.append(("answer", answer))
            guide = deepcopy(after)
            if "reviewFeedback" in new:
                if isinstance(new["reviewFeedback"], list):
                    state._blocks(guide)[identity]["reviewFeedback"][0] += " Cambio indebido."
                else:
                    state._blocks(guide)[identity]["reviewFeedback"] += " Cambio indebido."
                alterations.append(("guide", guide))
            missing = deepcopy(after)
            missing["textSemantics"]["fields"] = [
                field for field in missing["textSemantics"]["fields"]
                if (field["block"], field["path"]) != (identity, "/question")]
            alterations.append(("missing_question_marker", missing))
            stale = deepcopy(after)
            entry = next(f for f in stale["textSemantics"]["fields"]
                         if (f["block"], f["path"]) == (identity, "/question"))
            entry["sourceSha256"] = "0" * 64
            alterations.append(("stale_question_marker", stale))
            wrong_owner = deepcopy(after)
            wrong_owner["stateLayout"]["editorial"][-1]["id"] = "block_0000_0000"
            alterations.append(("wrong_owner", wrong_owner))
            for label, changed in alterations:
                with self.subTest(name=name, mutation=label):
                    old_block, changed_block = source, state._blocks(changed)[identity]
                    self.assertFalse(contract.preserves_previous_exercise(
                        asset, *historical, old_block, changed_block,
                        previous_document=before, current_document=changed))

    def test_authoring_requires_explicit_nonempty_compatible_edits(self):
        for name in CASES:
            before = family(name)["es"]
            identity = CASES[name]["identity"]
            spec = authored_fields(before, name)
            for altered in (
                {},
                {"question": {"text": "  ", "spans": []}},
                {"question": {"text": state._blocks(before)[identity]["question"], "spans": []}},
                {"code": {"text": "something", "spans": []}},
                {"question": {"text": "Otra pregunta"}},
            ):
                with self.subTest(name=name, altered=altered), self.assertRaises(state.LayoutError):
                    state.revise_ordinary_editorial(before, identity, altered)
            if name == "formatting":
                missing_return = deepcopy(spec)
                del missing_return["returnTo"]
                with self.assertRaisesRegex(state.LayoutError, "declare its exact returnTo"):
                    state.revise_ordinary_editorial(before, identity, missing_return)
                incorrect_return = deepcopy(spec)
                incorrect_return["returnTo"] = 0
                with self.assertRaisesRegex(state.LayoutError, "declare its exact returnTo"):
                    state.revise_ordinary_editorial(before, identity, incorrect_return)
                unmarked = deepcopy(spec)
                unmarked["question"]["spans"] = []
                with self.assertRaises(state.LayoutError):
                    state.revise_ordinary_editorial(before, identity, unmarked)
                misaligned = deepcopy(spec)
                misaligned["explanation"]["spans"][0]["start"] += 1
                with self.assertRaises((state.LayoutError, ValueError)):
                    state.revise_ordinary_editorial(before, identity, misaligned)

    def test_public_cli_outputs_new_file_and_rejects_overwrite(self):
        before = family("group")["es"]
        edits = authored_fields(before, "group")
        with tempfile.TemporaryDirectory(prefix="lgaed-u12-question-cli-") as directory:
            folder = Path(directory)
            source = folder / "original.json"
            spec_file = folder / "authored.json"
            dest = folder / "revised.json"
            source.write_text(json.dumps(before, ensure_ascii=False), encoding="utf-8")
            spec_file.write_text(json.dumps(edits, ensure_ascii=False), encoding="utf-8")
            baseline = source.read_bytes()
            command = [
                sys.executable, str(ROOT / "scripts/curriculum/stable_state_layout.py"),
                "revise-ordinary-editorial", str(source), "--previous", str(source),
                "--identity", CASES["group"]["identity"],
                "--editorial-fields", str(spec_file), "--output", str(dest)]
            success = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(success.returncode, 0, success.stderr)
            self.assertEqual(source.read_bytes(), baseline)
            expected = state.revise_ordinary_editorial(before, CASES["group"]["identity"], edits)
            self.assertEqual(json.loads(dest.read_text(encoding="utf-8")), expected)
            repeat = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(repeat.returncode, 0)
            self.assertEqual(json.loads(dest.read_text(encoding="utf-8")), expected)


if __name__ == "__main__":
    unittest.main()

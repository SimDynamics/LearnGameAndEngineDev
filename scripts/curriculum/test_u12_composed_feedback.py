"""Two exact U12L4 editorial pairs preserve sources, objectives and state."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import stable_state_layout as layout
import u12_composed_feedback_fixture_source as fixture
import validar_json_programacion as contract


class U12ComposedFeedbackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before, cls.after, cls.family = fixture.candidate()
        cls.qa = fixture.ROOT / "out" / "qa" / "u12-composed-feedback-python"
        cls.qa.mkdir(parents=True, exist_ok=True)

    def pair(self, identity):
        return tuple(deepcopy(layout._blocks(document)[identity]) for document in (self.before, self.after))

    def compiler(self, old, new, relative=fixture.RELATIVE, page=2, block=3, lesson=None):
        return contract.preserves_previous_compiler(relative, page, block, old, new, self.before if lesson is None else lesson)

    def prediction(self, old, new, relative=fixture.RELATIVE, page=4, block=1):
        return contract.preserves_previous_exercise(relative, page, block, old, new)

    def test_question_needs_its_exact_spanish_owner_field_and_reviewed_text_pair(self):
        old, new = self.pair(fixture.PREDICTION)
        self.assertEqual(fixture.QUESTION_HASHES, tuple(hashlib.sha256(value["question"].encode()).hexdigest() for value in (old, new)))
        self.assertTrue(self.prediction(old, new))
        key = (fixture.LESSON, 5, 2, "output_prediction", "question", fixture.ACTIVITY_ID)
        approvals = dict(contract.WORK_EXACT_EDITORIAL_REVISIONS)
        self.assertEqual(fixture.QUESTION_HASHES, approvals.pop(key))
        with patch.object(contract, "WORK_EXACT_EDITORIAL_REVISIONS", approvals):
            self.assertFalse(self.prediction(old, new))
        self.assertFalse(self.prediction(new, old))
        self.assertTrue(self.prediction(new, new))
        for changed in (dict(new, question=new["question"] + " Otra hipótesis."),
                        dict(new, answers=["Error escritura"]), dict(new, code="int main(){}\n"),
                        dict(new, explanation="Otra explicación"), dict(new, id="otro_id"),
                        dict(new, outputComparison="exact")):
            self.assertFalse(self.prediction(old, changed))
        for kind in ("fill_code", "cpp_compiler", "paragraph", "multiple_choice"):
            self.assertFalse(self.prediction(dict(old, type=kind), dict(new, type=kind)))
        for identity in (None, 17, fixture.PREDICTION, "otro_id"):
            self.assertFalse(self.prediction(dict(old, id=identity), dict(new, id=identity)))
        self.assertFalse(self.prediction({key: value for key, value in old.items() if key != "id"},
                                         {key: value for key, value in new.items() if key != "id"}))
        for page, block in ((3, 1), (4, 0), (4, 2)):
            self.assertFalse(self.prediction(old, new, page=page, block=block))
        self.assertFalse(self.prediction(old, new, relative=fixture.RELATIVE.replace("componer_salida", "otra_salida")))
        for locale in ("en", "fr"):
            with self.assertRaises(ValueError): self.prediction(old, new, relative=fixture.RELATIVE.replace("/es/", f"/{locale}/"))

    def test_compiler_guidance_is_editorial_without_licensing_executable_changes(self):
        old, new = self.pair(fixture.COMPILER)
        self.assertTrue(self.compiler(old, new))
        self.assertEqual(fixture.COMPILER_HASHES, tuple(layout.fingerprint(value) for value in (old, new)))
        self.assertEqual(fixture.OUTPUT_CHECK_HASHES, tuple(layout.fingerprint(value["outputCheck"]) for value in (old, new)))
        with patch.object(contract, "U12_COMPOSED_FEEDBACK_REVISION", ("no authorization", 0, 0, *fixture.COMPILER_HASHES)):
            self.assertTrue(self.compiler(old, new))
        self.assertTrue(self.compiler(new, old))
        self.assertTrue(self.compiler(old, old)); self.assertTrue(self.compiler(new, new))
        self.assertEqual({"outputCheck"}, {field for field in old.keys() | new.keys() if old.get(field) != new.get(field)})
        self.assertEqual(old["initialCode"], new["initialCode"])
        self.assertNotIn("id", old); self.assertNotIn("id", new)
        # Location/family and the historical editorial edge are checked by the
        # complete lesson transition below, not by the executable projection.
        self.assertTrue(self.compiler(old, new, lesson=dict(self.before, id="another_lesson")))
        for page, block in ((1, 3), (2, 2), (2, 4)):
            self.assertTrue(self.compiler(old, new, page=page, block=block))
        for locale in ("en", "fr"):
            self.assertTrue(self.compiler(old, new, relative=fixture.RELATIVE.replace("/es/", f"/{locale}/")))
        self.assertTrue(self.compiler(old, new, relative=fixture.RELATIVE.replace("componer_salida", "otra_salida")))
        for field, value in (("type", "code"), ("id", "fake"), ("language", "cpp"),
                             ("initialCode", old["initialCode"] + "// other source\n")):
            self.assertFalse(self.compiler(old, dict(new, **{field: value})), field)
        self.assertTrue(self.compiler(dict(old, initialCode="other"), dict(new, initialCode="other")))
        self.assertTrue(self.compiler(dict(old, id="same_fake"), dict(new, id="same_fake")))

    def test_case_outputs_stdin_comparison_stay_closed_while_guidance_can_be_revised(self):
        old, new = self.pair(fixture.COMPILER)
        expected = old["outputCheck"]["cases"][0]
        case = new["outputCheck"]["cases"][0]
        self.assertEqual({"standardInput", "outputs"}, expected.keys())
        self.assertEqual("", case["standardInput"])
        self.assertEqual(["Validos=2;Rechazados=1\nValidos=0;Rechazados=0\nValidos=5;Rechazados=3\n"], case["outputs"])
        self.assertEqual(fixture.EXPLANATION, case["explanation"])
        self.assertEqual(fixture.REVIEW_FEEDBACK, case["reviewFeedback"])
        protected, editorial = [], []
        for field, value in (("standardInput", "extra"), ("outputs", ["Validos=2;Rechazados=1"]),
                             ("unknown", True)):
            value_block = deepcopy(new); value_block["outputCheck"]["cases"][0][field] = value; protected.append(value_block)
        value_block = deepcopy(new)
        value_block["outputCheck"]["cases"][0]["explanation"] += " Otra frase."
        editorial.append(value_block)
        for field in ("explanation", "reviewFeedback"):
            value_block = deepcopy(new); del value_block["outputCheck"]["cases"][0][field]; editorial.append(value_block)
        for rule in range(2):
            for field, value in (("outputs", ["another output"]), ("text", "another guide")):
                value_block = deepcopy(new); value_block["outputCheck"]["cases"][0]["reviewFeedback"][rule][field] = value
                editorial.append(value_block)
        value_block = deepcopy(new); value_block["outputCheck"]["cases"][0]["reviewFeedback"].reverse(); editorial.append(value_block)
        value_block = deepcopy(new); value_block["outputCheck"]["cases"].append(deepcopy(case)); protected.append(value_block)
        value_block = deepcopy(new); value_block["outputCheck"]["inputComparison"] = "tokens"; protected.append(value_block)
        value_block = deepcopy(new); value_block["outputCheck"]["cases"][0]["outputs"][0] = case["outputs"][0].rstrip("\n"); protected.append(value_block)
        for index, changed in enumerate(protected):
            with self.subTest(index=index): self.assertFalse(self.compiler(old, changed))
        for index, changed in enumerate(editorial):
            with self.subTest(editorial=index): self.assertTrue(self.compiler(old, changed))

    def test_prepare_preserves_prior_history_all_owners_sources_and_exact_relationships(self):
        before, after = self.before, self.after
        self.assertEqual((15, 17), tuple(len(value["stateLayout"]["editorial"]) for value in (before, after)))
        self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:15])
        for field in before["stateLayout"]:
            if field != "editorial": self.assertEqual(before["stateLayout"][field], after["stateLayout"][field], field)
        self.assertEqual(layout.validate(before), layout.validate(after)); self.assertEqual(layout.lineage(before), layout.lineage(after))
        layout.validate_transition(before, after)
        layout.check_localized_family([after, self.family["en"], self.family["fr"]], ["es", "en", "fr"])
        self.assertTrue(after["compilerPuzzlesRequired"])
        self.assertEqual([{"activity": fixture.COMPILER, "blocks": [f"block_0003_{index:04d}" for index in range(6)]}], after["referenceAfter"])
        self.assertEqual([{"activity": fixture.PREDICTION, "requires": [fixture.COMPILER]}], after["activityAfter"])
        for field in ("compilerPuzzlesRequired", "referenceAfter", "activityAfter"):
            self.assertEqual(before[field], after[field])
        old, new = layout._blocks(before), layout._blocks(after)
        self.assertEqual(25, len(old)); self.assertEqual(old.keys(), new.keys())
        self.assertEqual({fixture.COMPILER, fixture.PREDICTION}, {key for key in old if old[key] != new[key]})
        self.assertEqual(7, sum(field in block for block in old.values() for field in ("code", "initialCode")))
        for identity in old:
            self.assertEqual(layout.state_fingerprint(old[identity]), layout.state_fingerprint(new[identity]), identity)
            for field in ("code", "initialCode", "answers", "correctIndex", "options", "explanation"):
                self.assertEqual(old[identity].get(field), new[identity].get(field), (identity, field))
        restored = deepcopy(after)
        for identity in (fixture.COMPILER, fixture.PREDICTION):
            p, b = (2, 3) if identity == fixture.COMPILER else (4, 1)
            restored["pages"][p]["blocks"][b] = deepcopy(old[identity])
        del restored["stateLayout"]["editorial"][15:]
        self.assertEqual(before, restored)

    def test_sources_proposal_and_full_provenance_are_independently_pinned(self):
        self.assertEqual("57c0044a7484f1e6726383e9f880137cf43f6aa9", fixture.COMMIT)
        for locale in ("es", "en", "fr"):
            payload = fixture.historical_bytes(locale)
            self.assertEqual(fixture.SOURCE_HASHES[locale], hashlib.sha256(payload).hexdigest())
            self.assertNotIn(b"\r", payload)
            with self.assertRaises(ValueError): fixture.verify_bytes(locale, payload + b"\n")
        self.assertEqual(fixture.AFTER_SHA256, hashlib.sha256(fixture.json_bytes(self.after)).hexdigest())
        for index, document in enumerate((self.before, self.after)):
            self.assertEqual(fixture.PAGES_HASHES[index], layout.fingerprint(document["pages"]))
        self.assertEqual(fixture.EDITORIAL_PREFIX_SHA256, layout.fingerprint(self.before["stateLayout"]["editorial"]))
        self.assertEqual(fixture.AFTER_EDITORIAL_SHA256, layout.fingerprint(self.after["stateLayout"]["editorial"]))
        with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError): fixture.candidate()
        for mode in ("missing", "before", "after", "identity", "family", "source"):
            changed = deepcopy(self.after)
            if mode == "missing": changed["stateLayout"]["editorial"].pop()
            elif mode in ("before", "after"): changed["stateLayout"]["editorial"][-1][mode + "Sha256"] = "0" * 64
            elif mode == "identity": changed["pages"][4]["blocks"][1]["id"] = "other"
            elif mode == "family": changed["stateLayout"]["family"]["fr"]["sha256"] = "0" * 64
            else: changed["pages"][2]["blocks"][3]["initialCode"] += "// altered\n"
            with self.subTest(mode=mode), self.assertRaises(ValueError): fixture.verify_pair(self.before, changed, self.family)
        for mode in ("missing", "before", "after", "identity"):
            changed = deepcopy(self.after)
            if mode == "missing": changed["stateLayout"]["editorial"].pop()
            elif mode in ("before", "after"): changed["stateLayout"]["editorial"][-1][mode + "Sha256"] = "0" * 64
            else: changed["pages"][4]["blocks"][1]["answers"] = ["Other output"]
            with self.subTest(transition=mode), self.assertRaises(layout.LayoutError): layout.validate_transition(self.before, changed)

    def test_export_is_idempotent_and_keeps_live_curriculum_unchanged(self):
        paths = [fixture.ROOT / (fixture.PREFIX.format(locale=locale) + fixture.LESSON) for locale in ("es", "en", "fr")]
        original = {path: path.read_bytes() for path in paths}
        with tempfile.TemporaryDirectory(prefix="export-", dir=self.qa) as temporary:
            output = Path(temporary) / "fixtures"; manifest_path = fixture.export(output)
            snapshot = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()}
            self.assertEqual({"before.json", "after.json", "manifest.json"}, snapshot.keys())
            self.assertEqual(fixture.historical_bytes(), snapshot["before.json"][0])
            self.assertEqual(fixture.json_bytes(self.after), snapshot["after.json"][0])
            manifest = json.loads(manifest_path.read_bytes())
            self.assertTrue(manifest["afterIsProposal"])
            self.assertEqual((15, 17, 25, 7), tuple(manifest[key] for key in ("editorialBefore", "editorialAfter", "identities", "sources")))
            self.assertEqual(self.after["stateLayout"]["editorial"], manifest["editorial"])
            self.assertEqual(fixture.COMPILER_HASHES, tuple(manifest["blocks"][fixture.COMPILER]["sha256"]))
            self.assertEqual(fixture.PREDICTION_HASHES, tuple(manifest["blocks"][fixture.PREDICTION]["sha256"]))
            self.assertEqual({key: layout._blocks(self.before)[key] for key in (fixture.COMPILER, fixture.PREDICTION)}, manifest["previousBlocks"])
            fixture.export(output)
            self.assertEqual(snapshot, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()})
            cli = subprocess.run([sys.executable, "-B", str(Path(fixture.__file__)), "--output-dir", str(output)],
                                 cwd=fixture.ROOT, capture_output=True, text=True)
            self.assertEqual(0, cli.returncode, cli.stderr)
            self.assertEqual(snapshot, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()})
        self.assertEqual(original, {path: path.read_bytes() for path in paths})

    def test_export_conflicts_and_bad_sources_fail_before_any_write(self):
        with tempfile.TemporaryDirectory(prefix="negative-", dir=self.qa) as temporary:
            root = Path(temporary)
            for name in ("before.json", "after.json", "manifest.json"):
                output = root / name.replace(".", "-"); output.mkdir(); (output / name).write_bytes(b"keep original evidence")
                with self.subTest(name=name), self.assertRaises(FileExistsError): fixture.export(output)
                self.assertEqual({name: b"keep original evidence"}, {path.name: path.read_bytes() for path in output.iterdir()})
            output = root / "directory"; output.mkdir(); (output / "after.json").mkdir()
            with self.assertRaises(FileExistsError): fixture.export(output)
            self.assertEqual(["after.json"], [path.name for path in output.iterdir()])
            output = root / "bad-source"
            with patch.object(fixture, "historical_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
                fixture.export(output)
            self.assertFalse(output.exists())
        for destination in (fixture.ROOT, fixture.ROOT / "out", fixture.ROOT / "build", fixture.ROOT / "scripts" / "curriculum"):
            with self.subTest(destination=destination), self.assertRaises(ValueError): fixture.export(destination)

    def test_foreign_checkouts_and_links_are_never_written(self):
        with tempfile.TemporaryDirectory(prefix="portable-", dir=self.qa) as temporary:
            root = Path(temporary); names = ("LearnGameAndEngineDev-Codex", "LearnGameAndEngineDev-Work")
            for index, name in enumerate(names):
                parent = root / str(index); parent.mkdir()
                owner = parent / name; owner.mkdir(); (owner / ".git").write_text("QA owner", encoding="utf-8")
                foreign = parent / names[1-index]; foreign.mkdir(); (foreign / ".git").write_text("QA foreign", encoding="utf-8")
                with patch.object(fixture, "ROOT", owner), patch.object(fixture, "candidate") as authenticate, self.assertRaises(ValueError):
                    fixture.export(foreign / "out" / "qa" / "fixtures")
                authenticate.assert_not_called(); self.assertFalse((foreign / "out").exists())
                self.assertEqual("QA foreign", (foreign / ".git").read_text(encoding="utf-8"))
            nested = root / "nested"; nested.mkdir(); (nested / ".git").write_text("QA nested", encoding="utf-8")
            with self.assertRaises(ValueError): fixture.export(nested / "out")
            self.assertFalse((nested / "out").exists())
            real = root / "real"; real.mkdir(); link = root / "link"
            try: link.symlink_to(real, target_is_directory=True)
            except OSError: return
            with self.assertRaises(ValueError): fixture.export(link / "fixtures")
            self.assertEqual([], list(real.iterdir()))


if __name__ == "__main__":
    unittest.main()

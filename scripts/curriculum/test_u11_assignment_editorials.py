"""Closed U11L20 wording revisions retain every historical response contract."""
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
import u11_assignment_editorial_fixture_source as fixture
import validar_json_programacion as contract


RELATIVE = fixture.PREFIX.format(locale="es") + fixture.LESSON


class AssignmentEditorialTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before, cls.after, cls.family = fixture.candidate()

    def preserved(self, identity, before, after, *, relative=RELATIVE, position=None):
        p, b = fixture.BLOCKS[identity]["historical"] if position is None else position
        return contract.preserves_previous_exercise(relative, p, b, before, after)

    def test_six_individual_pairs_and_the_complete_combination(self):
        old_blocks = layout._blocks(self.before)
        for name, change in fixture.CHANGES.items():
            previous, prepared, _ = fixture.candidate([name])
            old, new = old_blocks[change["identity"]], layout._blocks(prepared)[change["identity"]]
            with self.subTest(field=name):
                self.assertEqual(change["before"], old[change["field"]])
                self.assertEqual(change["after"], new[change["field"]])
                self.assertEqual(change["hashes"], tuple(fixture.text_hash(item[change["field"]]) for item in (old, new)))
                self.assertEqual({change["field"]}, {key for key in old.keys() | new.keys() if old.get(key) != new.get(key)})
                self.assertTrue(self.preserved(change["identity"], old, new))
                self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
                layout.validate_transition(previous, prepared)
        self.assertEqual(6, len(fixture.CHANGES))
        self.assertEqual(4, len(fixture.BLOCKS))
        for identity, spec in fixture.BLOCKS.items():
            with self.subTest(identity=identity):
                old, new = old_blocks[identity], layout._blocks(self.after)[identity]
                self.assertEqual(spec["type"], old["type"])
                self.assertEqual(spec["activity_id"], old["id"])
                self.assertEqual(spec["before"], layout.fingerprint(old))
                self.assertEqual(spec["after"], layout.fingerprint(new))
                self.assertTrue(self.preserved(identity, old, new))

    def test_regression_requires_the_six_new_authorizations(self):
        approvals = {key: value for key, value in contract.WORK_EXACT_EDITORIAL_REVISIONS.items()
                     if key[0] != fixture.LESSON}
        for change in fixture.CHANGES.values():
            old = layout._blocks(self.before)[change["identity"]]
            new = dict(old, **{change["field"]: change["after"]})
            with self.subTest(identity=change["identity"], field=change["field"]), \
                    patch.object(contract, "WORK_EXACT_EDITORIAL_REVISIONS", approvals):
                self.assertFalse(self.preserved(change["identity"], old, new))

    def test_complete_document_guards_family_provenance_and_all_other_payloads(self):
        before, after = self.before, self.after
        self.assertEqual(layout.lineage(before), layout.lineage(after))
        self.assertEqual(layout.validate(before), layout.validate(after))
        self.assertEqual(20, len(before["stateLayout"]["editorial"]))
        self.assertEqual(24, len(after["stateLayout"]["editorial"]))
        self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:20])
        self.assertEqual(before["stateLayout"]["family"], after["stateLayout"]["family"])
        for locale, original in self.family.items():
            manifest = before["stateLayout"]["family"][locale]
            self.assertEqual(layout.fingerprint(layout._without_layout(original)), manifest["sha256"])
            self.assertEqual(layout.prepare(original)["stateLayout"]["origin"], manifest["origin"])
        restored = deepcopy(after)
        for change in fixture.CHANGES.values():
            layout._blocks(restored)[change["identity"]][change["field"]] = change["before"]
        restored["stateLayout"]["editorial"] = deepcopy(before["stateLayout"]["editorial"])
        self.assertEqual(before, restored)
        for identity, old in layout._blocks(before).items():
            self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(layout._blocks(after)[identity]))
        layout.check_localized_family([after, self.family["en"], self.family["fr"]], ["es", "en", "fr"])
        aligned = layout.align_for_previous_validation(before, after)
        positions = layout.validate(before)
        for p, page in enumerate(before["pages"]):
            for b, old in enumerate(page["blocks"]):
                current = aligned["pages"][p]["blocks"][b]
                historical = positions[p, b]
                if old["type"] == "cpp_compiler":
                    self.assertTrue(contract.preserves_previous_compiler(RELATIVE, *historical,
                        old, current, layout.original_projection(before)))
                elif old["type"] in contract.EXERCISES | {"code"}:
                    self.assertTrue(contract.preserves_previous_exercise(RELATIVE, *historical, old, current))

    def test_helper_can_append_pairs_to_the_then_current_proof_without_replacing_prefix(self):
        working = deepcopy(self.before)
        for change in fixture.CHANGES.values():
            draft = deepcopy(working)
            layout._blocks(draft)[change["identity"]][change["field"]] = change["after"]
            prepared = layout.prepare_v2(draft, "es", self.family, working)
            self.assertEqual(working["stateLayout"]["editorial"], prepared["stateLayout"]["editorial"][:-1])
            self.assertTrue(self.preserved(change["identity"], layout._blocks(working)[change["identity"]],
                layout._blocks(prepared)[change["identity"]]))
            working = prepared
        self.assertEqual(26, len(working["stateLayout"]["editorial"]))
        expected = deepcopy(self.after)
        expected["stateLayout"]["editorial"] = working["stateLayout"]["editorial"]
        self.assertEqual(expected, working)

    def test_only_forward_exact_texts_and_original_spanish_owners_are_authorized(self):
        for change in fixture.CHANGES.values():
            identity = change["identity"]
            old = layout._blocks(self.before)[identity]
            new = dict(old, **{change["field"]: change["after"]})
            p, b = fixture.BLOCKS[identity]["historical"]
            with self.subTest(identity=identity, field=change["field"]):
                self.assertFalse(self.preserved(identity, new, old))
                self.assertTrue(self.preserved(identity, new, new))
                for side in ("before", "after"):
                    before, after = deepcopy(old), deepcopy(new)
                    (before if side == "before" else after)[change["field"]] += " Otra redacción."
                    self.assertFalse(self.preserved(identity, before, after))
                self.assertFalse(self.preserved(identity, old, new, position=(p + 1, b)))
                self.assertFalse(self.preserved(identity, old, new, position=(p, b + 1)))
                self.assertFalse(self.preserved(identity, old, new, relative=RELATIVE.replace("lesson_", "lesson_otra_")))
                for kind in ("order_code", "output_prediction", "multiple_choice", "fill_code"):
                    if kind != old["type"]:
                        self.assertFalse(self.preserved(identity, dict(old, type=kind), dict(new, type=kind)))
                for changed_id in (None, 17, identity, "otra_actividad"):
                    self.assertFalse(self.preserved(identity, old, dict(new, id=changed_id)))
                    self.assertFalse(self.preserved(identity, dict(old, id=changed_id), dict(new, id=changed_id)))
                without_id = lambda value: {key: item for key, item in value.items() if key != "id"}
                self.assertFalse(self.preserved(identity, without_id(old), without_id(new)))
                for locale in ("en", "fr"):
                    with self.assertRaises(ValueError):
                        self.preserved(identity, old, new, relative=RELATIVE.replace("/es/", f"/{locale}/"))

    def test_answers_programs_options_and_unapproved_fields_stay_closed(self):
        for identity in fixture.BLOCKS:
            old, new = (layout._blocks(document)[identity] for document in (self.before, self.after))
            permitted = {change["field"] for change in fixture.CHANGES.values() if change["identity"] == identity}
            self.assertEqual(old.get("reviewFeedback"), new.get("reviewFeedback"))
            for field in old.keys() - permitted - {"reviewFeedback"}:
                bad = deepcopy(new)
                bad[field] = str(bad[field]) + " UNREVIEWED"
                with self.subTest(identity=identity, field=field):
                    self.assertFalse(self.preserved(identity, old, bad))
            self.assertFalse(self.preserved(identity, old, dict(new, unreviewed="UNREVIEWED")))

    def test_removed_forged_or_reassigned_proofs_are_rejected(self):
        for mutation in ("missing", "before", "after", "owner", "prefix", "origin", "family"):
            bad = deepcopy(self.after)
            if mutation == "missing": bad["stateLayout"]["editorial"].pop()
            elif mutation in ("before", "after"):
                bad["stateLayout"]["editorial"][-1][mutation + "Sha256"] = "0" * 64
            elif mutation == "owner": bad["stateLayout"]["editorial"][-1]["id"] = "block_0000_0000"
            elif mutation == "prefix": bad["stateLayout"]["editorial"][0]["beforeSha256"] = "0" * 64
            elif mutation == "origin": bad["stateLayout"]["origin"][0]["id"] = "otra_pagina"
            else: bad["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
            with self.subTest(mutation=mutation), self.assertRaises(layout.LayoutError):
                layout.validate_transition(self.before, bad)

    def test_pinned_sources_texts_and_complete_after_are_independent_of_live_asset(self):
        self.assertEqual("1601494bc25b07cdb6e13001989ab29092747e98", fixture.COMMIT)
        self.assertEqual("de49377325565bc816ab8eaf2d528442c6754f5f", fixture.ORIGIN_COMMIT)
        for origin in (False, True):
            for locale in ("es", "en", "fr"):
                payload = fixture.historical_bytes(locale, origin=origin)
                expected = (fixture.ORIGIN_SOURCE_HASHES if origin else fixture.SOURCE_HASHES)[locale]
                self.assertEqual(expected, hashlib.sha256(payload).hexdigest())
                with self.assertRaises(ValueError): fixture.verify_bytes(locale, payload + b"\n", origin=origin)
        self.assertEqual(fixture.AFTER_SHA256, hashlib.sha256(fixture.json_bytes(self.after)).hexdigest())
        with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError):
            fixture.candidate()
        for fields in (("unknown",), ("order_question", "order_question")):
            with self.assertRaises(ValueError): fixture.candidate(fields)

    def test_real_export_and_check_clis_are_idempotent_and_keep_curricular_bytes(self):
        assets = [fixture.ROOT / (fixture.PREFIX.format(locale=locale) + fixture.LESSON) for locale in ("es", "en", "fr")]
        originals = {path: path.read_bytes() for path in assets}
        with tempfile.TemporaryDirectory(prefix="lgaed-u11-assignment-") as temporary:
            output = Path(temporary) / "fixtures"
            command = [sys.executable, "-B", str(Path(fixture.__file__).resolve()), "--output-dir", str(output)]
            subprocess.run(command, cwd=fixture.ROOT, check=True, capture_output=True)
            exported = {path.name: path.read_bytes() for path in output.iterdir()}
            self.assertEqual({"assignment-before.json", "assignment-after.json", "manifest.json"}, exported.keys())
            modified = {path.name: path.stat().st_mtime_ns for path in output.iterdir()}
            subprocess.run(command, cwd=fixture.ROOT, check=True, capture_output=True)
            self.assertEqual(exported, {path.name: path.read_bytes() for path in output.iterdir()})
            self.assertEqual(modified, {path.name: path.stat().st_mtime_ns for path in output.iterdir()})
            manifest = json.loads(exported["manifest.json"])
            self.assertEqual(fixture.COMMIT, manifest["commit"])
            self.assertEqual(fixture.ORIGIN_COMMIT, manifest["originCommit"])
            self.assertEqual(RELATIVE, manifest["relative"])
            self.assertEqual(0, manifest["coordinateBase"])
            self.assertEqual(fixture.SOURCE_HASHES, manifest["sourceSha256"])
            self.assertEqual(fixture.ORIGIN_SOURCE_HASHES, manifest["originSourceSha256"])
            self.assertEqual(fixture.historical_bytes(), exported["assignment-before.json"])
            self.assertEqual(self.after, json.loads(exported["assignment-after.json"]))
            for side in ("before", "after"):
                self.assertEqual(manifest[side]["sha256"], hashlib.sha256(exported[manifest[side]["file"]]).hexdigest())
            for identity, spec in fixture.BLOCKS.items():
                entry = manifest["blocks"][identity]
                self.assertEqual(list(spec["historical"]), entry["historical"])
                self.assertEqual(spec["activity_id"], entry["activityId"])
                self.assertEqual(spec["before"], entry["beforeSha256"])
                self.assertEqual(spec["after"], entry["afterSha256"])
            checked = subprocess.run([sys.executable, "-B", str(Path(layout.__file__).resolve()), "check",
                str(output / "assignment-after.json"), "--previous", str(output / "assignment-before.json")],
                cwd=fixture.ROOT, check=True, capture_output=True, text=True, encoding="utf-8")
            self.assertIn("Lesson transition valid", checked.stdout)
        self.assertEqual(originals, {path: path.read_bytes() for path in assets})

    def test_export_validates_all_inputs_and_destinations_before_writes(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-u11-assignment-negative-") as temporary:
            root = Path(temporary)
            output = root / "bad-manifest"; output.mkdir()
            (output / "manifest.json").write_bytes(b"unrelated")
            with self.assertRaises(FileExistsError): fixture.export(output)
            self.assertEqual({"manifest.json": b"unrelated"}, {path.name: path.read_bytes() for path in output.iterdir()})
            output = root / "wrong-type"; output.mkdir()
            (output / "assignment-after.json").mkdir()
            with self.assertRaises(FileExistsError): fixture.export(output)
            self.assertEqual(["assignment-after.json"], [path.name for path in output.iterdir()])
            output = root / "bad-source"
            with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError):
                fixture.export(output)
            self.assertFalse(output.exists())
            checkout = root / "other-checkout"; checkout.mkdir(); (checkout / ".git").write_text("unrelated", encoding="utf-8")
            with self.assertRaises(ValueError): fixture.export(checkout / "out")
            self.assertFalse((checkout / "out").exists())
        with self.assertRaises(ValueError): fixture.export(fixture.ROOT / "scripts" / "curriculum")


if __name__ == "__main__":
    unittest.main()

"""The reviewed U11L22 space preserves complete published state contracts."""
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
import validar_json_programacion as contract
import u11_session_editorial_fixture_source as fixture


RELATIVE = fixture.PREFIX.format(locale="es") + fixture.LESSON


class SessionEditorialTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before, cls.after, cls.family = fixture.candidate()
        cls.temporary_root = fixture.ROOT / "out" / "qa"
        cls.temporary_root.mkdir(parents=True, exist_ok=True)

    def owners(self):
        return tuple(layout._blocks(document)[fixture.IDENTITY] for document in (self.before, self.after))

    def preserved(self, old, new, *, relative=RELATIVE, page=1, block=4):
        return contract.preserves_previous_exercise(relative, page, block, old, new)

    def test_exact_single_space_keeps_every_other_field_and_all_fourteen_proofs(self):
        old, new = self.owners()
        self.assertEqual("multiple_choice", old["type"])
        self.assertEqual(fixture.ACTIVITY_ID, old["id"])
        self.assertEqual(1, old["correctIndex"])
        self.assertEqual(fixture.BEFORE, old["explanation"])
        self.assertEqual(fixture.AFTER, new["explanation"])
        self.assertEqual("literal 100", new["explanation"][new["explanation"].index("literal"):][:11])
        self.assertEqual({"explanation"}, {key for key in old.keys() | new.keys() if old.get(key) != new.get(key)})
        self.assertEqual(fixture.BLOCK_HASHES, tuple(layout.fingerprint(block) for block in (old, new)))
        self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
        self.assertEqual(layout.lineage(self.before), layout.lineage(self.after))
        self.assertEqual(layout.validate(self.before), layout.validate(self.after))
        self.assertEqual(14, len(self.before["stateLayout"]["editorial"]))
        self.assertEqual(self.before["stateLayout"]["editorial"], self.after["stateLayout"]["editorial"][:-1])
        self.assertEqual({"id": fixture.IDENTITY, "beforeSha256": fixture.BLOCK_HASHES[0],
                          "afterSha256": fixture.BLOCK_HASHES[1]}, self.after["stateLayout"]["editorial"][-1])
        restored = deepcopy(self.after)
        layout._blocks(restored)[fixture.IDENTITY]["explanation"] = fixture.BEFORE
        restored["stateLayout"]["editorial"].pop()
        self.assertEqual(self.before, restored)
        self.assertEqual({"es": 10, "en": 8, "fr": 8}, {locale: len(doc["pages"]) for locale, doc in self.family.items()})
        layout.validate_transition(self.before, self.after)
        layout.check_localized_family([self.after, self.family["en"], self.family["fr"]], ["es", "en", "fr"])

    def test_whole_authoring_loop_uses_real_historical_owners(self):
        aligned = layout.align_for_previous_validation(self.before, self.after)
        positions = layout.validate(self.before)
        for pn, page in enumerate(self.before["pages"]):
            for bn, old in enumerate(page["blocks"]):
                new = aligned["pages"][pn]["blocks"][bn]
                hp, hb = positions[pn, bn]
                if old["type"] == "cpp_compiler":
                    self.assertTrue(contract.preserves_previous_compiler(RELATIVE, hp, hb, old, new,
                        layout.original_projection(self.before)))
                elif old["type"] in contract.EXERCISES | {"code"}:
                    self.assertTrue(self.preserved(old, new, page=hp, block=hb), f"current={pn},{bn}; original={hp},{hb}")
        old, new = self.owners()
        key = (fixture.LESSON, 2, 5, "multiple_choice", "explanation", fixture.ACTIVITY_ID)
        with patch.dict(contract.WORK_EXACT_EDITORIAL_REVISIONS):
            del contract.WORK_EXACT_EDITORIAL_REVISIONS[key]
            self.assertFalse(self.preserved(old, new), "The ordinary guard must not silently allow arbitrary explanations")

    def test_only_both_reviewed_texts_in_forward_direction_are_authorized(self):
        old, new = self.owners()
        self.assertTrue(self.preserved(old, new))
        self.assertFalse(self.preserved(new, old))
        self.assertTrue(self.preserved(new, new))
        for side in ("old", "new"):
            before, after = deepcopy(old), deepcopy(new)
            (before if side == "old" else after)["explanation"] += " Otra revisión."
            with self.subTest(side=side): self.assertFalse(self.preserved(before, after))
        for proposed in (fixture.AFTER.replace("100", "99"), fixture.AFTER.replace("literal ", "literal  "),
                         fixture.AFTER + " ", "Explicación diferente."):
            self.assertFalse(self.preserved(old, dict(new, explanation=proposed)))

    def test_identity_type_location_locale_and_other_fields_stay_protected(self):
        old, new = self.owners()
        for kind in ("fill_code", "output_prediction", "order_code", "inline_fill_code"):
            self.assertFalse(self.preserved(dict(old, type=kind), dict(new, type=kind)))
        for identity in (None, 17, fixture.IDENTITY, "otra_actividad"):
            self.assertFalse(self.preserved(old, dict(new, id=identity)))
            self.assertFalse(self.preserved(dict(old, id=identity), dict(new, id=identity)))
        absent = lambda block: {key: value for key, value in block.items() if key != "id"}
        self.assertFalse(self.preserved(absent(old), absent(new)))
        for pn, bn in ((2, 4), (1, 5), (0, 4)):
            self.assertFalse(self.preserved(old, new, page=pn, block=bn))
        self.assertFalse(self.preserved(old, new, relative=RELATIVE.replace("lesson_", "lesson_otra_")))
        for locale in ("en", "fr"):
            with self.assertRaises(ValueError): self.preserved(old, new, relative=RELATIVE.replace("/es/", f"/{locale}/"))
        for field in new.keys() - {"explanation", "reviewFeedback"}:
            changed = deepcopy(new)
            changed[field] = changed[field] + " NO AUTORIZADO" if isinstance(changed[field], str) else {"unreviewed": changed[field]}
            with self.subTest(field=field): self.assertFalse(self.preserved(old, changed))
        self.assertFalse(self.preserved(old, dict(new, campo_nuevo="NO AUTORIZADO")))

    def test_candidate_still_requires_complete_proof_prefix_origin_and_family(self):
        for mutation in ("remove", "false_before", "false_after", "wrong_owner", "prefix", "origin", "family", "contract"):
            bad = deepcopy(self.after)
            if mutation == "remove": bad["stateLayout"]["editorial"].pop()
            elif mutation == "false_before": bad["stateLayout"]["editorial"][-1]["beforeSha256"] = "0" * 64
            elif mutation == "false_after": bad["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
            elif mutation == "wrong_owner": bad["stateLayout"]["editorial"][-1]["id"] = "block_0000_0000"
            elif mutation == "prefix": bad["stateLayout"]["editorial"][0]["beforeSha256"] = "0" * 64
            elif mutation == "origin": bad["stateLayout"]["origin"][0]["id"] = "otra_pagina"
            elif mutation == "family": bad["stateLayout"]["family"]["en"]["sha256"] = "0" * 64
            else: layout._blocks(bad)[fixture.IDENTITY]["correctIndex"] = 0
            with self.subTest(mutation=mutation), self.assertRaises(layout.LayoutError):
                layout.validate_transition(self.before, bad)

    def test_all_sources_after_document_and_pair_are_independently_pinned(self):
        self.assertEqual("63962a88e6a6bd425b688971a6789d873a4ac316", fixture.COMMIT)
        for locale in ("es", "en", "fr"):
            payload = fixture.historical_bytes(locale)
            self.assertEqual(fixture.SOURCE_HASHES[locale], hashlib.sha256(payload).hexdigest())
            with self.assertRaises(ValueError): fixture.verify_bytes(locale, payload + b"\n")
        with self.assertRaises(ValueError): fixture.verify_bytes("xx", b"{}")
        self.assertEqual(fixture.TEXT_HASHES, tuple(hashlib.sha256(text.encode()).hexdigest() for text in (fixture.BEFORE, fixture.AFTER)))
        self.assertEqual(fixture.AFTER_SHA256, hashlib.sha256(fixture.json_bytes(self.after)).hexdigest())
        with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError): fixture.candidate()

    def test_real_cli_is_idempotent_and_never_changes_live_assets(self):
        paths = [fixture.ROOT / (fixture.PREFIX.format(locale=locale) + fixture.LESSON) for locale in ("es", "en", "fr")]
        originals = {path: path.read_bytes() for path in paths}
        with tempfile.TemporaryDirectory(prefix="u11-session-editorial-", dir=self.temporary_root) as temporary:
            output = Path(temporary) / "fixtures"
            command = [sys.executable, "-B", str(Path(fixture.__file__).resolve()), "--output-dir", str(output)]
            subprocess.run(command, cwd=fixture.ROOT, check=True, capture_output=True)
            exported = {path.name: path.read_bytes() for path in output.iterdir()}
            self.assertEqual({"session-before.json", "session-after.json", "manifest.json"}, exported.keys())
            stamps = {path.name: path.stat().st_mtime_ns for path in output.iterdir()}
            subprocess.run(command, cwd=fixture.ROOT, check=True, capture_output=True)
            self.assertEqual(exported, {path.name: path.read_bytes() for path in output.iterdir()})
            self.assertEqual(stamps, {path.name: path.stat().st_mtime_ns for path in output.iterdir()})
            manifest = json.loads(exported["manifest.json"])
            self.assertEqual(fixture.COMMIT, manifest["commit"])
            self.assertEqual(RELATIVE, manifest["relative"])
            self.assertEqual(fixture.ACTIVITY_ID, manifest["activityId"])
            self.assertEqual(fixture.SOURCE_HASHES, manifest["sourceSha256"])
            self.assertEqual(list(fixture.TEXT_HASHES), manifest["textSha256"])
            self.assertEqual(fixture.historical_bytes(), exported["session-before.json"])
            for side, document, block_hash in zip(("before", "after"), (self.before, self.after), fixture.BLOCK_HASHES):
                entry = manifest[side]; payload = exported[entry["file"]]
                self.assertEqual(entry["sha256"], hashlib.sha256(payload).hexdigest())
                self.assertEqual(block_hash, entry["blockSha256"])
                self.assertEqual(document, json.loads(payload))
            checked = subprocess.run([sys.executable, "-B", str(Path(layout.__file__).resolve()), "check",
                str(output / "session-after.json"), "--previous", str(output / "session-before.json")],
                cwd=fixture.ROOT, check=True, capture_output=True, text=True, encoding="utf-8")
            self.assertIn("Lesson transition valid", checked.stdout)
        self.assertEqual(originals, {path: path.read_bytes() for path in paths})

    def test_export_rejects_conflicts_bad_sources_and_unsafe_destinations_before_writing(self):
        with tempfile.TemporaryDirectory(prefix="u11-session-negative-", dir=self.temporary_root) as temporary:
            root = Path(temporary)
            conflict = root / "conflict"; conflict.mkdir()
            (conflict / "manifest.json").write_bytes(b"unrelated")
            with self.assertRaises(FileExistsError): fixture.export(conflict)
            self.assertEqual({"manifest.json": b"unrelated"}, {path.name: path.read_bytes() for path in conflict.iterdir()})
            wrong_type = root / "wrong-type"; wrong_type.mkdir(); (wrong_type / "session-after.json").mkdir()
            with self.assertRaises(FileExistsError): fixture.export(wrong_type)
            self.assertEqual(["session-after.json"], [path.name for path in wrong_type.iterdir()])
            absent = root / "bad-source"
            with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError): fixture.export(absent)
            self.assertFalse(absent.exists())
            checkout = root / "other-checkout"; checkout.mkdir(); (checkout / ".git").write_text("unrelated", encoding="utf-8")
            with self.assertRaises(ValueError): fixture.export(checkout / "out")
            self.assertFalse((checkout / "out").exists())
            with patch.object(Path, "lstat", return_value=type("Reparse", (), {"st_mode": 0, "st_file_attributes": 0x400})()), self.assertRaises(ValueError):
                fixture.export(root / "reparse")
        with self.assertRaises(ValueError): fixture.export(fixture.ROOT / "scripts" / "curriculum")
        with self.assertRaises(ValueError): fixture.export(fixture.ROOT.parent / "other-output")


if __name__ == "__main__":
    unittest.main()

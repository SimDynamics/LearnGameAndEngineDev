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
    current = False
    proof_count = 14
    expected_commit = "63962a88e6a6bd425b688971a6789d873a4ac316"
    source_bytes = staticmethod(fixture.historical_bytes)
    source_hashes = fixture.SOURCE_HASHES
    after_hash = fixture.AFTER_SHA256
    before_name, after_name, manifest_name = "session-before.json", "session-after.json", "manifest.json"

    @classmethod
    def setUpClass(cls):
        cls.before, cls.after, cls.family = fixture.candidate(current=cls.current)
        cls.temporary_root = fixture.ROOT / "out" / "qa"
        cls.temporary_root.mkdir(parents=True, exist_ok=True)

    def owners(self):
        return tuple(layout._blocks(document)[fixture.IDENTITY] for document in (self.before, self.after))

    def preserved(self, old, new, *, relative=RELATIVE, page=1, block=4):
        return contract.preserves_previous_exercise(relative, page, block, old, new)

    def test_exact_single_space_keeps_every_other_field_and_all_published_proofs(self):
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
        self.assertEqual(self.proof_count, len(self.before["stateLayout"]["editorial"]))
        self.assertEqual(self.proof_count + 1, len(self.after["stateLayout"]["editorial"]))
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
        self.assertEqual(self.expected_commit, fixture.CURRENT_COMMIT if self.current else fixture.COMMIT)
        for locale in ("es", "en", "fr"):
            payload = self.source_bytes(locale)
            self.assertEqual(self.source_hashes[locale], hashlib.sha256(payload).hexdigest())
            with self.assertRaises(ValueError): fixture.verify_bytes(locale, payload + b"\n", current=self.current)
        with self.assertRaises(ValueError): fixture.verify_bytes("xx", b"{}", current=self.current)
        self.assertEqual(fixture.TEXT_HASHES, tuple(hashlib.sha256(text.encode()).hexdigest() for text in (fixture.BEFORE, fixture.AFTER)))
        self.assertEqual(self.after_hash, hashlib.sha256(fixture.json_bytes(self.after)).hexdigest())
        with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError):
            fixture.candidate(current=self.current)

    def test_real_cli_is_idempotent_and_never_changes_live_assets(self):
        paths = [fixture.ROOT / (fixture.PREFIX.format(locale=locale) + fixture.LESSON) for locale in ("es", "en", "fr")]
        originals = {path: path.read_bytes() for path in paths}
        with tempfile.TemporaryDirectory(prefix="u11-session-editorial-", dir=self.temporary_root) as temporary:
            output = Path(temporary) / "fixtures"
            command = [sys.executable, "-B", str(Path(fixture.__file__).resolve()), "--output-dir", str(output)]
            if self.current: command.append("--current")
            subprocess.run(command, cwd=fixture.ROOT, check=True, capture_output=True)
            exported = {path.name: path.read_bytes() for path in output.iterdir()}
            self.assertEqual({self.before_name, self.after_name, self.manifest_name}, exported.keys())
            stamps = {path.name: path.stat().st_mtime_ns for path in output.iterdir()}
            subprocess.run(command, cwd=fixture.ROOT, check=True, capture_output=True)
            self.assertEqual(exported, {path.name: path.read_bytes() for path in output.iterdir()})
            self.assertEqual(stamps, {path.name: path.stat().st_mtime_ns for path in output.iterdir()})
            manifest = json.loads(exported[self.manifest_name])
            self.assertEqual(self.expected_commit, manifest["commit"])
            self.assertEqual(RELATIVE, manifest["relative"])
            self.assertEqual(fixture.ACTIVITY_ID, manifest["activityId"])
            self.assertEqual(self.source_hashes, manifest["sourceSha256"])
            self.assertEqual(list(fixture.TEXT_HASHES), manifest["textSha256"])
            self.assertEqual(self.source_bytes(), exported[self.before_name])
            for side, document, block_hash in zip(("before", "after"), (self.before, self.after), fixture.BLOCK_HASHES):
                entry = manifest[side]; payload = exported[entry["file"]]
                self.assertEqual(entry["sha256"], hashlib.sha256(payload).hexdigest())
                self.assertEqual(block_hash, entry["blockSha256"])
                self.assertEqual(document, json.loads(payload))
            checked = subprocess.run([sys.executable, "-B", str(Path(layout.__file__).resolve()), "check",
                str(output / self.after_name), "--previous", str(output / self.before_name)],
                cwd=fixture.ROOT, check=True, capture_output=True, text=True, encoding="utf-8")
            self.assertIn("Lesson transition valid", checked.stdout)
        self.assertEqual(originals, {path: path.read_bytes() for path in paths})

    def test_export_rejects_conflicts_bad_sources_and_unsafe_destinations_before_writing(self):
        with tempfile.TemporaryDirectory(prefix="u11-session-negative-", dir=self.temporary_root) as temporary:
            root = Path(temporary)
            conflict = root / "conflict"; conflict.mkdir()
            (conflict / self.manifest_name).write_bytes(b"unrelated")
            with self.assertRaises(FileExistsError): fixture.export(conflict, current=self.current)
            self.assertEqual({self.manifest_name: b"unrelated"}, {path.name: path.read_bytes() for path in conflict.iterdir()})
            wrong_type = root / "wrong-type"; wrong_type.mkdir(); (wrong_type / self.after_name).mkdir()
            with self.assertRaises(FileExistsError): fixture.export(wrong_type, current=self.current)
            self.assertEqual([self.after_name], [path.name for path in wrong_type.iterdir()])
            absent = root / "bad-source"
            with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError):
                fixture.export(absent, current=self.current)
            self.assertFalse(absent.exists())
            checkout = root / "other-checkout"; checkout.mkdir(); (checkout / ".git").write_text("unrelated", encoding="utf-8")
            with self.assertRaises(ValueError): fixture.export(checkout / "out", current=self.current)
            self.assertFalse((checkout / "out").exists())
            with patch.object(Path, "lstat", return_value=type("Reparse", (), {"st_mode": 0, "st_file_attributes": 0x400})()), self.assertRaises(ValueError):
                fixture.export(root / "reparse", current=self.current)
        with self.assertRaises(ValueError): fixture.export(fixture.ROOT / "scripts" / "curriculum", current=self.current)
        with self.assertRaises(ValueError): fixture.export(fixture.ROOT.parent / "other-output", current=self.current)


class CurrentSessionEditorialTest(SessionEditorialTest):
    current = True
    proof_count = 24
    expected_commit = "1d357e701ce69bdfe8e8dab074aea24a8a3268bc"
    source_bytes = staticmethod(fixture.current_bytes)
    source_hashes = fixture.CURRENT_SOURCE_HASHES
    after_hash = fixture.CURRENT_AFTER_SHA256
    before_name, after_name, manifest_name = "session-current-before.json", "session-current-after.json", "manifest-current.json"

    def test_current_proposal_preserves_all_identities_compilers_dependencies_and_p4_order(self):
        before_blocks, after_blocks = (layout._blocks(document) for document in (self.before, self.after))
        self.assertEqual(40, len(before_blocks))
        self.assertEqual(before_blocks.keys(), after_blocks.keys())
        self.assertEqual([fixture.IDENTITY], [identity for identity in before_blocks if before_blocks[identity] != after_blocks[identity]])
        for field in ("origin", "current", "family", "additions"):
            self.assertEqual(self.before["stateLayout"][field], self.after["stateLayout"][field], field)
        self.assertEqual({"id": "page_0003", "blocks": ["block_0003_0000", "block_0003_0002", "block_0003_0003",
                          "block_0003_0004", "block_0003_0001"]}, self.after["stateLayout"]["current"][3])
        compilers = {identity: block for identity, block in before_blocks.items() if block["type"] == "cpp_compiler"}
        self.assertEqual({"block_0003_0001", "block_0006_0003"}, compilers.keys())
        self.assertTrue(self.after["compilerPuzzlesRequired"])
        for identity, block in compilers.items():
            self.assertEqual(block, after_blocks[identity], identity)
            self.assertIn("initialCode", block)
            self.assertIn("outputCheck", block)
        for field in ("compilerPuzzlesRequired", "referenceAfter", "activityAfter"):
            self.assertEqual(self.before[field], self.after[field], field)
        self.assertEqual(2, len(self.after["referenceAfter"]))
        self.assertEqual(4, len(self.after["activityAfter"]))
        self.assertEqual({"activity": "block_0006_0003", "requires": ["block_0003_0001"]}, self.after["activityAfter"][-1])

    def test_current_source_is_separate_from_historical_and_uses_immutable_git(self):
        self.assertEqual("9012b0d2a268a5df7fde68a74922748262c7b1d08e21d6d21225743db76c2933", fixture.CURRENT_SOURCE_HASHES["es"])
        self.assertEqual("9e248574ca08044e9d533f58cba482f033b7ea0e9fc091b9939bcce2c824b8ba", fixture.CURRENT_AFTER_SHA256)
        self.assertNotEqual(fixture.SOURCE_HASHES["es"], fixture.CURRENT_SOURCE_HASHES["es"])
        for current, payload in ((True, fixture.historical_bytes()), (False, fixture.current_bytes())):
            with self.subTest(current=current), self.assertRaises(ValueError):
                fixture.verify_bytes("es", payload, current=current)
        for current, commit, source in ((False, fixture.COMMIT, fixture.historical_bytes),
                                        (True, fixture.CURRENT_COMMIT, fixture.current_bytes)):
            with patch.object(fixture.subprocess, "check_output", return_value=source()) as git:
                source()
                args, options = git.call_args
                self.assertEqual(f"{commit}:{RELATIVE}", args[0][-1])
                self.assertEqual("1", options["env"]["GIT_NO_LAZY_FETCH"])
                self.assertEqual("1", options["env"]["GIT_NO_REPLACE_OBJECTS"])
                self.assertEqual("0", options["env"]["GIT_TERMINAL_PROMPT"])

    def test_current_export_coexists_with_unchanged_historical_files(self):
        with tempfile.TemporaryDirectory(prefix="u11-session-coexist-", dir=self.temporary_root) as temporary:
            output = Path(temporary)
            historical_manifest = fixture.export(output)
            originals = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()}
            self.assertEqual(output / "manifest-current.json", fixture.export(output, current=True))
            self.assertEqual(output / "manifest.json", historical_manifest)
            self.assertEqual(6, len(list(output.iterdir())))
            for name, (payload, stamp) in originals.items():
                self.assertEqual(payload, (output / name).read_bytes())
                self.assertEqual(stamp, (output / name).stat().st_mtime_ns)

    def test_current_candidate_rejects_changes_outside_the_exact_published_proposal(self):
        for mutation in ("proof14", "proof20", "program", "required", "reference", "activity", "p4order"):
            bad = deepcopy(self.after)
            if mutation == "proof14": bad["stateLayout"]["editorial"].pop(14)
            elif mutation == "proof20": bad["stateLayout"]["editorial"][20]["beforeSha256"] = "0" * 64
            elif mutation == "program": layout._blocks(bad)["block_0006_0003"]["initialCode"] += "// Otro programa.\n"
            elif mutation == "required": bad["compilerPuzzlesRequired"] = False
            elif mutation == "reference": bad["referenceAfter"].pop()
            elif mutation == "activity": bad["activityAfter"].pop()
            else:
                for entries in (bad["pages"][3]["blocks"], bad["stateLayout"]["current"][3]["blocks"]):
                    entries.insert(1, entries.pop())
            with self.subTest(mutation=mutation), patch.object(fixture.layout, "prepare_v2", return_value=bad), self.assertRaises(ValueError):
                fixture.candidate(current=True)


if __name__ == "__main__":
    unittest.main()

"""One exact file-flow hypothesis changes without weakening state protection."""
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
import u12_append_hypothesis_fixture_source as fixture
import validar_json_programacion as contract


class U12AppendHypothesisTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before, cls.after, cls.family = fixture.candidate()
        cls.current_before, cls.current_after, cls.current_family = fixture.current_candidate()

    def blocks(self):
        return tuple(deepcopy(layout._blocks(document)[fixture.IDENTITY]) for document in (self.before, self.after))

    def preserved(self, old, new, relative=fixture.RELATIVE, page=2, block=1):
        return contract.preserves_previous_exercise(relative, page, block, old, new)

    def test_exact_forward_pair_requires_its_own_closed_authorization(self):
        old, new = self.blocks()
        self.assertEqual(fixture.TEXT_HASHES, tuple(hashlib.sha256(value["question"].encode()).hexdigest() for value in (old, new)))
        self.assertTrue(self.preserved(old, new))
        key = (fixture.LESSON, 3, 2, "output_prediction", "question", fixture.ACTIVITY_ID)
        approvals = dict(contract.WORK_EXACT_EDITORIAL_REVISIONS)
        self.assertEqual(fixture.TEXT_HASHES, approvals.pop(key))
        with patch.object(contract, "WORK_EXACT_EDITORIAL_REVISIONS", approvals):
            self.assertFalse(self.preserved(old, new))
        self.assertFalse(self.preserved(new, old))
        self.assertTrue(self.preserved(new, new))
        for side in (0, 1):
            pair = self.blocks(); pair[side]["question"] += " Otra redacción."
            self.assertFalse(self.preserved(*pair))

    def test_original_type_id_coordinates_and_spanish_lesson_remain_closed(self):
        old, new = self.blocks()
        for kind in ("fill_code", "multiple_choice", "paragraph", "cpp_compiler"):
            self.assertFalse(self.preserved(dict(old, type=kind), dict(new, type=kind)))
        for identity in (None, 17, fixture.IDENTITY, "otra_actividad"):
            self.assertFalse(self.preserved(old, dict(new, id=identity)))
            self.assertFalse(self.preserved(dict(old, id=identity), dict(new, id=identity)))
        self.assertFalse(self.preserved({key: value for key, value in old.items() if key != "id"},
                                        {key: value for key, value in new.items() if key != "id"}))
        for page, block in ((1, 1), (3, 1), (2, 0), (2, 2)):
            self.assertFalse(self.preserved(old, new, page=page, block=block))
        self.assertFalse(self.preserved(old, new, relative=fixture.RELATIVE.replace("cerrar_reemplazar", "otro_archivo")))
        for locale in ("en", "fr"):
            with self.assertRaises(ValueError):
                self.preserved(old, new, relative=fixture.RELATIVE.replace("/es/", f"/{locale}/"))

    def test_code_answer_explanation_and_all_other_payloads_stay_exact(self):
        old, new = self.blocks()
        self.assertEqual({"question"}, {key for key in old.keys() | new.keys() if old.get(key) != new.get(key)})
        self.assertEqual(["Lectura incompleta"], new["answers"])
        for field in new.keys() - {"question"}:
            changed = deepcopy(new)
            changed[field] = changed[field] + " NO AUTORIZADO" if isinstance(changed[field], str) else ["otra respuesta"]
            self.assertFalse(self.preserved(old, changed), field)
        for field, value in (("outputComparison", "exact"), ("answers", ["Total=3"]),
                             ("initialCode", "otro programa"), ("unknown", "otro campo")):
            self.assertFalse(self.preserved(old, dict(new, **{field: value})), field)
        self.assertEqual(old["code"], new["code"])
        self.assertEqual(old["explanation"], new["explanation"])
        self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))

    def test_prepare_preserves_all_33_owners_family_and_six_prior_edges(self):
        before, after = self.before, self.after
        self.assertEqual(33, len(layout._blocks(before)))
        self.assertEqual(layout.validate(before), layout.validate(after))
        self.assertEqual(layout.lineage(before), layout.lineage(after))
        layout.validate_transition(before, after)
        layout.check_localized_family([after, self.family["en"], self.family["fr"]], ["es", "en", "fr"])
        for field in before["stateLayout"]:
            if field != "editorial": self.assertEqual(before["stateLayout"][field], after["stateLayout"][field], field)
        self.assertEqual({"es": 9, "en": 5, "fr": 5},
                         {locale: len(value["origin"]) for locale, value in after["stateLayout"]["family"].items()})
        self.assertEqual(6, len(before["stateLayout"]["editorial"]))
        self.assertEqual(7, len(after["stateLayout"]["editorial"]))
        self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:-1])
        self.assertEqual({"id": fixture.IDENTITY, "beforeSha256": fixture.BLOCK_HASHES[0],
                          "afterSha256": fixture.BLOCK_HASHES[1]}, after["stateLayout"]["editorial"][-1])
        for field, digest in (("family", fixture.FAMILY_SHA256), ("origin", fixture.ORIGIN_SHA256),
                              ("current", fixture.CURRENT_SHA256), ("editorial", fixture.EDITORIAL_PREFIX_SHA256)):
            self.assertEqual(digest, layout.fingerprint(before["stateLayout"][field]))
        restored = deepcopy(after)
        layout._blocks(restored)[fixture.IDENTITY]["question"] = fixture.BEFORE
        restored["stateLayout"]["editorial"].pop()
        self.assertEqual(before, restored)
        aligned = layout.align_for_previous_validation(before, after)
        positions = layout.validate(before)
        for pn, page in enumerate(before["pages"]):
            for bn, old in enumerate(page["blocks"]):
                new = aligned["pages"][pn]["blocks"][bn]; origin_p, origin_b = positions[pn, bn]
                if old["type"] == "cpp_compiler":
                    self.assertTrue(contract.preserves_previous_compiler(fixture.RELATIVE, origin_p, origin_b, old, new,
                                                                        layout.original_projection(before)))
                elif old["type"] in contract.EXERCISES | {"code"}:
                    self.assertTrue(contract.preserves_previous_exercise(fixture.RELATIVE, origin_p, origin_b, old, new))

    def test_false_or_missing_editorial_proof_is_refused(self):
        for name, before, after in (("historical", self.before, self.after), ("published", self.current_before, self.current_after)):
            for mode in ("remove", "before", "after", "owner", "origin"):
                bad = deepcopy(after)
                if mode == "remove": bad["stateLayout"]["editorial"].pop()
                elif mode == "before": bad["stateLayout"]["editorial"][-1]["beforeSha256"] = "0" * 64
                elif mode == "after": bad["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
                elif mode == "owner": bad["stateLayout"]["editorial"][-1]["id"] = "block_0000_0000"
                else: bad["stateLayout"]["origin"][0]["id"] = "otra_pagina"
                with self.subTest(pair=name, mode=mode), self.assertRaises(layout.LayoutError):
                    layout.validate_transition(before, bad)

    def test_all_git_bytes_and_full_candidate_are_independently_pinned(self):
        self.assertEqual("93923a470b98e9bd6acaf95cf836dd014b5e4a29", fixture.COMMIT)
        for locale in ("es", "en", "fr"):
            payload = fixture.historical_bytes(locale)
            self.assertEqual(fixture.SOURCE_HASHES[locale], hashlib.sha256(payload).hexdigest())
            with self.assertRaises(ValueError): fixture.verify_bytes(locale, payload + b"\n")
        self.assertEqual(fixture.AFTER_SHA256, hashlib.sha256(fixture.json_bytes(self.after)).hexdigest())
        with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError):
            fixture.candidate()

    def test_current_pair_is_the_exact_published_eleven_to_twelve_edge_revision(self):
        before, after = self.current_before, self.current_after
        self.assertEqual("18d34c2f94f25a0796534d53fe4fd21a79aa09ce", fixture.CURRENT_BEFORE_COMMIT)
        self.assertEqual("fe9dff99f7237163abd1c00a8aa5c4ddda4a1681", fixture.CURRENT_AFTER_COMMIT)
        self.assertEqual(33, len(layout._blocks(before)))
        self.assertEqual(11, len(before["stateLayout"]["editorial"]))
        self.assertEqual(12, len(after["stateLayout"]["editorial"]))
        self.assertEqual(fixture.CURRENT_EDITORIAL_PREFIX_SHA256, layout.fingerprint(before["stateLayout"]["editorial"]))
        self.assertEqual(fixture.CURRENT_EDITORIAL_SHA256, layout.fingerprint(after["stateLayout"]["editorial"]))
        self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:-1])
        self.assertEqual(self.before["stateLayout"]["editorial"], before["stateLayout"]["editorial"][:6])
        self.assertEqual({"id": fixture.IDENTITY, "beforeSha256": fixture.BLOCK_HASHES[0],
                          "afterSha256": fixture.BLOCK_HASHES[1]}, after["stateLayout"]["editorial"][-1])
        for field in before["stateLayout"]:
            if field != "editorial": self.assertEqual(before["stateLayout"][field], after["stateLayout"][field], field)
        self.assertEqual([{"activity": "block_0003_0003", "blocks": ["block_0003_0004", "block_0004_0000",
                          "block_0004_0001", "block_0004_0002"]}], after["referenceAfter"])
        self.assertEqual([{"activity": "block_0004_0003", "requires": ["block_0003_0003"]}], after["activityAfter"])
        self.assertNotIn("compilerPuzzlesRequired", before)
        self.assertNotIn("compilerPuzzlesRequired", after)
        self.assertEqual(before["referenceAfter"], after["referenceAfter"])
        self.assertEqual(before["activityAfter"], after["activityAfter"])
        self.assertEqual(layout.validate(before), layout.validate(after))
        self.assertEqual(layout.lineage(before), layout.lineage(after))
        layout.validate_transition(before, after)
        layout.check_localized_family([after, self.current_family["en"], self.current_family["fr"]], ["es", "en", "fr"])
        old, new = (layout._blocks(value)[fixture.IDENTITY] for value in (before, after))
        self.assertTrue(self.preserved(old, new))
        self.assertEqual({"question"}, {key for key in old.keys() | new.keys() if old.get(key) != new.get(key)})
        for field in ("id", "type", "code", "answers", "explanation", "language"):
            self.assertEqual(old[field], new[field])
        restored = deepcopy(after)
        layout._blocks(restored)[fixture.IDENTITY]["question"] = fixture.BEFORE
        restored["stateLayout"]["editorial"].pop()
        self.assertEqual(before, restored)
        for locale in ("es", "en", "fr"):
            for after_flag, expected in ((False, fixture.CURRENT_SOURCE_HASHES), (True, fixture.CURRENT_AFTER_SOURCE_HASHES)):
                payload = fixture.current_bytes(locale, after=after_flag)
                self.assertEqual(expected[locale], hashlib.sha256(payload).hexdigest())
                with self.assertRaises(ValueError): fixture.verify_current_bytes(locale, payload + b"\n", after=after_flag)
        self.assertEqual(fixture.current_bytes(after=True), fixture.json_bytes(after))
        self.assertEqual(fixture.CURRENT_AFTER_SHA256, hashlib.sha256(fixture.json_bytes(after)).hexdigest())
        with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError):
            fixture.current_candidate()

    def test_cli_export_is_idempotent_without_curricular_writes(self):
        paths = [fixture.ROOT / (fixture.PREFIX.format(locale=locale) + fixture.LESSON) for locale in ("es", "en", "fr")]
        original = {path: path.read_bytes() for path in paths}
        temporary_root = fixture.ROOT / "out" / "qa" / "u12-append-hypothesis-python"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="export-", dir=temporary_root) as temporary:
            output = Path(temporary) / "fixtures"
            command = [sys.executable, "-B", str(Path(fixture.__file__).resolve()), "--output-dir", str(output)]
            subprocess.run(command, cwd=fixture.ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            exported = {path.name: path.read_bytes() for path in output.iterdir()}
            stamps = {path.name: path.stat().st_mtime_ns for path in output.iterdir()}
            self.assertEqual({"before.json", "after.json", "manifest.json", "current-before.json",
                              "current-after.json", "current-manifest.json"}, exported.keys())
            subprocess.run(command, cwd=fixture.ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(exported, {path.name: path.read_bytes() for path in output.iterdir()})
            self.assertEqual(stamps, {path.name: path.stat().st_mtime_ns for path in output.iterdir()})
            self.assertEqual(fixture.historical_bytes(), exported["before.json"])
            self.assertEqual(fixture.json_bytes(self.after), exported["after.json"])
            self.assertEqual(fixture.current_bytes(), exported["current-before.json"])
            self.assertEqual(fixture.current_bytes(after=True), exported["current-after.json"])
            manifest = json.loads(exported["manifest.json"])
            self.assertEqual(fixture.COMMIT, manifest["commit"])
            self.assertEqual(list(fixture.HISTORICAL), manifest["historical"])
            self.assertEqual(fixture.RELATIVE, manifest["relative"])
            for side, expected in (("before", fixture.SOURCE_HASHES["es"]), ("after", fixture.AFTER_SHA256)):
                self.assertEqual(expected, manifest[side]["sha256"])
                self.assertEqual(expected, hashlib.sha256(exported[manifest[side]["file"]]).hexdigest())
            current_manifest = json.loads(exported["current-manifest.json"])
            self.assertEqual(fixture.CURRENT_BEFORE_COMMIT, current_manifest["beforeCommit"])
            self.assertEqual(fixture.CURRENT_AFTER_COMMIT, current_manifest["afterCommit"])
            self.assertEqual(11, current_manifest["editorialBefore"])
            self.assertEqual(12, current_manifest["editorialAfter"])
            self.assertEqual(fixture.CURRENT_EDITORIAL_PREFIX_SHA256, current_manifest["editorialPrefixSha256"])
            for side, expected in (("before", fixture.CURRENT_SOURCE_HASHES["es"]), ("after", fixture.CURRENT_AFTER_SHA256)):
                self.assertEqual(expected, current_manifest[side]["sha256"])
                self.assertEqual(expected, hashlib.sha256(exported[current_manifest[side]["file"]]).hexdigest())
        self.assertEqual(original, {path: path.read_bytes() for path in paths})

    def test_export_conflicts_and_disallowed_destinations_fail_before_writing(self):
        temporary_root = fixture.ROOT / "out" / "qa" / "u12-append-hypothesis-python"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="negative-", dir=temporary_root) as temporary:
            root = Path(temporary)
            conflict = root / "conflict"; conflict.mkdir()
            (conflict / "after.json").write_bytes(b"keep original")
            with self.assertRaises(FileExistsError): fixture.export(conflict)
            self.assertEqual({"after.json": b"keep original"}, {path.name: path.read_bytes() for path in conflict.iterdir()})
            invalid_type = root / "directory"; invalid_type.mkdir(); (invalid_type / "before.json").mkdir()
            with self.assertRaises(FileExistsError): fixture.export(invalid_type)
            self.assertEqual(["before.json"], [path.name for path in invalid_type.iterdir()])
            absent = root / "bad-source"
            with patch.object(fixture, "historical_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
                fixture.export(absent)
            self.assertFalse(absent.exists())
            missing_current = root / "bad-current-source"
            with patch.object(fixture, "current_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
                fixture.export(missing_current)
            self.assertFalse(missing_current.exists())
            other_checkout = root / "checkout"; other_checkout.mkdir(); (other_checkout / ".git").write_text("unrelated", encoding="utf-8")
            with self.assertRaises(ValueError): fixture.export(other_checkout / "qa")
            self.assertFalse((other_checkout / "qa").exists())
        for output in (fixture.ROOT / "scripts" / "curriculum",):
            with self.assertRaises(ValueError): fixture.export(output)

    def test_foreign_checkout_negative_is_portable_for_both_work_and_codex_names(self):
        # Both simulated roots live only inside this checkout's own QA area.
        # Never target the real sibling Work/Codex directories: when this test
        # runs in Work, its own out/qa is an authorized destination, not foreign.
        temporary_root = fixture.ROOT / "out" / "qa" / "u12-append-hypothesis-python"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="portable-", dir=temporary_root) as temporary:
            root = Path(temporary)
            names = ("LearnGameAndEngineDev-Codex", "LearnGameAndEngineDev-Work")
            for scenario, owner_name in enumerate(names):
                parent = root / str(scenario); parent.mkdir()
                owner = parent / owner_name; owner.mkdir()
                other = parent / names[1-scenario]; other.mkdir()
                (owner / ".git").write_text("test-owned root marker", encoding="utf-8")
                (other / ".git").write_text("test-owned foreign marker", encoding="utf-8")
                foreign_output = other / "out" / "qa" / "fixtures"
                with self.subTest(owner=owner_name), patch.object(fixture, "ROOT", owner), \
                        patch.object(fixture, "candidate") as authenticate, self.assertRaises(ValueError):
                    fixture.export(foreign_output)
                authenticate.assert_not_called()
                self.assertFalse((other / "out").exists())
                self.assertEqual("test-owned root marker", (owner / ".git").read_text(encoding="utf-8"))
                self.assertEqual("test-owned foreign marker", (other / ".git").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()

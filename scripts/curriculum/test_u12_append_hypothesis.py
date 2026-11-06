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
        cls.suffix_before, cls.suffix_after, cls.suffix_family = fixture.suffix_candidate()
        cls.final_before, cls.final_after, cls.final_family = fixture.final_candidate()

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

    def test_suffix_pair_is_raw_publications_with_two_real_later_p6_edits(self):
        before, after = self.suffix_before, self.suffix_after
        self.assertEqual(self.current_before, before)
        self.assertEqual("18d34c2f94f25a0796534d53fe4fd21a79aa09ce", fixture.SUFFIX_BEFORE_COMMIT)
        self.assertEqual("b2208f7da1d985216867a7ff4d4275bc3bc0ca8b", fixture.SUFFIX_AFTER_COMMIT)
        self.assertEqual(11, len(before["stateLayout"]["editorial"]))
        self.assertEqual(14, len(after["stateLayout"]["editorial"]))
        self.assertEqual(self.current_after["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:12])
        self.assertEqual(fixture.SUFFIX_EDITORIAL_SHA256, layout.fingerprint(after["stateLayout"]["editorial"]))
        self.assertEqual([{"id": "block_0005_0001",
                           "beforeSha256": "2605bd47eb587664152a36237fd8cd1ba76894dcc58c182ac8a74661eb68489d",
                           "afterSha256": "8bcee01baa4355084dbab89902780b269b72901347981fe9a7e8d0fd77e85acf"},
                          {"id": "block_0005_0002",
                           "beforeSha256": "5530152564fc0424d687b947084473adf203069edd6c1c9c9295f90a5138fae4",
                           "afterSha256": "8862987a96139ba33fd9ba1d1aa97dd2adf9378a38028beb15a738697512988d"}],
                         after["stateLayout"]["editorial"][12:])
        for field in before["stateLayout"]:
            if field != "editorial": self.assertEqual(before["stateLayout"][field], after["stateLayout"][field], field)
        previous, current = layout._blocks(before), layout._blocks(after)
        self.assertEqual(33, len(current))
        self.assertEqual(previous.keys(), current.keys())
        changed = {identity: {key for key in old.keys() | current[identity].keys() if old.get(key) != current[identity].get(key)}
                   for identity, old in previous.items() if old != current[identity]}
        self.assertEqual({fixture.IDENTITY: {"question"}, "block_0005_0001": {"reviewFeedback"},
                          "block_0005_0002": {"text", "emphasis"}}, changed)
        for identity, old in previous.items():
            self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(current[identity]))
            if old["type"] in {"code", "cpp_compiler"}: self.assertEqual(old, current[identity])
        self.assertEqual(layout.validate(before), layout.validate(after))
        self.assertEqual(layout.lineage(before), layout.lineage(after))
        self.assertTrue(self.preserved(previous[fixture.IDENTITY], current[fixture.IDENTITY]))
        layout.validate_transition(before, after)
        layout.check_localized_family([after, self.suffix_family["en"], self.suffix_family["fr"]], ["es", "en", "fr"])
        self.assertNotIn("compilerPuzzlesRequired", after)
        self.assertEqual(before["referenceAfter"], after["referenceAfter"][:1])
        self.assertEqual(before["activityAfter"], after["activityAfter"][:1])
        self.assertEqual({"activity": "block_0005_0003", "blocks": ["block_0006_0000", "block_0006_0001"]},
                         after["referenceAfter"][1])
        self.assertEqual({"activity": "block_0007_0001", "requires": ["block_0005_0003"]}, after["activityAfter"][1])
        self.assertEqual(2, len(after["referenceAfter"]))
        self.assertEqual(2, len(after["activityAfter"]))
        # The exported source remains the actual pre-question publication,
        # not today's p6 text combined with an older hypothetical question.
        hypothetical = deepcopy(after)
        layout._blocks(hypothetical)[fixture.IDENTITY]["question"] = fixture.BEFORE
        self.assertNotEqual(before["pages"], hypothetical["pages"])
        for locale in ("es", "en", "fr"):
            for after_flag, hashes in ((False, fixture.SUFFIX_SOURCE_HASHES), (True, fixture.SUFFIX_AFTER_SOURCE_HASHES)):
                payload = fixture.suffix_bytes(locale, after=after_flag)
                self.assertEqual(hashes[locale], hashlib.sha256(payload).hexdigest())
                with self.assertRaises(ValueError): fixture.verify_suffix_bytes(locale, payload + b"\n", after=after_flag)
        self.assertEqual(fixture.suffix_bytes(after=True), fixture.json_bytes(after))
        self.assertEqual(fixture.SUFFIX_AFTER_SHA256, hashlib.sha256(fixture.json_bytes(after)).hexdigest())

    def test_suffix_sources_and_proofs_cannot_be_substituted(self):
        original = fixture.suffix_bytes
        with patch.object(fixture, "suffix_bytes", side_effect=lambda locale="es", *, after=False:
                          fixture.current_bytes(locale, after=after)), self.assertRaises(ValueError):
            fixture.suffix_candidate()
        with patch.object(fixture, "suffix_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
            fixture.suffix_candidate()
        self.assertEqual(fixture.SUFFIX_AFTER_SHA256, hashlib.sha256(original(after=True)).hexdigest())
        for mode in ("remove_suffix_proof", "reorder_suffix", "false_owner", "change_source"):
            bad = deepcopy(self.suffix_after)
            if mode == "remove_suffix_proof": bad["stateLayout"]["editorial"].pop()
            elif mode == "reorder_suffix": bad["stateLayout"]["editorial"][12:] = bad["stateLayout"]["editorial"][12:][::-1]
            elif mode == "false_owner": bad["stateLayout"]["editorial"][-1]["id"] = fixture.IDENTITY
            else: bad["pages"][5]["blocks"][3]["initialCode"] += "// unrelated program\n"
            with patch.object(fixture, "suffix_bytes", side_effect=lambda locale="es", *, after=False:
                              fixture.json_bytes(bad) if after else original(locale)), self.assertRaises((ValueError, layout.LayoutError)):
                fixture.suffix_candidate()

    def test_final_pair_preserves_actual_sixteen_edge_history_programs_and_answers(self):
        before, after = self.final_before, self.final_after
        self.assertEqual(self.current_before, before)
        self.assertEqual("18d34c2f94f25a0796534d53fe4fd21a79aa09ce", fixture.FINAL_BEFORE_COMMIT)
        self.assertEqual("79890b03ab6de01c18ca6c3adebeb96bf4d766a8", fixture.FINAL_AFTER_COMMIT)
        self.assertEqual(11, len(before["stateLayout"]["editorial"]))
        self.assertEqual(16, len(after["stateLayout"]["editorial"]))
        self.assertEqual(self.suffix_after["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:14])
        self.assertEqual(fixture.FINAL_EDITORIAL_SHA256, layout.fingerprint(after["stateLayout"]["editorial"]))
        self.assertEqual([{"id": "block_0007_0001",
                           "beforeSha256": "d86a37ce31908280b2bfa0c2db787892ac6886b202584058d09aa6e06b720837",
                           "afterSha256": "0623f7b364b8a64e7d50375576b197278f75f016602f06ff5cc42658d349101b"},
                          {"id": "block_0007_0002",
                           "beforeSha256": "9f7b042b04d3a5da6d59c52df61493e74231bfb85a73cc4827cd92fae108b35a",
                           "afterSha256": "2bfb96456242bb5f43977bcc21c8aaa4a9657394095c8b1a89b87fba3c7a6ac2"}],
                         after["stateLayout"]["editorial"][14:])
        for field in before["stateLayout"]:
            if field != "editorial": self.assertEqual(before["stateLayout"][field], after["stateLayout"][field], field)
        previous, current = layout._blocks(before), layout._blocks(after)
        self.assertEqual(33, len(current))
        self.assertEqual(previous.keys(), current.keys())
        changed = {identity: {key for key in old.keys() | current[identity].keys() if old.get(key) != current[identity].get(key)}
                   for identity, old in previous.items() if old != current[identity]}
        self.assertEqual({fixture.IDENTITY: {"question"}, "block_0005_0001": {"reviewFeedback"},
                          "block_0005_0002": {"text", "emphasis"}, "block_0007_0001": {"reviewFeedback"},
                          "block_0007_0002": {"text", "emphasis"}}, changed)
        for identity, old in previous.items():
            self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(current[identity]))
            if old["type"] in {"code", "cpp_compiler"}: self.assertEqual(old, current[identity])
            for field in ("answers", "correctIndex", "options", "explanation"):
                self.assertEqual(old.get(field), current[identity].get(field))
        self.assertTrue(self.preserved(previous[fixture.IDENTITY], current[fixture.IDENTITY]))
        self.assertEqual(layout.validate(before), layout.validate(after))
        self.assertEqual(layout.lineage(before), layout.lineage(after))
        layout.validate_transition(before, after)
        layout.check_localized_family([after, self.final_family["en"], self.final_family["fr"]], ["es", "en", "fr"])
        self.assertIs(after["compilerPuzzlesRequired"], True)
        self.assertEqual(self.suffix_after["referenceAfter"], after["referenceAfter"][:2])
        self.assertEqual({"activity": "block_0007_0003", "blocks": ["block_0008_0000", "block_0008_0001"]},
                         after["referenceAfter"][2])
        self.assertEqual(3, len(after["referenceAfter"]))
        self.assertEqual(self.suffix_after["activityAfter"], after["activityAfter"])
        self.assertEqual(2, len(after["activityAfter"]))
        payload = fixture.final_bytes(after=True)
        self.assertEqual(fixture.json_bytes(after), payload)
        self.assertEqual(fixture.FINAL_AFTER_SHA256, hashlib.sha256(payload).hexdigest())
        self.assertNotIn(b"\r", payload)
        self.assertEqual(fixture.FINAL_WINDOWS_SHA256, hashlib.sha256(payload.replace(b"\n", b"\r\n")).hexdigest())
        for locale in ("es", "en", "fr"):
            for after_flag, hashes in ((False, fixture.FINAL_SOURCE_HASHES), (True, fixture.FINAL_AFTER_SOURCE_HASHES)):
                data = fixture.final_bytes(locale, after=after_flag)
                self.assertEqual(hashes[locale], hashlib.sha256(data).hexdigest())
                with self.assertRaises(ValueError): fixture.verify_final_bytes(locale, data + b"\n", after=after_flag)

    def test_final_sources_reject_older_or_unreviewed_full_payloads(self):
        original = fixture.final_bytes
        for mode in ("old_suffix", "remove_proof", "source", "owner", "relationship"):
            bad = deepcopy(self.final_after)
            if mode == "old_suffix": bad = deepcopy(self.suffix_after)
            elif mode == "remove_proof": bad["stateLayout"]["editorial"].pop()
            elif mode == "source": bad["pages"][7]["blocks"][3]["initialCode"] += "// unrelated source\n"
            elif mode == "owner": bad["stateLayout"]["editorial"][-1]["id"] = fixture.IDENTITY
            else: bad["referenceAfter"][-1]["activity"] = "block_0003_0003"
            with self.subTest(mode=mode), patch.object(fixture, "final_bytes", side_effect=lambda locale="es", *, after=False:
                                                      fixture.json_bytes(bad) if after else original(locale)), self.assertRaises(ValueError):
                fixture.final_candidate()
        with patch.object(fixture, "final_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
            fixture.final_candidate()

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
                              "current-after.json", "current-manifest.json", "suffix-before.json",
                              "suffix-after.json", "suffix-manifest.json", "final-before.json",
                              "final-after.json", "final-manifest.json"}, exported.keys())
            subprocess.run(command, cwd=fixture.ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(exported, {path.name: path.read_bytes() for path in output.iterdir()})
            self.assertEqual(stamps, {path.name: path.stat().st_mtime_ns for path in output.iterdir()})
            self.assertEqual(fixture.historical_bytes(), exported["before.json"])
            self.assertEqual(fixture.json_bytes(self.after), exported["after.json"])
            self.assertEqual(fixture.current_bytes(), exported["current-before.json"])
            self.assertEqual(fixture.current_bytes(after=True), exported["current-after.json"])
            self.assertEqual(fixture.suffix_bytes(), exported["suffix-before.json"])
            self.assertEqual(fixture.suffix_bytes(after=True), exported["suffix-after.json"])
            self.assertEqual(fixture.final_bytes(), exported["final-before.json"])
            self.assertEqual(fixture.final_bytes(after=True), exported["final-after.json"])
            self.assertEqual("189c10513c55ad49486a9e8dcb4d2e5baf934a8fce3147f20e16b05f6bec7313",
                             hashlib.sha256(exported["manifest.json"]).hexdigest())
            self.assertEqual("cf99c513c1038af7c0059125b1f0cd49b2053e16385ac7b18994683bf9dc2808",
                             hashlib.sha256(exported["current-manifest.json"]).hexdigest())
            self.assertEqual("a0afcd1cb66f0deee79baf290a19cac207f285f3bcc80cba6f03419887e4b3cc",
                             hashlib.sha256(exported["suffix-manifest.json"]).hexdigest())
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
            suffix_manifest = json.loads(exported["suffix-manifest.json"])
            self.assertEqual(fixture.SUFFIX_BEFORE_COMMIT, suffix_manifest["beforeCommit"])
            self.assertEqual(fixture.SUFFIX_AFTER_COMMIT, suffix_manifest["afterCommit"])
            self.assertEqual(11, suffix_manifest["editorialBefore"])
            self.assertEqual(14, suffix_manifest["editorialAfter"])
            self.assertEqual(12, suffix_manifest["questionEditorialAfter"])
            self.assertEqual(self.suffix_after["stateLayout"]["editorial"][12:], suffix_manifest["laterEditorial"])
            self.assertEqual(self.suffix_after["referenceAfter"], suffix_manifest["referenceAfter"])
            self.assertEqual(self.suffix_after["activityAfter"], suffix_manifest["activityAfter"])
            for side, expected in (("before", fixture.SUFFIX_SOURCE_HASHES["es"]), ("after", fixture.SUFFIX_AFTER_SHA256)):
                self.assertEqual(expected, suffix_manifest[side]["sha256"])
                self.assertEqual(expected, hashlib.sha256(exported[suffix_manifest[side]["file"]]).hexdigest())
            final_manifest = json.loads(exported["final-manifest.json"])
            self.assertEqual(fixture.FINAL_BEFORE_COMMIT, final_manifest["beforeCommit"])
            self.assertEqual(fixture.FINAL_AFTER_COMMIT, final_manifest["afterCommit"])
            self.assertEqual(11, final_manifest["editorialBefore"])
            self.assertEqual(16, final_manifest["editorialAfter"])
            self.assertEqual(12, final_manifest["questionEditorialAfter"])
            self.assertEqual(self.final_after["stateLayout"]["editorial"][12:], final_manifest["laterEditorial"])
            for field in ("compilerPuzzlesRequired", "referenceAfter", "activityAfter"):
                self.assertEqual(self.final_after[field], final_manifest[field])
            for side, expected in (("before", fixture.FINAL_SOURCE_HASHES["es"]), ("after", fixture.FINAL_AFTER_SHA256)):
                self.assertEqual(expected, final_manifest[side]["sha256"])
                self.assertEqual(expected, hashlib.sha256(exported[final_manifest[side]["file"]]).hexdigest())
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
            missing_suffix = root / "bad-suffix-source"
            with patch.object(fixture, "suffix_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
                fixture.export(missing_suffix)
            self.assertFalse(missing_suffix.exists())
            missing_final = root / "bad-final-source"
            with patch.object(fixture, "final_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
                fixture.export(missing_final)
            self.assertFalse(missing_final.exists())
            final_conflict = root / "final-conflict"; final_conflict.mkdir()
            (final_conflict / "final-after.json").write_bytes(b"retained final evidence")
            with self.assertRaises(FileExistsError): fixture.export(final_conflict)
            self.assertEqual({"final-after.json": b"retained final evidence"},
                             {path.name: path.read_bytes() for path in final_conflict.iterdir()})
            late_conflict = root / "suffix-conflict"; late_conflict.mkdir()
            (late_conflict / "suffix-after.json").write_bytes(b"retained suffix evidence")
            with self.assertRaises(FileExistsError): fixture.export(late_conflict)
            self.assertEqual({"suffix-after.json": b"retained suffix evidence"},
                             {path.name: path.read_bytes() for path in late_conflict.iterdir()})
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

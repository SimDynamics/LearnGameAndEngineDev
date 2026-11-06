"""Preserve the actual no-map -> first-v2 U12L4 documents for native QA."""
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
import u12_composed_output_fixture_source as fixture


class U12ComposedOutputFixturesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before, cls.after, cls.family = fixture.candidate()
        cls.current_before, cls.current, cls.current_family = fixture.current_candidate()
        cls.qa = fixture.ROOT / "out" / "qa" / "u12-composed-output-python"
        cls.qa.mkdir(parents=True, exist_ok=True)

    def test_complete_git_publications_and_untouched_translations_are_pinned(self):
        self.assertEqual("4741c4241b4e83f8458c7ad4bb051e6559aa21ca", fixture.BEFORE_COMMIT)
        self.assertEqual("05f14cabc192eb8851d8bd8b02ab2aecb744dbf6", fixture.AFTER_COMMIT)
        for after in (False, True):
            hashes = fixture.AFTER_SOURCE_HASHES if after else fixture.SOURCE_HASHES
            for locale in ("es", "en", "fr"):
                raw = fixture.historical_bytes(locale, after=after)
                self.assertEqual(hashes[locale], hashlib.sha256(raw).hexdigest())
                self.assertNotIn(b"\r", raw)
                self.assertEqual(raw, fixture.json_bytes(json.loads(raw)))
                with self.assertRaises(ValueError): fixture.verify_bytes(locale, raw + b"\n", after=after)
                with self.assertRaises(ValueError): fixture.verify_bytes(locale, raw.replace(b"\n", b"\r\n"), after=after)
        for locale in ("en", "fr"):
            self.assertEqual(fixture.historical_bytes(locale), fixture.historical_bytes(locale, after=True))
        with self.assertRaises(ValueError): fixture.historical_bytes("de")
        with self.assertRaises(ValueError): fixture.verify_bytes("de", b"{}")

    def test_first_v2_adopts_exactly_the_published_family_without_moving_owners(self):
        before, after = self.before, self.after
        self.assertNotIn("stateLayout", before)
        metadata = after["stateLayout"]
        self.assertEqual((2, "es", []), (metadata["version"], metadata["locale"], metadata["additions"]))
        self.assertEqual(fixture.SHAPE, tuple(len(page["blocks"]) for page in before["pages"]))
        self.assertEqual(25, len(layout._blocks(after)))
        self.assertEqual(layout.validate(before), layout.validate(after))
        for field, digest in (("family", fixture.FAMILY_SHA256), ("origin", fixture.ORIGIN_SHA256),
                              ("current", fixture.CURRENT_SHA256), ("editorial", fixture.EDITORIAL_SHA256)):
            self.assertEqual(digest, layout.fingerprint(metadata[field]), field)
        self.assertEqual({"es": (5, 25), "en": (5, 22), "fr": (5, 22)},
                         {locale: (len(value["origin"]), sum(len(page["blocks"]) for page in value["origin"]))
                          for locale, value in metadata["family"].items()})
        draft = deepcopy(after); del draft["stateLayout"]
        self.assertEqual(after, layout.prepare_v2(draft, "es", self.family, before))
        layout.validate_transition(before, after)
        layout.check_localized_family([after, self.family["en"], self.family["fr"]], ["es", "en", "fr"])

    def test_only_three_complete_prose_passages_and_five_review_guides_changed(self):
        before = layout._blocks(layout.prepare(self.before)); after = layout._blocks(self.after)
        expected = {**fixture.PARAGRAPHS, **fixture.GUIDES}
        self.assertEqual(set(expected), {identity for identity in before if before[identity] != after[identity]})
        self.assertEqual(8, len(self.after["stateLayout"]["editorial"]))
        for edge, (identity, values) in zip(self.after["stateLayout"]["editorial"], sorted(expected.items())):
            page, block, old_sha, new_sha = values
            self.assertEqual({"id": identity, "beforeSha256": old_sha, "afterSha256": new_sha}, edge)
            self.assertEqual(old_sha, layout.fingerprint(before[identity]))
            self.assertEqual(new_sha, layout.fingerprint(after[identity]))
            self.assertEqual(before[identity], self.before["pages"][page]["blocks"][block])
            self.assertEqual(after[identity], self.after["pages"][page]["blocks"][block])
            fields = {field for field in before[identity].keys() | after[identity].keys()
                      if before[identity].get(field) != after[identity].get(field)}
            self.assertEqual({"text", "emphasis"} if identity in fixture.PARAGRAPHS else {"reviewFeedback"}, fields)
            if identity in fixture.PARAGRAPHS:
                self.assertEqual({"type", "text"}, set(before[identity]))
                self.assertEqual("paragraph", before[identity]["type"])
            for field in ("id", "type", "question", "options", "answers", "correctIndex", "explanation",
                          "language", "outputComparison", "outputCheck", "code", "initialCode"):
                self.assertEqual(before[identity].get(field), after[identity].get(field), (identity, field))

    def test_all_seven_sources_and_entire_p3_p5_stay_literal(self):
        sources = []
        for page, (old_page, new_page) in enumerate(zip(self.before["pages"], self.after["pages"])):
            for block, (old, new) in enumerate(zip(old_page["blocks"], new_page["blocks"])):
                for field in ("code", "initialCode"):
                    if field in old:
                        self.assertEqual(old[field], new[field], (page, block, field))
                        sources.append((page, block, field))
        self.assertEqual([(0, 2, "code"), (1, 1, "code"), (1, 3, "code"), (2, 3, "initialCode"),
                          (3, 1, "code"), (3, 5, "code"), (4, 1, "code")], sources)
        self.assertEqual(self.before["pages"][2:], self.after["pages"][2:])
        for field in ("compilerPuzzlesRequired", "referenceAfter", "activityAfter"):
            self.assertNotIn(field, self.before)
            self.assertNotIn(field, self.after)

    def test_arbitrary_payload_or_provenance_cannot_borrow_this_fixture_pair(self):
        # Strict Git authentication precedes semantic checks; no otherwise valid
        # new editorial revision may borrow these historical complete documents.
        variants = []
        for side in (0, 1):
            pair = [deepcopy(self.before), deepcopy(self.after)]
            pair[side]["pages"][0]["blocks"][1]["text"] += " Otra prosa."
            variants.append(pair)
        for field, value in (("answers", ["Otra salida"]), ("code", "int main(){}\n"),
                             ("reviewFeedback", "Otra guía"), ("explanation", "Otra explicación")):
            pair = [deepcopy(self.before), deepcopy(self.after)]
            pair[1]["pages"][1]["blocks"][1][field] = value
            variants.append(pair)
        for field in ("family", "origin", "current", "editorial"):
            pair = [deepcopy(self.before), deepcopy(self.after)]
            pair[1]["stateLayout"][field] = []
            variants.append(pair)
        pair = [deepcopy(self.before), deepcopy(self.after)]; pair[1]["compilerPuzzlesRequired"] = True; variants.append(pair)
        pair = [deepcopy(self.before), deepcopy(self.after)]; pair[1]["pages"][2]["blocks"][1]["text"] += " No autorizado."; variants.append(pair)
        for before, after in variants:
            with self.subTest(after=after.get("id")), self.assertRaises(ValueError):
                fixture.verify_pair(before, after, self.family)
        bad_family = deepcopy(self.family); bad_family["en"]["title"] += " altered"
        with self.assertRaises(ValueError): fixture.verify_pair(self.before, self.after, bad_family)
        with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError):
            fixture.candidate()

    def test_export_preserves_raw_sources_manifest_and_existing_artifact_stamps(self):
        active = [fixture.ROOT / (fixture.PREFIX.format(locale=locale) + fixture.LESSON) for locale in ("es", "en", "fr")]
        originals = {path: path.read_bytes() for path in active}
        with tempfile.TemporaryDirectory(prefix="export-", dir=self.qa) as temporary:
            output = Path(temporary) / "fixtures"
            manifest_path = fixture.export(output)
            self.assertEqual(output / "manifest.json", manifest_path)
            snapshot = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()}
            self.assertEqual({"before.json", "after.json", "manifest.json"}, snapshot.keys())
            self.assertEqual(fixture.historical_bytes(), snapshot["before.json"][0])
            self.assertEqual(fixture.historical_bytes(after=True), snapshot["after.json"][0])
            manifest = json.loads(snapshot["manifest.json"][0])
            self.assertEqual((fixture.BEFORE_COMMIT, fixture.AFTER_COMMIT), (manifest["beforeCommit"], manifest["afterCommit"]))
            self.assertEqual((0, 2, 25, 7, 8), (manifest["beforeLayoutVersion"], manifest["afterLayoutVersion"],
                                               manifest["identities"], manifest["sources"], manifest["editorialAfter"]))
            self.assertEqual(self.after["stateLayout"]["editorial"], manifest["editorial"])
            self.assertEqual(set(fixture.PARAGRAPHS) | set(fixture.GUIDES), manifest["previousBlocks"].keys())
            for identity, record in manifest["paragraphs"].items():
                page, block, old_sha, new_sha = fixture.PARAGRAPHS[identity]
                self.assertEqual([page, block], record["historical"])
                self.assertEqual((old_sha, new_sha), (record["beforeSha256"], record["afterSha256"]))
                self.assertEqual(self.before["pages"][page]["blocks"][block], manifest["previousBlocks"][identity])
            for identity, (page, block, old_sha, _) in fixture.GUIDES.items():
                previous = manifest["previousBlocks"][identity]
                self.assertEqual(self.before["pages"][page]["blocks"][block], previous)
                self.assertEqual(old_sha, layout.fingerprint(previous))
                self.assertNotIn("reviewFeedback", previous)
            fixture.export(output)
            self.assertEqual(snapshot, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()})
            result = subprocess.run([sys.executable, "-B", str(Path(fixture.__file__)), "--output-dir", str(output)],
                                    cwd=fixture.ROOT, capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(snapshot, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()})
        self.assertEqual(originals, {path: path.read_bytes() for path in active})

    def test_export_rejects_conflicts_bad_sources_and_directories_before_writing(self):
        with tempfile.TemporaryDirectory(prefix="negative-", dir=self.qa) as temporary:
            root = Path(temporary)
            for name in ("before.json", "after.json", "manifest.json"):
                output = root / name.replace(".", "-"); output.mkdir()
                (output / name).write_bytes(b"keep existing evidence")
                with self.subTest(name=name), self.assertRaises(FileExistsError): fixture.export(output)
                self.assertEqual({name: b"keep existing evidence"}, {path.name: path.read_bytes() for path in output.iterdir()})
            output = root / "directory"; output.mkdir(); (output / "after.json").mkdir()
            with self.assertRaises(FileExistsError): fixture.export(output)
            self.assertEqual(["after.json"], [path.name for path in output.iterdir()])
            output = root / "bad-source"
            with patch.object(fixture, "historical_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
                fixture.export(output)
            self.assertFalse(output.exists())
        for destination in (fixture.ROOT, fixture.ROOT / "out", fixture.ROOT / "build", fixture.ROOT / "scripts" / "curriculum"):
            with self.subTest(destination=destination), self.assertRaises(ValueError): fixture.export(destination)

    def test_foreign_checkout_and_links_never_receive_fixture_writes(self):
        with tempfile.TemporaryDirectory(prefix="portable-", dir=self.qa) as temporary:
            root = Path(temporary)
            names = ("LearnGameAndEngineDev-Codex", "LearnGameAndEngineDev-Work")
            for index, name in enumerate(names):
                parent = root / str(index); parent.mkdir()
                owner = parent / name; owner.mkdir(); (owner / ".git").write_text("QA owner", encoding="utf-8")
                other = parent / names[1-index]; other.mkdir(); (other / ".git").write_text("QA foreign", encoding="utf-8")
                with patch.object(fixture, "ROOT", owner), patch.object(fixture, "candidate") as authenticate, self.assertRaises(ValueError):
                    fixture.export(other / "out" / "qa" / "fixtures")
                authenticate.assert_not_called()
                self.assertFalse((other / "out").exists())
                self.assertEqual("QA foreign", (other / ".git").read_text(encoding="utf-8"))
            nested = root / "nested"; nested.mkdir(); (nested / ".git").write_text("QA nested", encoding="utf-8")
            with self.assertRaises(ValueError): fixture.export(nested / "out")
            self.assertFalse((nested / "out").exists())
            real = root / "real"; real.mkdir(); link = root / "link"
            try:
                link.symlink_to(real, target_is_directory=True)
            except OSError:
                return  # Native Windows may not grant symlink creation.
            with self.assertRaises(ValueError): fixture.export(link / "fixtures")
            self.assertEqual([], list(real.iterdir()))

    def test_current_pair_keeps_real_old_pages_and_authenticates_the_seven_later_revisions(self):
        before, current = self.current_before, self.current
        self.assertEqual(self.before, before); self.assertEqual(self.family, self.current_family)
        self.assertEqual("57c0044a7484f1e6726383e9f880137cf43f6aa9", fixture.CURRENT_AFTER_COMMIT)
        self.assertEqual("ef77e1f7d47ab04a43690723c0bf18a97d52bb8ab7323c0df48f34997b807d87",
                         hashlib.sha256(fixture.historical_current_bytes()).hexdigest())
        self.assertEqual(fixture.CURRENT_PAGES_SHA256, layout.fingerprint(current["pages"]))
        self.assertEqual(fixture.CURRENT_EDITORIAL_SHA256, layout.fingerprint(current["stateLayout"]["editorial"]))
        self.assertEqual(self.after["pages"][:2], current["pages"][:2])
        self.assertEqual(self.after["stateLayout"]["editorial"], current["stateLayout"]["editorial"][:8])
        self.assertEqual(15, len(current["stateLayout"]["editorial"]))
        old, new = layout._blocks(self.after), layout._blocks(current)
        self.assertEqual(set(fixture.CURRENT_SUFFIX), {key for key in old if old[key] != new[key]})
        for index, (identity, (page, block, old_sha, new_sha)) in enumerate(fixture.CURRENT_SUFFIX.items()):
            self.assertGreaterEqual(page, 2)
            self.assertEqual((old_sha, new_sha), tuple(layout.fingerprint(values[identity]) for values in (old, new)))
            self.assertEqual({"id": identity, "beforeSha256": old_sha, "afterSha256": new_sha},
                             current["stateLayout"]["editorial"][8 + index])
            self.assertEqual(new[identity], current["pages"][page]["blocks"][block])
        restored = deepcopy(current)
        for identity, (page, block, _, _) in fixture.CURRENT_SUFFIX.items():
            restored["pages"][page]["blocks"][block] = deepcopy(old[identity])
        del restored["stateLayout"]["editorial"][8:]
        for field in ("compilerPuzzlesRequired", "referenceAfter", "activityAfter"): del restored[field]
        self.assertEqual(self.after, restored)

    def test_current_preserves_all_25_response_contracts_seven_sources_and_authored_gates(self):
        before, current = self.current_before, self.current
        original, active = layout._blocks(layout.prepare(before)), layout._blocks(current)
        self.assertEqual(25, len(active)); self.assertEqual(original.keys(), active.keys())
        for identity in original:
            self.assertEqual(layout.state_fingerprint(original[identity]), layout.state_fingerprint(active[identity]), identity)
            for field in ("code", "initialCode", "answers", "options", "correctIndex", "outputCheck", "explanation"):
                self.assertEqual(original[identity].get(field), active[identity].get(field), (identity, field))
        self.assertEqual(7, sum(field in block for block in original.values() for field in ("code", "initialCode")))
        for field in self.after["stateLayout"]:
            if field != "editorial": self.assertEqual(self.after["stateLayout"][field], current["stateLayout"][field], field)
        self.assertTrue(current["compilerPuzzlesRequired"])
        self.assertEqual([{"activity": "block_0002_0003", "blocks": [f"block_0003_{index:04d}" for index in range(6)]}], current["referenceAfter"])
        self.assertEqual([{"activity": "block_0004_0001", "requires": ["block_0002_0003"]}], current["activityAfter"])
        draft = deepcopy(current); del draft["stateLayout"]
        self.assertEqual(current, layout.prepare_v2(draft, "es", self.current_family, before))
        layout.validate_transition(before, current); layout.validate_transition(self.after, current)
        layout.check_localized_family([current, self.current_family["en"], self.current_family["fr"]], ["es", "en", "fr"])

    def test_coherent_new_prose_relocations_and_other_family_cannot_borrow_the_current_pair(self):
        # These are coherent alternative documents, not merely corrupt hashes.
        # Native/publishing validity does not authenticate them as this source.
        for page, block in ((0, 1), (0, 0), (1, 2), (2, 1)):
            draft = deepcopy(self.current); draft["pages"][page]["blocks"][block]["text"] += " Otra prosa compatible."
            changed = layout.prepare_v2(draft, "es", self.current_family, self.current)
            layout.validate_transition(self.current, changed)
            with self.subTest(prose=(page, block)), self.assertRaises(ValueError):
                fixture.verify_current_pair(self.current_before, changed, self.current_family)
        moved = deepcopy(self.current)
        blocks = moved["pages"][0]["blocks"]; blocks[1], blocks[3] = blocks[3], blocks[1]
        identities = moved["stateLayout"]["current"][0]["blocks"]; identities[1], identities[3] = identities[3], identities[1]
        layout.validate_transition(self.current, moved)
        with self.assertRaises(ValueError): fixture.verify_current_pair(self.current_before, moved, self.current_family)
        other_family = deepcopy(self.current_family); other_family["en"]["title"] += " other publication"
        draft = deepcopy(self.current); del draft["stateLayout"]
        other = layout.prepare_v2(draft, "es", other_family, self.current_before)
        layout.check_localized_family([other, other_family["en"], other_family["fr"]], ["es", "en", "fr"])
        with self.assertRaises(ValueError): fixture.verify_current_pair(self.current_before, other, other_family)

    def test_current_bytes_provenance_and_family_sources_are_closed(self):
        for locale in ("es", "en", "fr"):
            raw = fixture.historical_current_bytes(locale)
            self.assertEqual(fixture.CURRENT_SOURCE_HASHES[locale], hashlib.sha256(raw).hexdigest())
            self.assertNotIn(b"\r", raw)
            with self.assertRaises(ValueError): fixture.verify_current_bytes(locale, raw + b"\n")
            if locale != "es": self.assertEqual(fixture.historical_bytes(locale), raw)
        with self.assertRaises(ValueError): fixture.historical_current_bytes("de")
        with patch.object(fixture.subprocess, "check_output", return_value=b"{}"), self.assertRaises(ValueError): fixture.current_candidate()
        for mode in ("missing_suffix", "suffix_order", "old_edge", "new_edge", "source", "answer", "family"):
            changed = deepcopy(self.current)
            if mode == "missing_suffix": changed["stateLayout"]["editorial"].pop()
            elif mode == "suffix_order": changed["stateLayout"]["editorial"][8:10] = changed["stateLayout"]["editorial"][8:10][::-1]
            elif mode == "old_edge": changed["stateLayout"]["editorial"][0]["beforeSha256"] = "0" * 64
            elif mode == "new_edge": changed["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
            elif mode == "source": changed["pages"][2]["blocks"][3]["initialCode"] += "// other source\n"
            elif mode == "answer": changed["pages"][4]["blocks"][1]["answers"] = ["Error escritura"]
            else: changed["stateLayout"]["family"]["fr"]["sha256"] = "0" * 64
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                fixture.verify_current_pair(self.current_before, changed, self.current_family)
        changed_family = deepcopy(self.current_family); changed_family["fr"]["title"] += " altered"
        with self.assertRaises(ValueError): fixture.verify_current_pair(self.current_before, self.current, changed_family)

    def test_current_export_is_separate_idempotent_and_keeps_the_original_manifest_byte_exact(self):
        active = [fixture.ROOT / (fixture.PREFIX.format(locale=locale) + fixture.LESSON) for locale in ("es", "en", "fr")]
        originals = {path: path.read_bytes() for path in active}
        with tempfile.TemporaryDirectory(prefix="current-export-", dir=self.qa) as temporary:
            root = Path(temporary); old_dir = root / "adoption"; new_dir = root / "current"
            old_manifest = fixture.export(old_dir)
            self.assertEqual("9def30f523bdb8be88b1f73a7e854cf53e5bc2d3cbd35d4b63c153949d9c5ad9", hashlib.sha256(old_manifest.read_bytes()).hexdigest())
            old_snapshot = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in old_dir.iterdir()}
            manifest = fixture.export_current(new_dir)
            new_snapshot = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in new_dir.iterdir()}
            self.assertEqual({"before.json", "after.json", "manifest.json"}, new_snapshot.keys())
            self.assertEqual(fixture.historical_bytes(), new_snapshot["before.json"][0])
            self.assertEqual(fixture.historical_current_bytes(), new_snapshot["after.json"][0])
            details = json.loads(manifest.read_bytes())
            self.assertEqual(fixture.CURRENT_AFTER_COMMIT, details["afterCommit"])
            self.assertEqual((0, 15, 25, 7), tuple(details[key] for key in ("editorialBefore", "editorialAfter", "identities", "sources")))
            self.assertEqual([0, 1], details["reconstructedPages"])
            self.assertEqual(self.current["stateLayout"]["editorial"][8:], details["laterEditorial"])
            self.assertEqual(set(fixture.PARAGRAPHS) | set(fixture.GUIDES), details["previousBlocks"].keys())
            fixture.export_current(new_dir)
            self.assertEqual(new_snapshot, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in new_dir.iterdir()})
            for exporter, target in ((fixture.export_current, old_dir), (fixture.export, new_dir)):
                with self.assertRaises(FileExistsError): exporter(target)
            self.assertEqual(old_snapshot, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in old_dir.iterdir()})
            self.assertEqual(new_snapshot, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in new_dir.iterdir()})
            cli = subprocess.run([sys.executable, "-B", str(Path(fixture.__file__)), "--current-output-dir", str(new_dir)],
                                 cwd=fixture.ROOT, capture_output=True, text=True)
            self.assertEqual(0, cli.returncode, cli.stderr)
            absent = root / "mutually-exclusive"
            cli = subprocess.run([sys.executable, "-B", str(Path(fixture.__file__)), "--output-dir", str(absent),
                                  "--current-output-dir", str(absent)], cwd=fixture.ROOT, capture_output=True, text=True)
            self.assertEqual(2, cli.returncode); self.assertFalse(absent.exists())
        self.assertEqual(originals, {path: path.read_bytes() for path in active})

    def test_current_export_conflicts_bad_inputs_links_and_foreign_roots_are_portable(self):
        with tempfile.TemporaryDirectory(prefix="current-negative-", dir=self.qa) as temporary:
            root = Path(temporary)
            for name in ("before.json", "after.json", "manifest.json"):
                target = root / name.replace(".", "-"); target.mkdir(); (target / name).write_bytes(b"keep original")
                with self.subTest(name=name), self.assertRaises(FileExistsError): fixture.export_current(target)
                self.assertEqual({name: b"keep original"}, {path.name: path.read_bytes() for path in target.iterdir()})
            target = root / "bad-input"
            with patch.object(fixture, "historical_current_bytes", return_value=b"{}"), self.assertRaises((ValueError, KeyError)):
                fixture.export_current(target)
            self.assertFalse(target.exists())
            names = ("LearnGameAndEngineDev-Codex", "LearnGameAndEngineDev-Work")
            for index, name in enumerate(names):
                parent = root / str(index); parent.mkdir()
                owner = parent / name; owner.mkdir(); (owner / ".git").write_text("QA owner", encoding="utf-8")
                other = parent / names[1-index]; other.mkdir(); (other / ".git").write_text("QA foreign", encoding="utf-8")
                with patch.object(fixture, "ROOT", owner), patch.object(fixture, "current_candidate") as authenticate, self.assertRaises(ValueError):
                    fixture.export_current(other / "out" / "qa" / "fixtures")
                authenticate.assert_not_called(); self.assertFalse((other / "out").exists())
            nested = root / "nested"; nested.mkdir(); (nested / ".git").write_text("QA nested", encoding="utf-8")
            with self.assertRaises(ValueError): fixture.export_current(nested / "out")
            self.assertFalse((nested / "out").exists())
            real = root / "real"; real.mkdir(); link = root / "link"
            try: link.symlink_to(real, target_is_directory=True)
            except OSError: return
            with self.assertRaises(ValueError): fixture.export_current(link / "fixtures")
            self.assertEqual([], list(real.iterdir()))
        for output in (fixture.ROOT, fixture.ROOT / "out", fixture.ROOT / "build", fixture.ROOT / "scripts" / "curriculum"):
            with self.subTest(output=output), self.assertRaises(ValueError): fixture.export_current(output)


if __name__ == "__main__":
    unittest.main()

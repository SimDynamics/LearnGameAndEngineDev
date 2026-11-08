"""Authenticate the published U11L16 sources without claiming native playback."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import stable_state_layout as layout
import u11_remainder_editorial_fixture_source as fixture


class PublishedRemainderEditorialFixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = {stage: fixture.published_bytes(stage) for stage in fixture.COMMITS}
        cls.documents = {stage: json.loads(payload) for stage, payload in cls.raw.items()}

    def test_complete_publications_preserve_both_editorial_prefixes_and_coordinates(self):
        fixture.validate_published_history(self.documents)
        before, published26, published30 = (self.documents[stage] for stage in fixture.COMMITS)
        self.assertEqual([17, 26, 30], [len(document["stateLayout"]["editorial"])
                                      for document in (before, published26, published30)])
        self.assertEqual(before["stateLayout"]["editorial"], published26["stateLayout"]["editorial"][:17])
        self.assertEqual(published26["stateLayout"]["editorial"], published30["stateLayout"]["editorial"][:26])
        for stage, document in self.documents.items():
            with self.subTest(stage=stage):
                self.assertEqual(fixture.SOURCE_HASHES[stage], hashlib.sha256(self.raw[stage]).hexdigest())
                self.assertEqual(layout.validate(before), layout.validate(document))
                self.assertEqual(before["stateLayout"]["origin"], document["stateLayout"]["origin"])
                self.assertEqual(before["stateLayout"]["family"], document["stateLayout"]["family"])
                self.assertEqual((2, 2), layout.validate(document)[(2, 2)])
        # Complete publications keep the real changes on other pages as well.
        # Reducing the fixture to the focal explanation would lose this source.
        self.assertNotIn("compilerRevisions", before["stateLayout"])
        self.assertTrue(published26["stateLayout"]["compilerRevisions"])
        self.assertEqual(published26["stateLayout"]["compilerRevisions"], published30["stateLayout"]["compilerRevisions"])

    def test_first_publication_changes_only_the_explanation_on_page_three(self):
        before, published26 = (self.documents[stage] for stage in ("before", "published26"))
        original = before["pages"][2]["blocks"][2]
        revised = published26["pages"][2]["blocks"][2]
        self.assertEqual("order_code", original["type"])
        self.assertEqual([1, 2, 3, 0], original["correctOrder"])
        self.assertEqual([
            "if (derecha == 0) {", '    throw std::runtime_error("Divisor cero");', "}",
            "izquierda = resultado_en_dominio(izquierda % derecha);",
        ], [original["fragments"][index] for index in original["correctOrder"]])
        self.assertEqual({"explanation"}, {key for key in original.keys() | revised.keys()
                                           if original.get(key) != revised.get(key)})
        self.assertEqual(layout.state_fingerprint(original), layout.state_fingerprint(revised))
        expected = deepcopy(before["pages"][2])
        expected["blocks"][2]["explanation"] = revised["explanation"]
        self.assertEqual(expected, published26["pages"][2])
        self.assertEqual({
            "id": "block_0002_0002", "beforeSha256": layout.fingerprint(original),
            "afterSha256": layout.fingerprint(revised),
        }, published26["stateLayout"]["editorial"][19])

    def test_later_publication_changes_the_preceding_paragraph_and_its_semantics(self):
        published26, published30 = (self.documents[stage] for stage in ("published26", "published30"))
        self.assertEqual(published26["pages"][2]["blocks"][2:], published30["pages"][2]["blocks"][2:])
        self.assertEqual(published26["pages"][2]["blocks"][0], published30["pages"][2]["blocks"][0])
        old, new = (document["pages"][2]["blocks"][1] for document in (published26, published30))
        self.assertEqual({"text"}, {key for key in old.keys() | new.keys() if old.get(key) != new.get(key)})
        self.assertNotIn("textSemantics", published26)
        focal = [field for field in published30["textSemantics"]["fields"]
                 if field["block"].startswith("block_0002_")]
        self.assertEqual([("block_0002_0001", "/text")], [(field["block"], field["path"]) for field in focal])
        self.assertEqual(hashlib.sha256(new["text"].encode("utf-8")).hexdigest(), focal[0]["sourceSha256"])
        self.assertEqual("block_0002_0001", published30["stateLayout"]["editorial"][-1]["id"])

    def test_sources_are_pinned_local_git_objects_with_verified_commit_ancestry(self):
        run = subprocess.check_output
        with patch.object(fixture.subprocess, "check_output", wraps=run) as invoked:
            self.assertEqual(self.documents, fixture.published_documents())
        commands = [call.args[0][4:] for call in invoked.call_args_list]
        self.assertEqual([["show", f"{commit}:{fixture.RELATIVE}"] for commit in fixture.COMMITS.values()], commands[:3])
        self.assertEqual([
            ["merge-base", "--is-ancestor", fixture.COMMITS["before"], fixture.COMMITS["published26"]],
            ["merge-base", "--is-ancestor", fixture.COMMITS["published26"], fixture.COMMITS["published30"]],
        ], commands[3:])
        for call in invoked.call_args_list:
            self.assertEqual(fixture.ROOT, call.kwargs["cwd"])
            for key in ("GIT_NO_LAZY_FETCH", "GIT_NO_REPLACE_OBJECTS"):
                self.assertEqual("1", call.kwargs["env"][key])
            self.assertEqual("0", call.kwargs["env"]["GIT_TERMINAL_PROMPT"])

    def test_unknown_tampered_or_swapped_source_stages_are_rejected(self):
        with patch.object(fixture.subprocess, "check_output") as invoked:
            for invalid in ("after", "candidate", "Codex", "current"):
                with self.subTest(stage=invalid), self.assertRaisesRegex(ValueError, "Unknown published"):
                    fixture.published_bytes(invalid)
            invoked.assert_not_called()
        for stage in fixture.COMMITS:
            other = "published26" if stage == "before" else "before"
            for payload in (self.raw[stage] + b"\n", self.raw[other]):
                with self.subTest(stage=stage), patch.object(fixture.subprocess, "check_output", return_value=payload):
                    with self.assertRaisesRegex(ValueError, "pinned Git bytes"):
                        fixture.published_bytes(stage)

    def test_modified_order_contract_page_history_and_other_sources_are_rejected(self):
        for mutation in ("question", "fragments", "correctOrder", "reviewFeedback", "activity_id", "explanation",
                         "paragraph", "coordinates", "origin", "signature", "prefix", "missing_edge",
                         "semantics", "other_page", "title"):
            documents = deepcopy(self.documents)
            altered = documents["published30"]
            target = altered["pages"][2]["blocks"][2]
            with self.subTest(mutation=mutation):
                if mutation in ("question", "reviewFeedback", "explanation"):
                    target[mutation] += " Otra instrucción."
                elif mutation == "fragments": target["fragments"][0] = "izquierda %= derecha;"
                elif mutation == "correctOrder": target["correctOrder"] = [0, 1, 2, 3]
                elif mutation == "activity_id": target["id"] += "_different"
                elif mutation == "paragraph": altered["pages"][2]["blocks"][1]["text"] += " Otra causa."
                elif mutation == "coordinates": altered["stateLayout"]["current"][2]["blocks"].reverse()
                elif mutation == "origin": altered["stateLayout"]["origin"][2]["blocks"][2]["sha256"] = "0" * 64
                elif mutation == "signature": altered["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                elif mutation == "prefix": altered["stateLayout"]["editorial"][0]["beforeSha256"] = "0" * 64
                elif mutation == "missing_edge": altered["stateLayout"]["editorial"].pop()
                elif mutation == "semantics": altered["textSemantics"]["fields"][-1]["spans"] = []
                elif mutation == "other_page": altered["pages"][0]["blocks"][0]["text"] = "Cambio ajeno."
                else: altered["title"] += " changed"
                with self.assertRaises((layout.LayoutError, ValueError)):
                    fixture.validate_published_history(documents)

    def test_a_reconstructed_candidate_or_wrong_publication_cannot_replace_a_stage(self):
        for stage in ("published26", "published30"):
            documents = deepcopy(self.documents)
            documents[stage] = deepcopy(documents["before"])
            blocks = layout._blocks(documents[stage])
            blocks["block_0002_0002"]["explanation"] = fixture.EXPLANATIONS["after"]
            documents[stage]["stateLayout"]["editorial"].append({
                "id": "block_0002_0002", "beforeSha256": fixture.TARGET["before_sha256"],
                "afterSha256": fixture.TARGET["after_sha256"],
            })
            with self.subTest(stage=stage), self.assertRaises(ValueError):
                fixture.validate_published_history(documents)
        documents = deepcopy(self.documents)
        documents["published26"], documents["published30"] = documents["published30"], documents["published26"]
        with self.assertRaises(ValueError):
            fixture.validate_published_history(documents)
        del documents["published30"]
        with self.assertRaisesRegex(ValueError, "three named publication"):
            fixture.validate_published_history(documents)

    def test_export_preserves_exact_bytes_manifest_and_timestamps_on_repeat(self):
        asset = fixture.ROOT / fixture.RELATIVE
        original_asset = (asset.read_bytes(), asset.stat().st_mtime_ns)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "published"
            manifest_path = fixture.export(output)
            manifest = json.loads(manifest_path.read_bytes())
            self.assertEqual("published-remainder-editorial-history", manifest["kind"])
            self.assertEqual(0, manifest["coordinateBase"])
            self.assertEqual([2, 2], manifest["target"]["historical"])
            self.assertEqual([2, 1], manifest["preceding"]["historical"])
            self.assertEqual("published30", manifest["preceding"]["changedAt"])
            self.assertEqual(fixture.ORIGINAL_SIGNATURE, manifest["originalSignature"])
            for stage, count in (("before", 17), ("published26", 26), ("published30", 30)):
                source = manifest["publications"][stage]
                self.assertEqual(fixture.COMMITS[stage], source["commit"])
                self.assertEqual(count, source["editorialCount"])
                self.assertEqual(self.raw[stage], (output / source["file"]).read_bytes())
                self.assertEqual(hashlib.sha256(self.raw[stage]).hexdigest(), source["sha256"])
            retained = output / "unrelated-evidence.txt"
            retained.write_text("Retain this independent result.", encoding="utf-8")
            original = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()}
            self.assertEqual(manifest_path, fixture.export(output))
            self.assertEqual(original, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()})
        self.assertEqual(original_asset, (asset.read_bytes(), asset.stat().st_mtime_ns))

    def test_missing_tampered_or_nonancestral_history_never_creates_output(self):
        real_git = fixture._git
        for failure in ("missing", "tampered", "nonancestral"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as folder:
                output = Path(folder) / "unwritten"

                def git(*arguments):
                    if failure == "nonancestral" and arguments[0] == "merge-base":
                        raise subprocess.CalledProcessError(1, arguments)
                    if arguments == ("show", f"{fixture.COMMITS['published30']}:{fixture.RELATIVE}"):
                        if failure == "missing": raise subprocess.CalledProcessError(128, arguments)
                        if failure == "tampered": return self.raw["published30"] + b"\n"
                    return real_git(*arguments)

                with patch.object(fixture, "_git", side_effect=git):
                    with self.assertRaises((subprocess.CalledProcessError, ValueError)):
                        fixture.export(output)
                self.assertFalse(output.exists())

    def test_conflicting_file_is_detected_before_writing_any_document(self):
        for filename in ("before.json", "published26.json", "published30.json", "manifest.json"):
            with self.subTest(filename=filename), tempfile.TemporaryDirectory() as folder:
                output = Path(folder)
                retained = output / filename
                retained.write_bytes(b"Retain the original QA evidence.\n")
                original = (retained.read_bytes(), retained.stat().st_mtime_ns)
                with self.assertRaises(FileExistsError):
                    fixture.export(output)
                self.assertEqual([retained], list(output.iterdir()))
                self.assertEqual(original, (retained.read_bytes(), retained.stat().st_mtime_ns))

    def test_source_and_other_checkout_destinations_are_rejected_before_reading_history(self):
        with patch.object(fixture, "_authenticated_payloads", side_effect=AssertionError("Destination must be checked first")):
            for output in (fixture.ROOT, fixture.ROOT / "native", fixture.ROOT / Path(fixture.RELATIVE).parent):
                with self.subTest(output=output), self.assertRaisesRegex(ValueError, "source files"):
                    fixture.export(output)
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                (root / ".git").write_text("gitdir: existing-checkout", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "another checkout"):
                    fixture.export(root / "out" / "published")
                self.assertEqual([root / ".git"], list(root.iterdir()))

    def test_linked_destination_and_existing_output_links_preserve_their_targets(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            real = root / "real"
            real.mkdir()
            link = root / "linked"
            try:
                link.symlink_to(real, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"This account cannot create a test symlink: {error}")
            with self.assertRaisesRegex(ValueError, "links or reparse points"):
                fixture.export(link / "published")
            self.assertEqual([], list(real.iterdir()))
            original = root / "original.json"
            original.write_bytes(b"Existing unrelated source.\n")
            alias = real / "published30.json"
            alias.symlink_to(original)
            with self.assertRaises(FileExistsError):
                fixture.export(real)
            self.assertEqual([alias], list(real.iterdir()))
            self.assertTrue(alias.is_symlink())
            self.assertEqual(b"Existing unrelated source.\n", original.read_bytes())


if __name__ == "__main__":
    unittest.main()

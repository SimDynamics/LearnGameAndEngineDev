"""Full-publication guards for the two joint-variable Win32 scenarios."""
from copy import deepcopy
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import joint_local_int_fixture_source as fixture
import stable_state_layout as state


class JointLocalIntFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.export_payloads()
        cls.manifest = state.parse_json(cls.payloads["manifest.json"].decode("utf-8"))
        cls.lessons = {item["id"]: item for item in cls.manifest["lessons"]}

    def document(self, name, version):
        return state.parse_json(self.payloads[f"{name}/{version}.json"].decode("utf-8"))

    def test_full_git_publications_have_independent_pins_and_reject_changed_bytes(self):
        self.assertEqual(fixture.PUBLISHED_COMMIT, "a92895f2b00242dbd338ab0a386ea8ceeda9edf8")
        for name, publication in fixture.PUBLICATIONS.items():
            with self.subTest(lesson=name):
                original = self.payloads[f"{name}/before.json"]
                self.assertEqual(hashlib.sha256(original).hexdigest(), publication["sha256"])
                self.assertEqual(fixture.verify_published_bytes(name, original), original)
                for invalid in (original + b" ", self.payloads[f"{name}/after.json"]):
                    with self.assertRaises(ValueError):
                        fixture.verify_published_bytes(name, invalid)
                with mock.patch.object(fixture.subprocess, "check_output", return_value=original) as read:
                    self.assertEqual(fixture.published_bytes(name), original)
                args, kwargs = read.call_args
                self.assertEqual(args[0], ["git", "--no-optional-locks", "-c",
                    f"safe.directory={fixture.ROOT}", "show",
                    f"{fixture.PUBLISHED_COMMIT}:{publication['path']}"])
                self.assertEqual(kwargs["cwd"], fixture.ROOT)
                self.assertEqual(kwargs["env"]["GIT_NO_LAZY_FETCH"], "1")
                self.assertEqual(kwargs["env"]["GIT_NO_REPLACE_OBJECTS"], "1")
                self.assertEqual(kwargs["env"]["GIT_TERMINAL_PROMPT"], "0")

    def test_both_real_transitions_append_one_pair_and_preserve_every_old_field(self):
        for name, publication in fixture.PUBLICATIONS.items():
            with self.subTest(lesson=name):
                before, after = self.document(name, "before"), self.document(name, "after")
                state.validate_transition(before, after)
                activity = publication["activity"]
                previous = next(item for item in before["compilerLocalIntChecks"]
                                if item["activity"] == activity)["probes"]
                current = next(item for item in after["compilerLocalIntChecks"]
                               if item["activity"] == activity)["probes"]
                self.assertEqual(len(previous), {"u3l7": 3, "u3l8": 8}[name])
                self.assertEqual(current[:-1], previous)
                self.assertEqual(current[-1], publication["probe"])
                preserved = deepcopy(after)
                next(item for item in preserved["compilerLocalIntChecks"]
                     if item["activity"] == activity)["probes"].pop()
                self.assertEqual(preserved, before)
                self.assertEqual(before["stateLayout"], after["stateLayout"])
                self.assertEqual(state._blocks(before), state._blocks(after))
                self.assertTrue(before["compilerPuzzlesRequired"])
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, before)
                rewritten = deepcopy(after)
                next(item for item in rewritten["compilerLocalIntChecks"]
                     if item["activity"] == activity)["probes"][0]["replacement"] = 2
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(before, rewritten)

    def test_manifest_authenticates_both_owners_four_references_and_precise_negatives(self):
        self.assertEqual(set(self.manifest), {"version", "kind", "publishedCommit", "lessons"})
        self.assertEqual(self.manifest["kind"], "joint-local-int")
        self.assertEqual(self.manifest["version"], 1)
        self.assertEqual(self.manifest["publishedCommit"], fixture.PUBLISHED_COMMIT)
        self.assertEqual(tuple(self.lessons), ("u3l7", "u3l8"))
        self.assertEqual(len(self.payloads), 5)
        solutions = 0
        for name, lesson in self.lessons.items():
            with self.subTest(lesson=name):
                publication = fixture.PUBLICATIONS[name]
                blocks = state._blocks(self.document(name, "before"))
                for version in ("before", "after"):
                    self.assertEqual(lesson[version], f"{name}/{version}.json")
                    self.assertEqual(lesson[version + "Sha256"],
                                     hashlib.sha256(self.payloads[lesson[version]]).hexdigest())
                self.assertEqual({item["activity"] for item in lesson["solutions"]},
                                 set(publication["references"]))
                for solution in lesson["solutions"]:
                    reference = publication["references"][solution["activity"]]
                    self.assertEqual(solution["source"], blocks[reference]["code"])
                    self.assertEqual(solution["sourceSha256"],
                                     hashlib.sha256(solution["source"].encode("utf-8")).hexdigest())
                    solutions += 1
                self.assertEqual(len(lesson["plans"]), 1)
                plan = lesson["plans"][0]
                self.assertEqual(plan["activity"], publication["activity"])
                self.assertEqual(plan["reference"], publication["references"][plan["activity"]])
                original = blocks[plan["reference"]]["code"]
                self.assertEqual(plan["fixedSource"], fixture.negative_source(name, original))
                self.assertNotEqual(plan["fixedSource"], original)
                self.assertEqual(plan["fixedSourceSha256"],
                                 hashlib.sha256(plan["fixedSource"].encode("utf-8")).hexdigest())
                with self.assertRaises(ValueError):
                    fixture.negative_source(name, original + original)
                with self.assertRaises(ValueError):
                    fixture.negative_source(name, "int main() {}\n")
                first = blocks[plan["activity"]]["outputCheck"]["cases"]
                self.assertEqual(len(first), 1)
                self.assertEqual(first[0]["standardInput"], "")
                self.assertEqual(first[0]["outputs"][1], first[0]["outputs"][0] + "\n")
        self.assertEqual(solutions, 4)

    def test_export_preserves_exact_bytes_and_existing_mtimes(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "joint"
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                self.assertEqual(fixture.export(output), output.resolve() / "manifest.json")
                original = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                            for name in self.payloads}
                self.assertEqual(fixture.export(output), output.resolve() / "manifest.json")
            for name, (payload, modified) in original.items():
                self.assertEqual(payload, self.payloads[name])
                self.assertEqual((output / name).read_bytes(), payload)
                self.assertEqual((output / name).stat().st_mtime_ns, modified)

    def test_last_destination_conflict_is_rejected_before_writing_other_files(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "joint"
            conflict = output / "u3l8/after.json"
            conflict.parent.mkdir(parents=True)
            conflict.write_bytes(b"keep these existing bytes")
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                with self.assertRaises(FileExistsError):
                    fixture.export(output)
            self.assertEqual({path.relative_to(output).as_posix() for path in output.rglob("*")
                              if path.is_file()}, {"u3l8/after.json"})
            self.assertEqual(conflict.read_bytes(), b"keep these existing bytes")

    def test_corrupt_second_publication_cannot_leave_partial_outputs(self):
        by_ref = {fixture.PUBLISHED_COMMIT + ":" + publication["path"]:
                  self.payloads[f"{name}/before.json"]
                  for name, publication in fixture.PUBLICATIONS.items()}
        by_ref[fixture.PUBLISHED_COMMIT + ":" + fixture.PUBLICATIONS["u3l8"]["path"]] += b" "
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "joint"
            with mock.patch.object(fixture.subprocess, "check_output",
                                   side_effect=lambda command, **_kwargs: by_ref[command[-1]]):
                with self.assertRaisesRegex(ValueError, "u3l8"):
                    fixture.export(output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()

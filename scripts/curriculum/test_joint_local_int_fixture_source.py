"""Full-publication and decision-oracle guards for joint-variable Win32 QA.

These are authoring/transport tests, not executions of the C++ examples.
"""
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
        self.assertEqual(fixture.publication_commit("u3l5"), "08a11877a2d2c0bf96a846660e3baea69e53f758")
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
                    f"{fixture.publication_commit(name)}:{publication['path']}"])
                self.assertEqual(kwargs["cwd"], fixture.ROOT)
                self.assertEqual(kwargs["env"]["GIT_NO_LAZY_FETCH"], "1")
                self.assertEqual(kwargs["env"]["GIT_NO_REPLACE_OBJECTS"], "1")
                self.assertEqual(kwargs["env"]["GIT_TERMINAL_PROMPT"], "0")
                if name == "u3l5":
                    self.assertEqual(hashlib.sha1(b"blob " + str(len(original)).encode("ascii") +
                                                 b"\0" + original).hexdigest(),
                                     "c0e67f1c87127e974decb0396bc3adfe33526a9a")

    def test_three_real_transitions_append_only_joint_scenarios_and_preserve_every_old_field(self):
        for name, publication in fixture.PUBLICATIONS.items():
            with self.subTest(lesson=name):
                before, after = self.document(name, "before"), self.document(name, "after")
                state.validate_transition(before, after)
                activity = publication["activity"]
                previous = next(item for item in before["compilerLocalIntChecks"]
                                if item["activity"] == activity)["probes"]
                current = next(item for item in after["compilerLocalIntChecks"]
                               if item["activity"] == activity)["probes"]
                self.assertEqual(len(previous), {"u3l7": 3, "u3l8": 8, "u3l5": 3}[name])
                additions = list(publication["probes"]) if "probes" in publication else [publication["probe"]]
                self.assertEqual(current[:len(previous)], previous)
                self.assertEqual(current[len(previous):], additions)
                preserved = deepcopy(after)
                remaining = next(item for item in preserved["compilerLocalIntChecks"]
                                 if item["activity"] == activity)["probes"]
                del remaining[len(previous):]
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

    def test_manifest_authenticates_three_owners_six_references_and_precise_negatives(self):
        self.assertEqual(set(self.manifest), {"version", "kind", "publishedCommit", "lessons"})
        self.assertEqual(self.manifest["kind"], "joint-local-int")
        self.assertEqual(self.manifest["version"], 1)
        self.assertEqual(self.manifest["publishedCommit"], fixture.PUBLISHED_COMMIT)
        self.assertEqual(tuple(self.lessons), ("u3l7", "u3l8", "u3l5"))
        self.assertEqual(len(self.payloads), 7)
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
                if name == "u3l5":
                    self.assertEqual(lesson["publishedCommit"], fixture.publication_commit(name))
                else:
                    self.assertNotIn("publishedCommit", lesson)
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
        self.assertEqual(solutions, 6)

    def test_historical_exports_and_manifest_entries_remain_byte_equivalent(self):
        # Frozen before extending this exporter: not recalculated from the
        # new declarations. Both old AFTER fixtures and their metadata stay
        # usable with the exact native pins already verified for U3L7/U3L8.
        expected_payloads = {
            "u3l7/before.json": "132e947149b4388fcb0881792dc4054e2592f1a15c64a5c0f491d0f570ad541f",
            "u3l7/after.json": "b4a05a8ddd852b7015d132237d1f2a185b745b139269e543822466480047dce3",
            "u3l8/before.json": "faf26c9f5443a650c06fd5b73c882de3ba7cf03885cb0758202cd58257ec98a1",
            "u3l8/after.json": "e90ebf9f573981bf1045ddebc7f6130d6f3eceb65b29b1f1290355b677ddbe5b",
            "u3l5/before.json": "d5d696e9f88ccdaa9b997d00bcd93c677f8e6af861eaef82e9409d3374725c67",
            "u3l5/after.json": "5a069c2dacbcf2898c82e3bebed50216c7d309ef7333496c82dabb1e94f29d92",
            "manifest.json": "6504585691bdd2eb4f800f0917adccaba7fd15a950435fc00c632ba98082597f",
        }
        for path, digest in expected_payloads.items():
            self.assertEqual(hashlib.sha256(self.payloads[path]).hexdigest(), digest, path)
        expected_entries = {
            "u3l7": "d86bb97bc74532fc47d430851525ff03b830b0b6ee865d76e99ef4e6af04607b",
            "u3l8": "03d81bad99ebd8a791788c6ffc86ff837768eb70843b4d70e7989c0dc00c2b27",
        }
        for name, digest in expected_entries.items():
            self.assertEqual(hashlib.sha256(fixture.json_bytes(self.lessons[name])).hexdigest(), digest)
        historical = deepcopy(self.manifest)
        historical["lessons"] = historical["lessons"][:2]
        self.assertEqual(hashlib.sha256(fixture.json_bytes(historical)).hexdigest(),
                         "9d724b101145207611de9e06181c44d0c2122e077a36ab5bcebc07e83937b28f")

    def test_u3l5_scenarios_reject_distinct_false_positives_with_exact_full_sources(self):
        before, after = self.document("u3l5", "before"), self.document("u3l5", "after")
        lesson = self.lessons["u3l5"]
        plan = lesson["plans"][0]
        self.assertEqual(plan["originalProbeCount"], 3)
        activity = "p1u3l5_reparar_parentesis"
        self.assertEqual(plan["activity"], activity)
        self.assertEqual(plan["reference"], "p1u3l5_ejemplo_reparacion")
        checks = next(item for item in after["compilerLocalIntChecks"] if item["activity"] == activity)["probes"]
        self.assertEqual(checks[3:], [
            {"variable": "pase", "replacement": 0,
             "additionalReplacements": [{"variable": "invitacion", "replacement": 1},
                                        {"variable": "apertura", "replacement": 1}],
             "outputs": ["Entrada permitida\nDatos: 0 1 1", "Entrada permitida\nDatos: 0 1 1\n"]},
            {"variable": "pase", "replacement": 0,
             "additionalReplacements": [{"variable": "apertura", "replacement": 1}],
             "outputs": ["Entrada denegada\nDatos: 0 0 1", "Entrada denegada\nDatos: 0 0 1\n"]},
        ])
        blocks = state._blocks(before)
        source = blocks[plan["reference"]]["code"]
        self.assertEqual(hashlib.sha256(source.encode("utf-8")).hexdigest(),
                         "f26acbb7524bf3761fd46912e517228359308ce4c50946a783f291db2d772d45")
        declaration = "    const bool permitido = (tiene_pase || tiene_invitacion) && sala_abierta;"
        self.assertEqual(source.count(declaration), 1)
        expected = {
            "omits_invitation": ("tiene_pase && sala_abierta", 3,
                "f7496334ce93aa0daf943cd962a11659b50df77f3d231aea7e3742fc9ae8ae1a"),
            "open_only": ("sala_abierta", 4,
                "66951ed1a36227f4723585864efb290d2c35a8576db57d124bc3f05a97f7b8f1"),
        }
        examples = plan["counterexamples"]
        self.assertEqual([item["label"] for item in examples], list(expected))
        baseline = {"pase": 1, "invitacion": 0, "apertura": 0}

        def reported(values, mode):
            # Independent truth-table oracle for the fixture; no C++ process
            # or native proof is fabricated by this authoring test.
            pase, invitacion, apertura = (values[key] != 0 for key in baseline)
            permitted = ((pase or invitacion) and apertura if mode == "reference" else
                         pase and apertura if mode == "omits_invitation" else apertura)
            decision = "Entrada permitida" if permitted else "Entrada denegada"
            return decision + "\nDatos: " + " ".join(str(values[key]) for key in baseline)

        public = blocks[activity]["outputCheck"]["cases"]
        self.assertEqual(public, [{"standardInput": "", "outputs": [
            "Entrada denegada\nDatos: 1 0 0", "Entrada denegada\nDatos: 1 0 0\n"]}])
        variants = []
        for probe in checks:
            values = dict(baseline)
            values[probe["variable"]] = probe["replacement"]
            for additional in probe.get("additionalReplacements", ()):
                values[additional["variable"]] = additional["replacement"]
            variants.append(values)
            result = reported(values, "reference")
            self.assertEqual(probe["outputs"], [result, result + "\n"])
        for example in examples:
            label = example["label"]
            expression, failed_probe, digest = expected[label]
            self.assertEqual(set(example), {"label", "source", "sourceSha256", "failedProbeIndex"})
            self.assertEqual(example["source"], source.replace(declaration,
                "    const bool permitido = " + expression + ";", 1))
            self.assertEqual(example["sourceSha256"], digest)
            self.assertEqual(hashlib.sha256(example["source"].encode("utf-8")).hexdigest(), digest)
            self.assertEqual(example["failedProbeIndex"], failed_probe)
            self.assertEqual(reported(baseline, label), public[0]["outputs"][0])
            self.assertEqual([index for index, values in enumerate(variants)
                              if reported(values, label) not in checks[index]["outputs"]], [failed_probe])
        self.assertEqual(plan["fixedSource"], examples[1]["source"])
        self.assertEqual(plan["fixedSourceSha256"], examples[1]["sourceSha256"])

    def test_u3l5_changed_reference_missing_commit_and_wrong_blob_fail_closed(self):
        lesson = self.lessons["u3l5"]
        source = next(solution["source"] for solution in lesson["solutions"]
                      if solution["activity"] == "p1u3l5_reparar_parentesis")
        for changed in (source + "\n", source + source, "int main() {}\n"):
            with self.assertRaises(ValueError):
                fixture.u3l5_counterexamples(changed)
        publication = fixture.PUBLICATIONS["u3l5"]
        with mock.patch.dict(publication, {"blobSha": "0" * 40}):
            with self.assertRaisesRegex(ValueError, "Git blob"):
                fixture.verify_published_bytes("u3l5", self.payloads["u3l5/before.json"])
        without_commit = {key: value for key, value in publication.items() if key != "publishedCommit"}
        with mock.patch.dict(publication, without_commit, clear=True):
            with self.assertRaises(KeyError):
                fixture.publication_commit("u3l5")

    def test_u3l5_unavailable_pinned_git_source_has_no_fetch_or_asset_fallback(self):
        failure = fixture.subprocess.CalledProcessError(1, ["git", "show"])
        with mock.patch.object(fixture.subprocess, "check_output", side_effect=failure) as read:
            with self.assertRaises(fixture.subprocess.CalledProcessError):
                fixture.published_bytes("u3l5")
        read.assert_called_once()
        self.assertEqual(read.call_args.args[0][-1],
            "08a11877a2d2c0bf96a846660e3baea69e53f758:" + fixture.PUBLICATIONS["u3l5"]["path"])

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
            conflict = output / "u3l5/after.json"
            conflict.parent.mkdir(parents=True)
            conflict.write_bytes(b"keep these existing bytes")
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                with self.assertRaises(FileExistsError):
                    fixture.export(output)
            self.assertEqual({path.relative_to(output).as_posix() for path in output.rglob("*")
                              if path.is_file()}, {"u3l5/after.json"})
            self.assertEqual(conflict.read_bytes(), b"keep these existing bytes")

    def test_corrupt_later_publication_cannot_leave_partial_outputs(self):
        for corrupted in ("u3l8", "u3l5"):
            with self.subTest(lesson=corrupted):
                by_ref = {fixture.publication_commit(name) + ":" + publication["path"]:
                          self.payloads[f"{name}/before.json"]
                          for name, publication in fixture.PUBLICATIONS.items()}
                by_ref[fixture.publication_commit(corrupted) + ":" +
                       fixture.PUBLICATIONS[corrupted]["path"]] += b" "
                with tempfile.TemporaryDirectory() as directory:
                    output = Path(directory) / "joint"
                    with mock.patch.object(fixture.subprocess, "check_output",
                                           side_effect=lambda command, **_kwargs: by_ref[command[-1]]):
                        with self.assertRaisesRegex(ValueError, corrupted):
                            fixture.export(output)
                    self.assertFalse(output.exists())


class ActiveJointLocalIntFixtureTests(unittest.TestCase):
    PINNED = {
        "u3l7": (
            "338242533d1384605ddd9a47f244cbd6efa732b3701aa4f7d2a9d6dc616cd5b3",
            "67bd6a96aae754657d9788bcfd69f1abc79b3aad", 3, 1,
            {"block_0003_0003": "block_0003_0006", "block_0007_0002": "block_0007_0005"}),
        "u3l8": (
            "1ff0efacbfd0b9f57e4bae956e17a4e9c76a287505e34e4b82c33db067261bb7",
            "437370cdb893856ba6c5cce707699c19ea51f000", 8, 1,
            {"block_0002_0002": "block_0002_0006", "block_0005_0002": "block_0005_0010"}),
        "u3l5": (
            "6ce6bbb557ec2180c5b872d8a8b16cc9ed11906f63ec89afc5165263ba56a9b4",
            "d8fb5d4503abaa6cba5906ca4a2602626dcd1e14", 3, 4,
            {"block_0003_0002": "block_0003_0005", "block_0005_0002": "block_0005_0006"}),
    }

    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.active_payloads()
        cls.manifest = state.parse_json(cls.payloads["manifest.json"].decode("utf-8"))
        cls.lessons = {item["id"]: item for item in cls.manifest["lessons"]}

    def document(self, name, version):
        return state.parse_json(self.payloads[f"{name}/{version}.json"].decode("utf-8"))

    def test_all_active_publications_require_both_hashes_and_the_exact_read_only_git_pin(self):
        commit = "39ba3cb196e196bac99c2d891b488f765679ad6d"
        self.assertEqual(fixture.ACTIVE_COMMIT, commit)
        for name, (digest, blob, _, _, _) in self.PINNED.items():
            with self.subTest(lesson=name):
                original = self.payloads[f"{name}/before.json"]
                publication = fixture.ACTIVE_PUBLICATIONS[name]
                self.assertEqual(fixture.publication_commit(name, active=True), commit)
                self.assertEqual(publication["sha256"], digest)
                self.assertEqual(publication["blobSha"], blob)
                self.assertEqual(hashlib.sha256(original).hexdigest(), digest)
                self.assertEqual(hashlib.sha1(b"blob " + str(len(original)).encode("ascii") +
                                             b"\0" + original).hexdigest(), blob)
                self.assertEqual(fixture.verify_published_bytes(name, original, active=True), original)
                archived = fixture.published_bytes(name)
                for wrong in (original + b" ", original[:-1], archived,
                              self.payloads[f"{name}/after.json"]):
                    with self.assertRaises(ValueError):
                        fixture.verify_published_bytes(name, wrong, active=True)
                for field in ("sha256", "blobSha"):
                    with mock.patch.dict(publication, {field: "0" * len(publication[field])}):
                        with self.assertRaises(ValueError):
                            fixture.verify_published_bytes(name, original, active=True)
                with mock.patch.object(fixture.subprocess, "check_output", return_value=original) as read:
                    self.assertEqual(fixture.published_bytes(name, active=True), original)
                command, = read.call_args.args
                self.assertEqual(command, ["git", "--no-optional-locks", "-c",
                    f"safe.directory={fixture.ROOT}", "show", commit + ":" + publication["path"]])
                self.assertIn("/edition_es_1__expresiones_y_decisiones/", command[-1])
                self.assertEqual(read.call_args.kwargs["cwd"], fixture.ROOT)
                for key in ("GIT_NO_LAZY_FETCH", "GIT_NO_REPLACE_OBJECTS"):
                    self.assertEqual(read.call_args.kwargs["env"][key], "1")
                self.assertEqual(read.call_args.kwargs["env"]["GIT_TERMINAL_PROMPT"], "0")
                error = fixture.subprocess.CalledProcessError(128, command)
                with mock.patch.object(fixture.subprocess, "check_output", side_effect=error) as read:
                    with self.assertRaises(fixture.subprocess.CalledProcessError):
                        fixture.published_bytes(name, active=True)
                read.assert_called_once()

    def test_active_manifest_keeps_every_real_owner_reference_and_field_outside_the_suffix(self):
        self.assertEqual(set(self.manifest), {"version", "kind", "publishedCommit", "lessons"})
        self.assertEqual((self.manifest["version"], self.manifest["kind"]), (1, "active-joint-local-int"))
        self.assertEqual(self.manifest["publishedCommit"], fixture.ACTIVE_COMMIT)
        self.assertEqual(tuple(self.lessons), ("u3l7", "u3l8", "u3l5"))
        self.assertEqual(len(self.payloads), 7)
        for name, (_, _, count, added, references) in self.PINNED.items():
            with self.subTest(lesson=name):
                before, after = self.document(name, "before"), self.document(name, "after")
                blocks = state._blocks(before)
                lesson = self.lessons[name]
                plan, = lesson["plans"]
                owner = next(iter(references))
                self.assertEqual(lesson["publishedCommit"], fixture.ACTIVE_COMMIT)
                self.assertEqual((plan["activity"], plan["reference"]), (owner, references[owner]))
                self.assertEqual({identity for identity, block in blocks.items()
                                  if block.get("type") == "cpp_compiler"}, set(references))
                self.assertEqual({item["activity"] for item in lesson["solutions"]}, set(references))
                for solution in lesson["solutions"]:
                    source = blocks[references[solution["activity"]]]["code"]
                    self.assertEqual(solution["source"], source)
                    self.assertEqual(solution["sourceSha256"], hashlib.sha256(source.encode("utf-8")).hexdigest())
                original = next(item for item in before["compilerLocalIntChecks"]
                                if item["activity"] == owner)["probes"]
                probes = next(item for item in after["compilerLocalIntChecks"]
                              if item["activity"] == owner)["probes"]
                self.assertEqual(len(original), count)
                self.assertEqual(len(probes), count + added)
                self.assertEqual(probes[:count], original)
                self.assertTrue(all("additionalReplacements" in item for item in probes[count:]))
                if name == "u3l7":
                    self.assertEqual(probes[count:], [{
                        "variable": "solicitadas", "replacement": 6,
                        "additionalReplacements": [{"variable": "codigo_permiso", "replacement": 1}],
                        "outputs": ["Pendiente: 2", "Pendiente: 2\n"]}])
                    self.assertEqual(plan["fixedSourceSha256"],
                                     "1c58b10a3c7a7f1a4f30f2b9e47d5efdb348bdd4c5f8a85c4b9117121056e25a")
                elif name == "u3l8":
                    self.assertEqual(probes[count:], [{
                        "variable": "fichas", "replacement": 0,
                        "additionalReplacements": [{"variable": "capacidad", "replacement": 0}],
                        "outputs": ["Datos invalidos", "Datos invalidos\n"]}])
                    self.assertEqual(plan["fixedSourceSha256"],
                                     "488bd22b229fd020fdfd307d13c5d08f719f27faed1ab9a319a5faa809b3f4b8")
                self.assertEqual(plan["fixedSourceSha256"],
                                 hashlib.sha256(plan["fixedSource"].encode("utf-8")).hexdigest())
                restored = deepcopy(after)
                del next(item for item in restored["compilerLocalIntChecks"]
                         if item["activity"] == owner)["probes"][count:]
                self.assertEqual(restored, before)
                self.assertEqual(before["pages"], after["pages"])
                self.assertEqual(before["stateLayout"], after["stateLayout"])
                self.assertEqual(before["courseEdition"], {
                    "version": 1, "courseId": "cpp_fundamentals", "locale": "es",
                    "generation": 1, "id": "cpp_desde_cero_2026"})
                self.assertEqual(after["courseEdition"], before["courseEdition"])
                self.assertTrue(before["compilerPuzzlesRequired"])
                self.assertEqual(len(blocks[owner]["outputCheck"]["cases"]), 1)
                state.validate_transition(before, after)
                for version in ("before", "after"):
                    self.assertEqual(lesson[version], f"{name}/{version}.json")
                    self.assertEqual(lesson[version + "Sha256"],
                                     hashlib.sha256(self.payloads[lesson[version]]).hexdigest())

    def test_active_transition_rejects_rewritten_originals_and_removal_of_joint_observations(self):
        for name, (_, _, _, _, references) in self.PINNED.items():
            before, after = self.document(name, "before"), self.document(name, "after")
            owner = next(iter(references))
            with self.subTest(lesson=name):
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, before)
                for field, value in (("replacement", 2), ("outputs", ["forged private result"]),
                                     ("variable", "other_variable")):
                    changed = deepcopy(after)
                    probes = next(item for item in changed["compilerLocalIntChecks"]
                                  if item["activity"] == owner)["probes"]
                    probes[0][field] = value
                    with self.assertRaises(state.LayoutError):
                        state.validate_transition(before, changed)
                changed = deepcopy(after)
                state._blocks(changed)[owner]["outputCheck"]["cases"][0]["outputs"] = ["forged public result"]
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(before, changed)

    def test_active_u3l5_covers_all_eight_rows_and_each_counterexample_fails_only_its_new_row(self):
        before, after = self.document("u3l5", "before"), self.document("u3l5", "after")
        owner, reference = "block_0003_0002", "block_0003_0005"
        blocks = state._blocks(before)
        probes = next(item for item in after["compilerLocalIntChecks"] if item["activity"] == owner)["probes"]
        expected_rows = [(0, 0, 0), (1, 1, 0), (1, 0, 1),
                         (0, 1, 1), (0, 0, 1), (0, 1, 0), (1, 1, 1)]
        baseline = {"pase": 1, "invitacion": 0, "apertura": 0}
        rows = []
        for probe in probes:
            values = dict(baseline)
            values[probe["variable"]] = probe["replacement"]
            for additional in probe.get("additionalReplacements", ()):
                values[additional["variable"]] = additional["replacement"]
            rows.append(tuple(values.values()))
        self.assertEqual(rows, expected_rows)
        self.assertEqual(set(rows + [(1, 0, 0)]),
                         {(p, i, a) for p in (0, 1) for i in (0, 1) for a in (0, 1)})

        def output(row, mode):
            # Independent eight-row oracle; actual C++ execution belongs to
            # the native suite, not this authoring/transport test.
            p, i, a = map(bool, row)
            permitted = {
                "reference": (p or i) and a,
                "omits_invitation": p and a,
                "open_only": a,
                "xor_invitation": (p != i) and a,
                "invitation_ignores_closure": ((p or i) and a) or (not p and i),
            }[mode]
            return ("Entrada permitida" if permitted else "Entrada denegada") + "\nDatos: " + " ".join(map(str, row))

        public, = blocks[owner]["outputCheck"]["cases"]
        self.assertEqual(public, {"standardInput": "", "outputs": [
            "Entrada denegada\nDatos: 1 0 0", "Entrada denegada\nDatos: 1 0 0\n"]})
        for row, probe in zip(rows, probes):
            result = output(row, "reference")
            self.assertEqual(probe["outputs"], [result, result + "\n"])
        expected = {
            "omits_invitation": ("tiene_pase && sala_abierta", 3,
                "f7496334ce93aa0daf943cd962a11659b50df77f3d231aea7e3742fc9ae8ae1a"),
            "open_only": ("sala_abierta", 4,
                "66951ed1a36227f4723585864efb290d2c35a8576db57d124bc3f05a97f7b8f1"),
            "xor_invitation": ("(tiene_pase != tiene_invitacion) && sala_abierta", 6,
                "cc6e31e1aefe0d97befc6beac66193a846dca0c6d2d5feba54db2fe7083897fa"),
            "invitation_ignores_closure": (
                "((tiene_pase || tiene_invitacion) && sala_abierta) || (!tiene_pase && tiene_invitacion)", 5,
                "6c58f6807b9066b29d9d538905aa02db9a82a6e4570b5038d60adfba0c51f1aa"),
        }
        plan, = self.lessons["u3l5"]["plans"]
        self.assertEqual(plan["originalProbeCount"], 3)
        self.assertEqual([item["label"] for item in plan["counterexamples"]], list(expected))
        source = blocks[reference]["code"]
        declaration = "    const bool permitido = (tiene_pase || tiene_invitacion) && sala_abierta;"
        for example in plan["counterexamples"]:
            label = example["label"]
            expression, failed, digest = expected[label]
            self.assertEqual(set(example), {"label", "source", "sourceSha256", "failedProbeIndex"})
            self.assertEqual(example["source"], source.replace(declaration,
                "    const bool permitido = " + expression + ";", 1))
            self.assertEqual(example["sourceSha256"], digest)
            self.assertEqual(hashlib.sha256(example["source"].encode("utf-8")).hexdigest(), digest)
            self.assertEqual(example["failedProbeIndex"], failed)
            self.assertEqual(output((1, 0, 0), label), public["outputs"][0])
            self.assertEqual([index for index, row in enumerate(rows)
                              if output(row, label) not in probes[index]["outputs"]], [failed])
            if label in ("xor_invitation", "invitation_ignores_closure"):
                self.assertTrue(all(output(row, label) in probe["outputs"]
                                    for row, probe in zip(rows[:5], probes[:5])))
        self.assertEqual(plan["fixedSource"], plan["counterexamples"][1]["source"])
        self.assertEqual(blocks["block_0005_0002"]["initialStandardInput"], "1 0 0")
        self.assertEqual(len(blocks["block_0005_0002"]["outputCheck"]["cases"]), 8)
        with self.assertRaises(ValueError):
            fixture.u3l5_counterexamples(source + "\n", active=True)

    def test_active_export_is_idempotent_and_rejects_mixing_cohorts(self):
        historical = fixture.export_payloads()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active-joint"
            with mock.patch.object(fixture, "active_payloads", return_value=self.payloads):
                self.assertEqual(fixture.export(output, active=True), output.resolve() / "manifest.json")
                original = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                            for name in self.payloads}
                fixture.export(output, active=True)
            with mock.patch.object(fixture, "export_payloads", return_value=historical):
                with self.assertRaises(FileExistsError):
                    fixture.export(output)
            self.assertEqual({path.relative_to(output).as_posix()
                              for path in output.rglob("*") if path.is_file()}, set(self.payloads))
            for name, (payload, modified) in original.items():
                self.assertEqual((output / name).read_bytes(), payload)
                self.assertEqual((output / name).stat().st_mtime_ns, modified)

    def test_corrupt_last_active_publication_leaves_no_partial_output_or_historical_fallback(self):
        by_ref = {fixture.ACTIVE_COMMIT + ":" + publication["path"]:
                  self.payloads[f"{name}/before.json"]
                  for name, publication in fixture.ACTIVE_PUBLICATIONS.items()}
        by_ref[fixture.ACTIVE_COMMIT + ":" + fixture.ACTIVE_PUBLICATIONS["u3l5"]["path"]] += b" "
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active-joint"
            with mock.patch.object(fixture.subprocess, "check_output",
                                   side_effect=lambda command, **_kwargs: by_ref[command[-1]]) as read:
                with self.assertRaisesRegex(ValueError, "u3l5"):
                    fixture.export(output, active=True)
            self.assertEqual(read.call_count, 3)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()

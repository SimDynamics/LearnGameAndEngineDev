"""Closed compiler expansions keep immutable originals and reviewed output contracts."""
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "f410040c7a2b36468f36b15f6d04be7a28b347a7"
PROPOSAL_BASELINE = "c570ceb3643592d16f46c25969cf82202313efe0"
PROPOSAL_LESSONS = frozenset({"terminos_prioridad_y_division_segura",
                             "signos_unarios_parentesis_y_progreso_recursivo",
                             "diagnosticar_y_recuperar_hasta_fin_de_operacion"})
PREFIX = "app/src/main/assets/content/es/programming/cpp_fundamentals/"


def asset_path(lesson):
    return "texto_interpretacion_y_flujos/lesson_" + lesson.split("__")[-1] + ".json"


def baseline(lesson):
    # The new three are authenticated proposals from the current publication;
    # the older four retain their independent pre-integration source commits.
    return PROPOSAL_BASELINE if lesson.split("__")[-1] in PROPOSAL_LESSONS else BASELINE


@lru_cache(maxsize=None)
def published_bytes(lesson, locale="es"):
    return subprocess.check_output(["git", "show", baseline(lesson) + ":" + PREFIX.replace("/es/", "/" + locale + "/")
                                    + asset_path(lesson)], cwd=contract.ROOT)


def published(lesson, locale="es"):
    return json.loads(published_bytes(lesson, locale))


def block(document, identity):
    return layout._blocks(document)[identity]


def revised(lesson, identity):
    return layout.revise_compiler_cases(published(lesson), identity)


def replace_terminal_proof(document, identity):
    proofs = [proof for proof in document["stateLayout"]["editorial"] if proof["id"] == identity]
    proofs[-1]["afterSha256"] = layout.fingerprint(block(document, identity))


def history_block(document, identity):
    snapshot = document["stateLayout"]["retirementHistory"][0]
    return next(candidate for page, record in zip(snapshot["pages"], snapshot["current"])
                for candidate, owner in zip(page["blocks"], record["blocks"]) if owner == identity)


class CompilerCaseRevisionTest(unittest.TestCase):
    def test_three_pending_batteries_append_only_the_independently_pinned_reports_and_calls(self):
        # Pins are independent of the authoring allowlist. These remain QA
        # proposals; a report count is not a count of execution receipts.
        plans = [
            ("terminos_prioridad_y_division_segura", "block_0004_0003", "block_0008_0001", 199, 233,
             "a34d52d3096f1ea54d998807701c6d02b943f1c637aca0e2b929a8c76715ae22",
             "5bd75ba1888258cd6d02ceb255724439cbcdfbdee7f65590784caf5bd9b50915",
             '    probar("11*10/2;");\n'),
            ("signos_unarios_parentesis_y_progreso_recursivo", "block_0004_0004", "block_0009_0001", 248, 310,
             "a161812776b989acf7ff05023a94228b5dfd39f0fa6590fbb389f3b33e02e32d",
             "81607dd072f0f3c6867a4b567753b0b131b42476464a74255863dccdc9fd5d43",
             '    probar("11*10/2;");\n    probar("(((((((((1)))))))));");\n'),
            ("diagnosticar_y_recuperar_hasta_fin_de_operacion", "block_0004_0004", "block_0008_0001", 357, 406,
             "a4a5cb2f5ff65a572e8803577ac0705409598f65895167e7aa92e5de8c82ba99",
             "9e1ab256574d1c00240942829ea735907caac6af57258c972b49a1c31bc4b30e",
             '    std::cout << "Caso 11\\n";\n    probar("?z;4;");\n'),
        ]
        for name, identity, reference_id, before_size, after_size, before_sha, after_sha, calls in plans:
            with self.subTest(lesson=name):
                lesson = layout._CHOICE_PREFIX + name
                before, after = published(lesson), revised(lesson, identity)
                old, new = block(before, identity), block(after, identity)
                self.assertEqual(1, len(old["outputCheck"]["cases"]))
                self.assertEqual(1, len(new["outputCheck"]["cases"]))
                self.assertEqual("", old["outputCheck"]["cases"][0]["standardInput"])
                self.assertEqual(old.get("initialStandardInput"), new.get("initialStandardInput"))
                old_output, new_output = (item["outputCheck"]["cases"][0]["outputs"][0].encode("utf-8")
                                          for item in (old, new))
                self.assertEqual((before_size, after_size), (len(old_output), len(new_output)))
                self.assertEqual(before_sha, hashlib.sha256(old_output).hexdigest())
                self.assertEqual(after_sha, hashlib.sha256(new_output).hexdigest())
                self.assertTrue(new_output.startswith(old_output))
                tail = "    return 0;\n}\n"
                self.assertEqual(block(before, reference_id)["code"][:-len(tail)] + calls + tail,
                                 block(after, reference_id)["code"])

    def test_seven_exact_expansions_preserve_the_original_program_map_and_translations(self):
        self.assertEqual(7, len(layout.COMPILER_CASE_REVISIONS))
        for (lesson, identity), spec in layout.COMPILER_CASE_REVISIONS.items():
            with self.subTest(lesson=lesson):
                before = published(lesson); untouched = deepcopy(before)
                current = layout.revise_compiler_cases(before, identity)
                old, new = block(before, identity), block(current, identity)
                self.assertEqual(spec["before"], layout.fingerprint(old))
                self.assertEqual(spec["after"], layout.fingerprint(new))
                self.assertEqual(spec["before"], layout.state_fingerprint(new))
                self.assertEqual({key: value for key, value in old.items() if key != "outputCheck"},
                                 {key: value for key, value in new.items() if key != "outputCheck"})
                cases, original = new["outputCheck"]["cases"], old["outputCheck"]["cases"]
                if "append" in spec:
                    self.assertEqual(19, len(original)); self.assertEqual(21, len(cases))
                    self.assertEqual(original, cases[:19]); self.assertEqual(spec["append"], cases[19:])
                    self.assertTrue(all(not case["standardInput"].endswith("\n") for case in cases[19:]))
                else:
                    expected = deepcopy(old["outputCheck"])
                    expected["cases"][0]["outputs"][0] += spec["suffix"]
                    self.assertEqual(expected, new["outputCheck"])
                self.assertEqual(before, untouched)
                self.assertEqual(current, layout.revise_compiler_cases(current, identity))
                archive = {"id": identity, "block": old}
                reference = layout.COMPILER_REFERENCE_REVISIONS.get((lesson, identity))
                if reference is not None:
                    old_reference = block(before, reference["id"])
                    new_reference = block(current, reference["id"])
                    self.assertEqual(reference["before"], layout.fingerprint(old_reference))
                    self.assertEqual(reference["after"], layout.fingerprint(new_reference))
                    archive["references"] = [{"id": reference["id"], "block": old_reference}]
                    self.assertEqual(old_reference, layout.historical_compiler_reference_payload(new_reference))
                self.assertEqual([archive], current["stateLayout"]["compilerRevisions"])
                changed_ids = {identity} | ({reference["id"]} if reference else set())
                for other, previous in layout._blocks(before).items():
                    if other not in changed_ids:
                        self.assertEqual(previous, block(current, other))
                for field in ("origin", "current", "family", "additions", "retired", "retirementHistory", "orderRefinements", "choiceRevisions"):
                    self.assertEqual(before["stateLayout"].get(field), current["stateLayout"].get(field))
                self.assertEqual(layout.validate(before), layout.validate(current))
                layout.validate_transition(before, current)
                layout.check_localized_family([current, published(lesson, "en"), published(lesson, "fr")], ["es", "en", "fr"])

    def test_curricular_guard_only_accepts_the_validated_document_projection(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                before, current = published(lesson), revised(lesson, identity)
                _, page, position = identity.split("_"); page, position = int(page), int(position)
                self.assertFalse(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), page, position,
                    block(before, identity), block(current, identity), before))
                aligned = layout.align_for_previous_validation(before, current)
                self.assertTrue(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), page, position,
                    block(before, identity), block(aligned, identity), before))
                self.assertEqual(layout.original_projection(before)["pages"],
                                 layout.original_projection(current)["pages"])

    def test_v2_spanish_owner_archive_and_exact_first_editorial_edge_are_required(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            good = revised(lesson, identity)
            def no_map(doc): doc.pop("stateLayout")
            def no_archive(doc): doc["stateLayout"].pop("compilerRevisions")
            def wrong_locale(doc): doc["stateLayout"]["locale"] = "en"
            def wrong_lesson(doc): doc["id"] += "_other"
            def no_proof(doc): doc["stateLayout"]["editorial"] = [p for p in doc["stateLayout"]["editorial"] if p["id"] != identity]
            def wrong_identity(doc): doc["stateLayout"]["compilerRevisions"][0]["id"] = "block_0000_0000"
            def downgrade_v1(doc): doc["stateLayout"] = {key: value for key, value in doc["stateLayout"].items() if key in {"origin", "current"}} | {"version": 1}
            for mutate in (no_map, no_archive, wrong_locale, wrong_lesson, no_proof, wrong_identity, downgrade_v1):
                with self.subTest(lesson=lesson, mutation=mutate.__name__):
                    bad = deepcopy(good); mutate(bad)
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)
            bad = deepcopy(good); proof = next(p for p in bad["stateLayout"]["editorial"] if p["id"] == identity)
            bad["stateLayout"]["editorial"].remove(proof); middle = "0" * 64
            bad["stateLayout"]["editorial"].extend([{**proof, "afterSha256": middle}, {**proof, "beforeSha256": middle}])
            with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_archive_schema_original_bytes_and_unique_identity_are_protected(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        for value in (None, {}, [{}], [{"id": identity, "block": []}],
                      [{"id": identity, "block": block(published(lesson), identity), "other": True}]):
            with self.subTest(value=value):
                bad = revised(lesson, identity); bad["stateLayout"]["compilerRevisions"] = value
                with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = revised(lesson, identity); bad["stateLayout"]["compilerRevisions"] *= 2
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = revised(lesson, identity); bad["stateLayout"]["compilerRevisions"][0]["block"]["initialCode"] += "\n"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_sources_inputs_outputs_comparison_and_existing_case_order_stay_protected(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            for change in ("source", "input", "expected", "mode", "case", "id", "type"):
                with self.subTest(lesson=lesson, change=change):
                    bad = revised(lesson, identity); actual = block(bad, identity)
                    cases = actual["outputCheck"]["cases"]
                    if change == "source": actual["initialCode"] += "\n// a different source"
                    elif change == "input": cases[0]["standardInput"] += " "
                    elif change == "expected": cases[0]["outputs"][0] += " "
                    elif change == "mode": actual["outputCheck"]["comparison"] = "tokens"
                    elif change == "case": cases.append(deepcopy(cases[0]))
                    elif change == "id": actual["id"] = "another_compiler"
                    else: actual["type"] = "code"
                    replace_terminal_proof(bad, identity)
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        bad = revised(lesson, identity); cases = block(bad, identity)["outputCheck"]["cases"]; cases[0], cases[1] = cases[1], cases[0]
        replace_terminal_proof(bad, identity)
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_metadata_and_expansion_cannot_be_removed_on_later_publication(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            good = revised(lesson, identity)
            with self.subTest(lesson=lesson):
                with self.assertRaises(layout.LayoutError): layout.validate_transition(good, published(lesson))
                bad = deepcopy(good)
                actual = block(bad, identity); actual.clear(); actual.update(deepcopy(block(published(lesson), identity)))
                with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_retirement_snapshots_preserve_the_real_version_before_and_after_expansion(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                before = published(lesson)
                archived = layout.retire(before, ["block_0000_0001"])
                current = layout.revise_compiler_cases(archived, identity)
                layout.validate_transition(archived, current)
                self.assertEqual(block(before, identity), history_block(current, identity))
                expanded = revised(lesson, identity); current = layout.retire(expanded, ["block_0000_0001"])
                layout.validate_transition(expanded, current)
                self.assertEqual(block(expanded, identity), history_block(current, identity))

    def test_historical_expansion_cannot_be_fabricated_or_borrow_another_owner(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                forged = layout.retire(published(lesson), ["block_0000_0001"])
                target = history_block(forged, identity); target.clear(); target.update(deepcopy(block(revised(lesson, identity), identity)))
                with self.assertRaisesRegex(layout.LayoutError, "historical revised compiler"): layout.validate(forged)
                current = layout.retire(revised(lesson, identity), ["block_0000_0001"])
                snapshot = current["stateLayout"]["retirementHistory"][0]
                page, position = next((pn, record["blocks"].index(identity))
                                      for pn, record in enumerate(snapshot["current"]) if identity in record["blocks"])
                blocks = snapshot["pages"][page]["blocks"]
                blocks[position - 1], blocks[position] = blocks[position], blocks[position - 1]
                with self.assertRaises(layout.LayoutError): layout.validate(current)

    def test_future_guidance_appends_provenance_but_cannot_weaken_runtime_stderr(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                before = revised(lesson, identity); draft = deepcopy(before)
                block(draft, identity)["outputCheck"]["cases"][0]["explanation"] = "Conserva el orden de comprobación."
                block(draft, identity)["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
                family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
                current = layout.prepare_v2(draft, "es", family, previous=before)
                self.assertEqual(before["stateLayout"]["compilerRevisions"], current["stateLayout"]["compilerRevisions"])
                self.assertEqual(before["stateLayout"]["editorial"], current["stateLayout"]["editorial"][:-1])
                rollback = deepcopy(current); block(rollback, identity)["outputCheck"]["cases"][0].pop("standardError")
                rollback["stateLayout"]["editorial"].append({"id": identity, "beforeSha256": layout.fingerprint(block(current, identity)),
                    "afterSha256": layout.fingerprint(block(rollback, identity))})
                layout.validate(rollback)
                with self.assertRaisesRegex(layout.LayoutError, "stderr"): layout.validate_transition(current, rollback)

    def test_reorder_keeps_history_and_refuses_reattachment(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        current = revised(lesson, identity); plan = deepcopy(current["stateLayout"]["current"])
        _, page, position = identity.split("_"); page, position = int(page), int(position)
        plan[page]["blocks"][position - 1], plan[page]["blocks"][position] = plan[page]["blocks"][position], plan[page]["blocks"][position - 1]
        moved = layout.reorder(current, plan); layout.validate_transition(published(lesson), moved)
        self.assertEqual(current["stateLayout"]["compilerRevisions"], moved["stateLayout"]["compilerRevisions"])
        bad = deepcopy(moved); bad["stateLayout"]["compilerRevisions"][0]["id"] = "block_0003_0004"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_helper_requires_the_exact_unmodified_pre_revision_block(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        before = published(lesson)
        with self.assertRaises(layout.LayoutError): layout.revise_compiler_cases(before, "block_0000_0000")
        changed = deepcopy(before)
        block(changed, identity)["outputCheck"]["cases"][0]["explanation"] = "Otra guía publicada."
        changed["stateLayout"]["editorial"].append({"id": identity,
            "beforeSha256": layout.fingerprint(block(before, identity)),
            "afterSha256": layout.fingerprint(block(changed, identity))})
        layout.validate(changed); unchanged = deepcopy(changed)
        with self.assertRaises(layout.LayoutError): layout.revise_compiler_cases(changed, identity)
        self.assertEqual(unchanged, changed)

    def test_compiler_and_choice_revisions_keep_their_independent_archives(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        choice_id = "block_0000_0004"
        compiler_first = revised(lesson, identity)
        both = layout.revise_choice_option(compiler_first, choice_id)
        layout.validate_transition(published(lesson), both)
        self.assertEqual(compiler_first["stateLayout"]["compilerRevisions"], both["stateLayout"]["compilerRevisions"])
        choice_first = layout.revise_choice_option(published(lesson), choice_id)
        alternate = layout.revise_compiler_cases(choice_first, identity)
        layout.validate_transition(choice_first, alternate)
        self.assertEqual(choice_first["stateLayout"]["choiceRevisions"], alternate["stateLayout"]["choiceRevisions"])
        self.assertEqual(both["pages"], alternate["pages"])

    def test_retired_contract_cannot_smuggle_in_an_unarchived_expansion(self):
        for lesson, identity in layout.COMPILER_CASE_REVISIONS:
            with self.subTest(lesson=lesson):
                before = published(lesson)
                references = [link for link in before.get("referenceAfter", []) if link["activity"] != identity]
                activities = []
                for link in before.get("activityAfter", []):
                    remaining = [value for value in link["requires"] if value != identity]
                    if link["activity"] != identity and remaining:
                        activities.append({**link, "requires": remaining})
                current = layout.retire(before, [identity], reference_after=references, activity_after=activities)
                target = next(item["block"] for item in current["stateLayout"]["retired"] if item["id"] == identity)
                target.clear(); target.update(deepcopy(block(revised(lesson, identity), identity)))
                with self.assertRaisesRegex(layout.LayoutError, "active revision archive"): layout.validate(current)

    def test_new_guidance_is_not_hidden_from_the_ordinary_curricular_guard(self):
        lesson, identity = next(iter(layout.COMPILER_CASE_REVISIONS))
        before = revised(lesson, identity); draft = deepcopy(before)
        block(draft, identity)["outputCheck"]["cases"][0]["explanation"] = "Una explicación nueva."
        family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
        current = layout.prepare_v2(draft, "es", family, previous=before)
        aligned = layout.align_for_previous_validation(published(lesson), current)
        _, page, position = identity.split("_"); page, position = int(page), int(position)
        self.assertEqual("Una explicación nueva.", block(aligned, identity)["outputCheck"]["cases"][0]["explanation"])
        self.assertFalse(contract.preserves_previous_compiler(PREFIX + asset_path(lesson), page, position,
            block(published(lesson), identity), block(aligned, identity), published(lesson)))

    def test_six_reference_expansions_require_their_matching_compiler_and_immutable_archive(self):
        self.assertEqual(6, len(layout.COMPILER_REFERENCE_REVISIONS))
        for (lesson, identity), reference in layout.COMPILER_REFERENCE_REVISIONS.items():
            before, good = published(lesson), revised(lesson, identity)
            reference_id = reference["id"]
            def old_reference(doc):
                target = block(doc, reference_id); target.clear(); target.update(deepcopy(block(before, reference_id)))
            def old_compiler(doc):
                target = block(doc, identity); target.clear(); target.update(deepcopy(block(before, identity)))
            def no_reference_archive(doc): doc["stateLayout"]["compilerRevisions"][0].pop("references")
            def wrong_reference_owner(doc): doc["stateLayout"]["compilerRevisions"][0]["references"][0]["id"] = "block_0000_0000"
            def duplicate_reference(doc): doc["stateLayout"]["compilerRevisions"][0]["references"] *= 2
            def forged_original(doc): doc["stateLayout"]["compilerRevisions"][0]["references"][0]["block"]["code"] += "\n"
            def no_reference_proof(doc): doc["stateLayout"]["editorial"] = [p for p in doc["stateLayout"]["editorial"] if p["id"] != reference_id]
            for mutate in (old_reference, old_compiler, no_reference_archive, wrong_reference_owner,
                           duplicate_reference, forged_original, no_reference_proof):
                with self.subTest(lesson=lesson, mutation=mutate.__name__):
                    bad = deepcopy(good); mutate(bad)
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)
            bad = deepcopy(good); block(bad, reference_id)["code"] += "// unrelated source change\n"
            replace_terminal_proof(bad, reference_id)
            with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_reviewed_reference_alone_cannot_enter_unmapped_or_archived_presentations(self):
        for (lesson, identity), reference in layout.COMPILER_REFERENCE_REVISIONS.items():
            reference_id = reference["id"]; good = revised(lesson, identity)
            with self.subTest(lesson=lesson):
                bad = deepcopy(good); bad.pop("stateLayout")
                with self.assertRaises(layout.LayoutError): layout.validate(bad)
                old = layout.retire(published(lesson), ["block_0000_0001"])
                target = history_block(old, reference_id); target.clear(); target.update(deepcopy(block(good, reference_id)))
                with self.assertRaisesRegex(layout.LayoutError, "historical revised compiler reference"): layout.validate(old)
                current = layout.retire(good, ["block_0000_0001"])
                self.assertEqual(block(good, reference_id), history_block(current, reference_id))
                layout.validate_transition(good, current)

    def test_historical_compiler_and_reference_cannot_mix_before_and_after_versions(self):
        for (lesson, identity), reference in layout.COMPILER_REFERENCE_REVISIONS.items():
            for old_identity in (identity, reference["id"]):
                with self.subTest(lesson=lesson, old_identity=old_identity):
                    current = layout.retire(revised(lesson, identity), ["block_0000_0001"])
                    target = history_block(current, old_identity)
                    target.clear(); target.update(deepcopy(block(published(lesson), old_identity)))
                    with self.assertRaisesRegex(layout.LayoutError, "same published version"):
                        layout.validate(current)

    def test_cli_never_overwrites_source_or_an_existing_proposal(self):
        for (lesson, identity), spec in layout.COMPILER_CASE_REVISIONS.items():
            with self.subTest(lesson=lesson, source="current asset, integrated or pending proposal"):
                source = contract.ROOT / (PREFIX + asset_path(lesson)); source_bytes = source.read_bytes()
                current = json.loads(source_bytes); layout.validate(current)
                archives = [entry for entry in current["stateLayout"].get("compilerRevisions", []) if entry["id"] == identity]
                if archives:
                    self.assertEqual(1, len(archives))
                    self.assertEqual(spec["before"], layout.fingerprint(archives[0]["block"]))
                    self.assertIn({"id": identity, "beforeSha256": spec["before"], "afterSha256": spec["after"]},
                                  current["stateLayout"]["editorial"])
                    expected = current
                else:
                    self.assertIn(lesson.split("__")[-1], PROPOSAL_LESSONS)
                    self.assertEqual(spec["before"], layout.fingerprint(block(current, identity)))
                    expected = layout.revise_compiler_cases(current, identity)
                    self.assertNotEqual(current, expected)
                # Retain every subsequent editorial/choice revision from the
                # current source. Integrated revisions are no-ops; pending
                # proposals must actually expand their pinned original.
                with tempfile.TemporaryDirectory() as directory:
                    output = Path(directory) / "proposal.json"
                    command = [sys.executable, "-B", str(Path(layout.__file__)), "revise-compiler-cases", str(source),
                               "--locale", "es", "--base-ref", baseline(lesson), "--identity", identity, "--output", str(output)]
                    result = subprocess.run(command, cwd=contract.ROOT, capture_output=True, text=True, encoding="utf-8")
                    self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                    proposal = output.read_bytes(); self.assertEqual(expected, json.loads(proposal))
                    again = subprocess.run(command, cwd=contract.ROOT, capture_output=True)
                    self.assertNotEqual(0, again.returncode); self.assertEqual(proposal, output.read_bytes())
                    overwrite_source = subprocess.run(command[:-1] + [str(source)], cwd=contract.ROOT, capture_output=True)
                    self.assertNotEqual(0, overwrite_source.returncode)
                    self.assertEqual(proposal, output.read_bytes())
                self.assertEqual(source_bytes, source.read_bytes())

    def test_cli_really_expands_archived_sources_without_overwriting_their_files(self):
        for (lesson, identity), spec in layout.COMPILER_CASE_REVISIONS.items():
            with self.subTest(lesson=lesson, source="verified historical snapshot"):
                original_bytes = published_bytes(lesson); original = json.loads(original_bytes)
                layout.validate(original)
                self.assertEqual(spec["before"], layout.fingerprint(block(original, identity)))
                expected = revised(lesson, identity)
                self.assertNotEqual(original, expected)  # A no-op cannot prove adoption.
                self.assertEqual(spec["after"], layout.fingerprint(block(expected, identity)))
                layout.validate_transition(original, expected)
                with tempfile.TemporaryDirectory() as directory:
                    source = Path(directory) / "historical-source.json"
                    previous = Path(directory) / "published-reference.json"
                    output = Path(directory) / "proposal.json"
                    # New, temporary QA files only; never roll back the asset.
                    source.write_bytes(original_bytes); previous.write_bytes(original_bytes)
                    command = [sys.executable, "-B", str(Path(layout.__file__)), "revise-compiler-cases", str(source),
                               "--previous", str(previous), "--identity", identity, "--output", str(output)]
                    result = subprocess.run(command, cwd=contract.ROOT, capture_output=True, text=True, encoding="utf-8")
                    self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                    proposal = output.read_bytes(); self.assertEqual(expected, json.loads(proposal))
                    again = subprocess.run(command, cwd=contract.ROOT, capture_output=True)
                    self.assertNotEqual(0, again.returncode); self.assertEqual(proposal, output.read_bytes())
                    for protected in (source, previous):
                        overwrite = subprocess.run(command[:-1] + [str(protected)], cwd=contract.ROOT, capture_output=True)
                        self.assertNotEqual(0, overwrite.returncode)
                        self.assertEqual(original_bytes, protected.read_bytes())
                    self.assertEqual(original_bytes, source.read_bytes()); self.assertEqual(original_bytes, previous.read_bytes())
                    self.assertEqual(proposal, output.read_bytes())


if __name__ == "__main__":
    unittest.main()

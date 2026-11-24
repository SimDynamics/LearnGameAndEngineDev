"""Explicit compiler retirement preserves published contracts and state slots."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

import stable_state_layout as layout
import retirement_fixture_source as fixtures


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "app/src/main/assets/content"
UNIT = "programming/cpp_fundamentals/programas_y_ejecucion"
PLANS = (
    ("lesson_primer_programa_y_salida.json", ("ejecucion_original_consigna", "ejecucion_original_editor")),
    ("lesson_texto_secuencia_y_formato.json", ("ejecucion_original_texto_secuencia_y_formato_consigna",
                                             "ejecucion_original_texto_secuencia_y_formato_editor")),
    ("lesson_fuente_compilacion_y_diagnostico.json", ("ejecucion_original_fuente_compilacion_y_diagnostico_consigna",
                                                  "ejecucion_original_fuente_compilacion_y_diagnostico_editor")),
)
FIRST_RETIRED_COMMIT = "52d057d1aad844b133d0d26ff195cbfcbe18b2e1"
FIRST_RETIRED_SHA256 = {
    "lesson_primer_programa_y_salida.json": "449b62604d3738cc10b54ac0a0b8835f020d36f64cb120e9c9ac28ff3a62118f",
    "lesson_texto_secuencia_y_formato.json": "9b5bce191c5100cacfb7b2c32d213c1c3a71a9362b6af84454c57cd118fd9786",
    "lesson_fuente_compilacion_y_diagnostico.json": "6509a4ab4b7e00d48b10387e45f6a232ffbf6c7e4edf7dd087918de3f7f24904",
}
REFERENCE_RETIREMENTS = {
    "lesson_primer_programa_y_salida.json": ("block_0005_0002", "block_0005_0008"),
    "lesson_texto_secuencia_y_formato.json": ("block_0007_0002", "block_0007_0003"),
}


def source(filename):
    return fixtures.load_historical_json(filename)


def integrated_source(filename):
    return layout.load(CONTENT / "es" / UNIT / filename)


def first_retired_source(filename):
    """The exact published reading layout before the later reference retirement."""
    if filename not in FIRST_RETIRED_SHA256:
        raise ValueError("Unknown first-retirement fixture: " + filename)
    relative = fixtures.RELATIVE_UNIT + "/" + filename
    payload = subprocess.check_output(["git", "show", FIRST_RETIRED_COMMIT + ":" + relative], cwd=ROOT)
    if hashlib.sha256(payload).hexdigest() != FIRST_RETIRED_SHA256[filename]:
        raise RuntimeError("First-retirement fixture SHA-256 mismatch: " + filename)
    return layout.parse_json(payload.decode("utf-8"))


def retired(source, identities):
    # The fixture expresses exactly Work's U1 decision. Production retire()
    # never invents or removes dependencies on the author's behalf.
    removing = set(identities)
    activities = []
    for relation in source.get("activityAfter", []):
        if relation["activity"] not in removing:
            remaining = [item for item in relation["requires"] if item not in removing]
            if remaining:
                activities.append({"activity": relation["activity"], "requires": remaining})
    references = []
    for relation in source.get("referenceAfter", []):
        if relation["activity"] not in removing:
            remaining = [item for item in relation["blocks"] if item not in removing]
            if remaining:
                references.append({"activity": relation["activity"], "blocks": remaining})
    return layout.retire(source, list(identities), activity_after=activities,
                         reference_after=references, compiler_puzzles_required=True)


class RetiredStateLayoutTest(unittest.TestCase):
    def test_pinned_history_is_exact_independent_and_never_falls_back_to_live_assets(self):
        for filename, _ in PLANS:
            payload = fixtures.historical_bytes(filename)
            fixtures._verify_bytes(filename, payload)
            with self.subTest(filename=filename), self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                fixtures._verify_bytes(filename, payload + b"\n")
            historical = source(filename)
            historical["pages"][0]["blocks"][0]["text"] = "Changed only inside this test"
            self.assertNotEqual(historical, source(filename))
        with self.assertRaisesRegex(ValueError, "Unknown historical"):
            fixtures.historical_bytes("../lesson_not_a_fixture.json")
        fixtures.historical_bytes.cache_clear()
        try:
            with patch.object(fixtures.subprocess, "check_output", side_effect=OSError("Git unavailable")):
                with self.assertRaisesRegex(RuntimeError, "No network retrieval or fallback"):
                    source(PLANS[0][0])
        finally:
            fixtures.historical_bytes.cache_clear()

    def test_fixture_export_is_exact_repeatable_and_verifies_inputs_before_writing(self):
        qa = ROOT / "out" / "qa"
        qa.mkdir(parents=True, exist_ok=True)
        with TemporaryDirectory(prefix="retirement-source-", dir=qa) as temporary:
            destination = Path(temporary) / "fixtures"
            fixtures.export_fixtures(destination)
            self.assertEqual(set(fixtures.FIXTURE_SHA256), {path.name for path in destination.iterdir()})
            times = {}
            for filename in fixtures.FIXTURE_SHA256:
                target = destination / filename
                self.assertEqual(fixtures.historical_bytes(filename), target.read_bytes())
                times[filename] = target.stat().st_mtime_ns
            fixtures.export_fixtures(destination)
            self.assertEqual(times, {name: (destination / name).stat().st_mtime_ns for name in times})
            # Missing historical input is a hard failure, never a fixture made
            # from today's assets or a partially populated destination.
            missing = Path(temporary) / "must-not-exist"
            with patch.object(fixtures, "historical_bytes", side_effect=RuntimeError("Missing pinned input")):
                with self.assertRaisesRegex(RuntimeError, "Missing pinned input"):
                    fixtures.export_fixtures(missing)
            self.assertFalse(missing.exists())

    def test_current_assets_already_retire_the_originals_and_preserve_the_published_migration(self):
        useful_compilers = 0
        for filename, identities in PLANS:
            before, after = source(filename), integrated_source(filename)
            intermediate = first_retired_source(filename)
            original = deepcopy(after)
            # Published legacy snapshots remain untouched. Rehearse each
            # complete transition separately with today's authenticated capture,
            # and compare its entire presentation with the historical snapshot.
            def check_published_stage(previous, published):
                layout.validate(previous)
                layout.validate(published)
                old_history = previous["stateLayout"].get("retirementHistory", [])
                history = published["stateLayout"].get("retirementHistory", [])
                if len(history) == len(old_history):
                    layout.validate_transition(previous, published)
                    return
                if len(history) > len(old_history) + 1:
                    # The current published lesson may contain several later
                    # independent retirement releases. They are NOT one legal
                    # transition. validate(published) already authenticates
                    # every archival batch and reading snapshot; pin the exact
                    # previous edition at the first additional batch.
                    self.assertEqual(old_history, history[:len(old_history)])
                    first_new = history[len(old_history)]
                    self.assertEqual(previous["pages"], first_new["pages"])
                    self.assertEqual(previous["stateLayout"]["current"], first_new["current"])
                    self.assertEqual(previous.get("referenceAfter", []), first_new["referenceAfter"])
                    self.assertEqual(previous.get("activityAfter", []), first_new["activityAfter"])
                    old_ids = [entry["id"] for entry in previous["stateLayout"].get("retired", [])]
                    current_ids = [entry["id"] for entry in published["stateLayout"].get("retired", [])]
                    self.assertEqual(old_ids, current_ids[:len(old_ids)])
                    self.assertEqual(
                        [identity for snapshot in history[len(old_history):]
                         for identity in snapshot["retired"]],
                        current_ids[len(old_ids):])
                    # Later releases can also add source-format archives whose
                    # owners were active only after this old edition. Keep the
                    # compound jump rejected without prescribing which guard
                    # runs first; the isolated test below reaches the history
                    # guard with every existing canonical archive preserved.
                    with self.assertRaises(layout.LayoutError):
                        layout.validate_transition(previous, published)
                    return
                self.assertEqual(len(old_history) + 1, len(history))
                old_count = len(previous["stateLayout"].get("retired", []))
                removing = [item["id"] for item in published["stateLayout"]["retired"][old_count:]]
                historical = history[-1]
                if (historical["pages"] != previous["pages"] or
                        historical["current"] != previous["stateLayout"]["current"] or
                        published["stateLayout"]["additions"] != previous["stateLayout"]["additions"]):
                    # This historical batch followed a real intermediate
                    # publication that added pages or exercises. Replaying
                    # it directly against the much older fixture would
                    # manufacture a transition that was never published, and
                    # its modern referenceAfter links can name new owners.
                    # Authenticate the complete current snapshot with
                    # validate(), then require the old archive and its
                    # historical reading sequence to remain an exact prefix.
                    self.assertEqual(old_history, history[:len(old_history)])
                    previous_retired = previous["stateLayout"].get("retired", [])
                    self.assertEqual(previous_retired,
                                     published["stateLayout"]["retired"][:old_count])
                    self.assertEqual(removing, historical["retired"])
                    old_additions = previous["stateLayout"]["additions"]
                    later_additions = published["stateLayout"]["additions"]
                    self.assertEqual(old_additions,
                                     later_additions[:len(old_additions)])
                    self.assertGreater(len(later_additions), len(old_additions))
                    return
                rehearsal = layout.retire(previous, removing,
                    activity_after=published.get("activityAfter", []),
                    reference_after=published.get("referenceAfter", []),
                    compiler_puzzles_required=published.get("compilerPuzzlesRequired", False))
                captured = rehearsal["stateLayout"]["retirementHistory"][-1]
                historical = history[-1]
                self.assertLessEqual(set(historical), set(captured))
                self.assertLessEqual(set(captured) - set(historical), {"editorialPrefix", "textSemantics"})
                if "textSemantics" not in historical:
                    self.assertIsNone(captured.get("textSemantics"))
                self.assertEqual(historical, {key: captured[key] for key in historical})
                proposal = deepcopy(published)
                proposal["stateLayout"]["retirementHistory"][-1] = captured
                # The pinned order refinement introduced an optional first
                # hint; it did not replace an existing guide. Preserve that
                # authenticated transition together with its original order
                # contract, proof chain and exact retirement snapshot.
                original_blocks = layout._all_blocks(previous)
                revised_blocks = layout._all_blocks(proposal)
                legacy_guides = [
                    identity for identity, old in original_blocks.items()
                    if old.get("type") == "order_code"
                    and "reviewFeedback" not in old
                    and isinstance(revised_blocks.get(identity), dict)
                    and isinstance(revised_blocks[identity].get("reviewFeedback"), str)
                ]
                if legacy_guides:
                    refinements = {
                        item["id"]: item["block"]
                        for item in proposal["stateLayout"].get("orderRefinements", [])
                    }
                    for identity in legacy_guides:
                        self.assertIn(identity, refinements)
                        self.assertEqual(refinements[identity], original_blocks[identity])
                        self.assertTrue(any(
                            proof["id"] == identity
                            and "reviewPaths" not in proof
                            for proof in proposal["stateLayout"]["editorial"]))
                layout.validate_transition(previous, proposal)
                for identity in legacy_guides:
                    for fault in ("missing-refinement", "missing-proof", "invented-review-path"):
                        forged = deepcopy(proposal)
                        if fault == "missing-refinement":
                            forged["stateLayout"]["orderRefinements"] = [
                                item for item in forged["stateLayout"]["orderRefinements"]
                                if item["id"] != identity
                            ]
                        else:
                            journal = forged["stateLayout"]["editorial"]
                            added = [
                                index for index in range(
                                    len(previous["stateLayout"]["editorial"]), len(journal))
                                if journal[index]["id"] == identity
                            ]
                            self.assertTrue(added)
                            if fault == "missing-proof":
                                del journal[added[-1]]
                            else:
                                journal[added[-1]]["reviewPaths"] = ["/reviewFeedback"]
                        with self.subTest(filename=filename, identity=identity, fault=fault):
                            with self.assertRaises(layout.LayoutError):
                                layout.validate_transition(previous, forged)
            check_published_stage(before, intermediate)
            check_published_stage(intermediate, after)
            self.assertEqual(original, after)
            self.assertTrue(after["compilerPuzzlesRequired"])
            for field in ("origin", "family"):
                self.assertEqual(before["stateLayout"][field], after["stateLayout"][field])
            # Later Work releases may append new graded activities without
            # assigning the historical answers to those new storage owners.
            # A publication cannot shorten or rewrite the earlier additions.
            old_additions = before["stateLayout"]["additions"]
            current_additions = after["stateLayout"]["additions"]
            self.assertEqual(old_additions, current_additions[:len(old_additions)])
            self.assertEqual(len(current_additions),
                             len({entry["id"] for entry in current_additions}))
            published, current = layout._all_blocks(before), layout._all_blocks(after)
            self.assertLessEqual(published.keys(), current.keys())
            for identity in published:
                # Authenticate later source spacing before restoring only its
                # code bytes. Existing option and line-refinement archives
                # recover the original wording/groups after full validation,
                # retaining the entire answer and evaluation contract.
                projected = layout._historical_source_format_contract(after, identity, current[identity])
                projected = layout._historical_choice_contract(after, identity, projected)
                projected = layout._historical_order_contract(after, identity, projected)
                self.assertEqual(layout.state_payload(published[identity]), layout.state_payload(projected))
            references = REFERENCE_RETIREMENTS.get(filename, ())
            # The original compiler and reference retirements remain exact
            # prefixes. Work may subsequently retire unrelated obsolete
            # presentation blocks in additional authenticated releases.
            archive = after["stateLayout"]["retired"]
            legacy_ids = list(identities + references)
            self.assertEqual(legacy_ids, [row["id"] for row in archive[:len(legacy_ids)]])
            for record in archive[:len(legacy_ids)]:
                self.assertEqual(published[record["id"]], record["block"])
            history = after["stateLayout"]["retirementHistory"]
            self.assertGreaterEqual(len(history), 1 + bool(references))
            self.assertEqual(
                [identity for snapshot in history for identity in snapshot["retired"]],
                [entry["id"] for entry in archive])
            self.assertEqual(
                [identity for snapshot in history[1 + bool(references):]
                 for identity in snapshot["retired"]],
                [entry["id"] for entry in archive[len(legacy_ids):]])
            if len(history) > 1 + bool(references):
                # Genuine later releases are allowed, but their individual
                # identity batches cannot be shortened while retaining the
                # same complete retired-block archive and reading history.
                forged = deepcopy(after)
                extra_batch = forged["stateLayout"]["retirementHistory"][-1]["retired"]
                self.assertGreater(len(extra_batch), 1)
                extra_batch.pop()
                with self.assertRaises(layout.LayoutError):
                    layout.validate(forged)
            self.assertEqual(before["pages"], history[0]["pages"])
            self.assertEqual(before["stateLayout"]["current"], history[0]["current"])
            self.assertEqual(before.get("activityAfter", []), history[0]["activityAfter"])
            self.assertEqual(before.get("referenceAfter", []), history[0]["referenceAfter"])
            self.assertFalse(history[0]["compilerPuzzlesRequired"])
            self.assertEqual(intermediate["stateLayout"]["retirementHistory"], history[:1])
            if references:
                self.assertEqual(intermediate["pages"], history[1]["pages"])
                self.assertEqual(intermediate["stateLayout"]["current"], history[1]["current"])
                self.assertEqual(intermediate.get("activityAfter", []), history[1]["activityAfter"])
                self.assertEqual(intermediate.get("referenceAfter", []), history[1]["referenceAfter"])
                self.assertEqual(intermediate["compilerPuzzlesRequired"], history[1]["compilerPuzzlesRequired"])
                # This whole published jump can violate more than one gate;
                # the focused regression below isolates the reading-history
                # rejection without removing any later canonical archives.
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(before, after)
            self.assertTrue(set(identities + references).isdisjoint(layout._blocks(after)))
            with self.assertRaisesRegex(layout.LayoutError, "Only active identities"):
                retired(after, identities)
            useful_compilers += sum(block.get("type") == "cpp_compiler" for block in layout._blocks(after).values())
            members, languages = [after], ["es"]
            for language in before["stateLayout"]["family"]:
                if language != "es":
                    languages.append(language)
                    members.append(layout.load(CONTENT / language / UNIT / filename))
            layout.check_localized_family(members, languages)
        self.assertEqual(6, useful_compilers)
        for path in (CONTENT / "es" / UNIT).glob("lesson_*.json"):
            if path.name in {plan[0] for plan in PLANS}:
                continue
            current = layout.load(path)
            count = sum(block.get("type") == "cpp_compiler" for page in current["pages"] for block in page["blocks"])
            if count:
                self.assertTrue(current["compilerPuzzlesRequired"])
            useful_compilers += count
        self.assertEqual(9, useful_compilers)

    def test_skipped_retirement_history_is_rejected_with_current_format_archives_intact(self):
        for filename, _ in PLANS:
            with self.subTest(filename=filename):
                before = integrated_source(filename)
                original = deepcopy(before)
                layout.validate(before)
                canonical = before["stateLayout"].get("sourceFormatRevisions", [])
                self.assertTrue(canonical)
                # These two retirements exist only inside this test. Begin
                # with the complete live publication, choosing active owners
                # by identity rather than rebuilding an earlier edition.
                identities = [identity for identity, block in layout._blocks(before).items()
                              if block.get("type") == "paragraph"][:2]
                self.assertEqual(2, len(identities))
                previous = before
                for identity in identities:
                    following = retired(previous, [identity])
                    layout.validate_transition(previous, following)
                    old_meta, new_meta = previous["stateLayout"], following["stateLayout"]
                    self.assertEqual(canonical, new_meta["sourceFormatRevisions"])
                    self.assertEqual(layout._all_blocks(previous), layout._all_blocks(following))
                    for field in ("retired", "retirementHistory"):
                        prefix = old_meta.get(field, [])
                        self.assertEqual(prefix, new_meta[field][:len(prefix)])
                        self.assertEqual(len(prefix) + 1, len(new_meta[field]))
                    # This equality includes every editorial prefix, original
                    # family/storage declaration and canonical revision map.
                    moving = {"current", "retired", "retirementHistory"}
                    self.assertEqual(
                        {key: value for key, value in old_meta.items() if key not in moving},
                        {key: value for key, value in new_meta.items() if key not in moving})
                    captured = new_meta["retirementHistory"][-1]
                    self.assertEqual([identity], captured["retired"])
                    self.assertEqual(previous["pages"], captured["pages"])
                    self.assertEqual(old_meta["current"], captured["current"])
                    previous = following
                self.assertEqual(original, before)
                # Each intervening publication is valid. Presenting both as
                # one transition must reach this particular history guard,
                # without an earlier source-owner or archive failure.
                with self.assertRaisesRegex(
                        layout.LayoutError,
                        "^One transition can archive only its actual previous reading layout$"):
                    layout.validate_transition(before, previous)

    def test_three_real_retirements_preserve_every_contract_and_historical_slot(self):
        useful_compilers = 0
        for filename, identities in PLANS:
            before = source(filename)
            original = deepcopy(before)
            after = retired(before, identities)
            layout.validate_transition(before, after)
            self.assertEqual(original, before)
            for field in ("origin", "family", "additions", "editorial"):
                self.assertEqual(before["stateLayout"][field], after["stateLayout"][field])
            self.assertTrue(after["compilerPuzzlesRequired"])
            self.assertEqual(layout._all_blocks(before), layout._all_blocks(after))
            self.assertTrue(set(identities).isdisjoint(layout._blocks(after)))
            old_mapping, new_mapping = layout.validate(before), layout.validate(after)
            old_slots = {identity: old_mapping[p, b]
                         for p, row in enumerate(before["stateLayout"]["current"])
                         for b, identity in enumerate(row["blocks"])}
            for p, row in enumerate(after["stateLayout"]["current"]):
                for b, identity in enumerate(row["blocks"]):
                    self.assertEqual(old_slots[identity], new_mapping[p, b])
            useful_compilers += sum(block.get("type") == "cpp_compiler"
                                    for block in layout._blocks(after).values())
            # The regular curriculum guard sees the untouched retired block at
            # its real previous coordinate, so retirement is no edit bypass.
            aligned = layout.align_for_previous_validation(before, after)
            self.assertEqual(before["pages"], aligned["pages"])
            languages = ["es"]
            members = [after]
            for language in before["stateLayout"]["family"]:
                if language != "es":
                    languages.append(language)
                    members.append(layout.load(CONTENT / language / UNIT / filename))
            layout.check_localized_family(members, languages)
        # These three lessons retain six useful editors. The final two are in
        # U1L5, which is intentionally outside this retirement plan.
        self.assertEqual(6, useful_compilers)
        other_compilers = sum(block.get("type") == "cpp_compiler"
                              for path in (CONTENT / "es" / UNIT).glob("lesson_*.json")
                              if path.name not in {plan[0] for plan in PLANS}
                              for page in layout.load(path)["pages"] for block in page["blocks"])
        self.assertEqual(9, useful_compilers + other_compilers)

    def test_archives_are_append_only_and_cannot_be_rewritten_or_resurrected(self):
        filename, identities = PLANS[0]
        before = source(filename)
        after = retired(before, identities)
        for key, value in (("page", "page_0000"), ("next", ""), ("block", {"type": "paragraph", "text": "replacement"})):
            changed = deepcopy(after)
            changed["stateLayout"]["retired"][0][key] = value
            with self.subTest(key=key), self.assertRaises(layout.LayoutError):
                layout.validate_transition(before, changed)
        for changed in (before, dict(after, stateLayout={**after["stateLayout"], "retired": []})):
            with self.assertRaises(layout.LayoutError):
                layout.validate_transition(after, changed)
        changed = deepcopy(after)
        changed["stateLayout"]["retired"][1]["block"]["initialCode"] += "// changed"
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed)
        changed = deepcopy(after)
        changed["stateLayout"]["retired"][0]["block"]["text"] += " changed"
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, changed)

    def test_stderr_objectives_preserve_active_addresses_and_immutable_retirement_snapshots(self):
        filename, identities = PLANS[0]
        before = source(filename)
        required = deepcopy(before)
        editor = layout._blocks(required)[identities[1]]
        previous_editor = deepcopy(editor)
        editor["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
        required["stateLayout"]["editorial"].append({"id": identities[1],
            "beforeSha256": layout.fingerprint(previous_editor), "afterSha256": layout.fingerprint(editor)})
        layout.validate_transition(before, required)
        after = retired(required, identities)
        layout.validate_transition(required, after)
        archived = after["stateLayout"]["retired"][1]["block"]
        self.assertEqual("nonBlank", archived["outputCheck"]["cases"][0]["standardError"])
        history = deepcopy(after["stateLayout"]["retirementHistory"])
        # A later active objective can be extended while old reading payloads
        # retain precisely the requirements published when they were archived.
        revised = deepcopy(after)
        active_id = "block_0004_0002"
        active = layout._blocks(revised)[active_id]
        old_active = deepcopy(active)
        active["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
        revised["stateLayout"]["editorial"].append({"id": active_id,
            "beforeSha256": layout.fingerprint(old_active), "afterSha256": layout.fingerprint(active)})
        layout.validate_transition(after, revised)
        self.assertEqual(layout.validate(after), layout.validate(revised))
        self.assertEqual(history, revised["stateLayout"]["retirementHistory"])
        for part in ("retired", "retirementHistory"):
            changed = deepcopy(after)
            if part == "retired":
                case = changed["stateLayout"][part][1]["block"]["outputCheck"]["cases"][0]
            else:
                old_pages = changed["stateLayout"][part][0]["pages"]
                old_editor = next(block for page in old_pages for block in page["blocks"]
                                  if block.get("type") == "cpp_compiler" and
                                  block.get("outputCheck", {}).get("cases", [{}])[0].get("standardError"))
                case = old_editor["outputCheck"]["cases"][0]
            del case["standardError"]
            with self.subTest(part=part), self.assertRaises(layout.LayoutError):
                layout.validate_transition(after, changed)

    def test_unknown_duplicate_foreign_and_cyclic_retirements_are_rejected(self):
        filename, identities = PLANS[0]
        before = source(filename)
        for plan in ([], [identities[0], identities[0]], ["not_an_activity"], ["block_0004_0003"], [{}], [None]):
            with self.subTest(plan=plan), self.assertRaises(layout.LayoutError):
                layout.retire(before, plan)
        after = retired(before, identities)
        for change in ("active", "cycle", "foreign", "locale"):
            changed = deepcopy(after)
            if change == "active":
                changed["stateLayout"]["retired"][0]["id"] = changed["stateLayout"]["current"][0]["blocks"][0]
            elif change == "cycle":
                first, second = changed["stateLayout"]["retired"]
                first["next"], second["next"] = second["id"], first["id"]
            elif change == "foreign":
                changed["stateLayout"]["retired"][0]["next"] = "unrelated"
            else:
                changed["stateLayout"]["locale"] = "en"
            with self.subTest(change=change), self.assertRaises(layout.LayoutError):
                layout.validate(changed)

    def test_relationship_updates_are_explicit_and_useful_dependencies_survive(self):
        before = source(PLANS[0][0])
        with self.assertRaisesRegex(layout.LayoutError, "does not exist"):
            layout.retire(before, list(PLANS[0][1]))
        after = retired(before, PLANS[0][1])
        self.assertTrue(all(set(link["requires"]).isdisjoint(PLANS[0][1])
                            for link in after.get("activityAfter", [])))
        before = source(PLANS[1][0])
        after = retired(before, PLANS[1][1])
        for relation in before["activityAfter"]:
            if relation["activity"] == "block_0006_0006":
                self.assertIn(relation, after["activityAfter"])

    def test_mandatory_flow_flag_is_typed_and_cannot_be_disabled_after_publication(self):
        before = source(PLANS[0][0])
        enabled = dict(deepcopy(before), compilerPuzzlesRequired=True)
        layout.validate_transition(before, enabled)
        for value in (None, "true", 1, [], {}):
            with self.subTest(value=value), self.assertRaises(layout.LayoutError):
                layout.validate(dict(before, compilerPuzzlesRequired=value))
        for current in (before, dict(enabled, compilerPuzzlesRequired=False)):
            with self.assertRaises(layout.LayoutError):
                layout.validate_transition(enabled, current)

    def test_reading_archive_keeps_exact_old_payloads_layout_and_visibility_during_active_revision(self):
        before = source(PLANS[0][0])
        after = retired(before, PLANS[0][1])
        snapshot = after["stateLayout"]["retirementHistory"][0]
        self.assertEqual(before["pages"], snapshot["pages"])
        self.assertEqual(before["stateLayout"]["current"], snapshot["current"])
        self.assertEqual(before.get("activityAfter", []), snapshot["activityAfter"])
        self.assertEqual(before.get("referenceAfter", []), snapshot["referenceAfter"])
        self.assertFalse(snapshot["compilerPuzzlesRequired"])
        identity = "block_0004_0003"
        active = layout._blocks(after)[identity]
        old = deepcopy(active)
        active["question"] += " Consigna activa revisada sin ejecutar el original."
        after["stateLayout"]["editorial"].append({"id": identity,
            "beforeSha256": layout.fingerprint(old), "afterSha256": layout.fingerprint(active)})
        layout.validate_transition(before, after)
        old_blocks = {stable: block for page, record in zip(snapshot["pages"], snapshot["current"])
                      for stable, block in zip(record["blocks"], page["blocks"])}
        self.assertEqual(old, old_blocks[identity])
        self.assertNotEqual(old_blocks[identity]["question"], active["question"])
        # A second retirement records the actual intervening presentation,
        # without reintroducing identities already absent from that version.
        another = next(stable for stable, block in layout._blocks(after).items()
                       if block.get("type") == "paragraph")
        twice = layout.retire(after, [another])
        self.assertEqual(snapshot, twice["stateLayout"]["retirementHistory"][0])
        self.assertEqual(after["pages"], twice["stateLayout"]["retirementHistory"][1]["pages"])
        layout.validate_transition(after, twice)

    def test_reading_archive_cannot_invent_source_text_graph_order_or_flow(self):
        before = source(PLANS[0][0])
        after = retired(before, PLANS[0][1])
        for change in ("text", "graph", "flow", "order", "removed", "unknown"):
            changed = deepcopy(after)
            history = changed["stateLayout"]["retirementHistory"]
            if change == "text":
                history[0]["pages"][0]["blocks"][0]["text"] += " historia inventada"
            elif change == "graph":
                history[0]["activityAfter"] = []
            elif change == "flow":
                history[0]["compilerPuzzlesRequired"] = True
            elif change == "order":
                history[0]["pages"][0]["blocks"].reverse()
                history[0]["current"][0]["blocks"].reverse()
            elif change == "removed":
                changed["stateLayout"].pop("retirementHistory")
                layout.validate(changed)  # Older archives remain loadable.
            else:
                history[0]["activityAfter"][0]["requires"] = ["unrelated_activity"]
            with self.subTest(change=change), self.assertRaises(layout.LayoutError):
                layout.validate_transition(before, changed)
            with self.subTest(update=change), self.assertRaises(layout.LayoutError):
                layout.validate_transition(after, changed)

    def test_later_additions_preserve_old_reading_history_across_another_retirement(self):
        before = source(PLANS[0][0])
        after = retired(before, PLANS[0][1])
        snapshot = deepcopy(after["stateLayout"]["retirementHistory"])
        plan = deepcopy(after["stateLayout"]["current"])
        identity = "qa_addition_after_retirement"
        plan[0]["blocks"].append(identity)
        reserved = max([len(row["origin"]) for row in after["stateLayout"]["family"].values()] +
                       [row["storage"]["page"] + 1 for row in after["stateLayout"]["additions"]])
        added = layout.reorder(after, plan, [{"id": identity, "storage": {"page": reserved, "block": 0},
            "progressPage": plan[0]["id"], "block": {"type": "paragraph", "text": "Alta compatible posterior."}}])
        layout.validate_transition(after, added)
        self.assertEqual(snapshot, added["stateLayout"]["retirementHistory"])
        self.assertNotIn(identity, {item for page in snapshot[0]["current"] for item in page["blocks"]})
        self.assertIn(identity, layout._blocks(added))
        self.assertEqual((reserved, 0), layout.validate(added)[0, len(plan[0]["blocks"]) - 1])
        again = layout.retire(added, [identity])
        layout.validate_transition(added, again)
        self.assertEqual(snapshot[0], again["stateLayout"]["retirementHistory"][0])
        self.assertEqual(added["pages"], again["stateLayout"]["retirementHistory"][1]["pages"])
        # Original blocks cannot be mistaken for a later addition.
        damaged = deepcopy(added)
        old = damaged["stateLayout"]["retirementHistory"][0]
        old["current"][0]["blocks"].pop(0)
        old["pages"][0]["blocks"].pop(0)
        with self.assertRaises(layout.LayoutError):
            layout.validate(damaged)


class RetirementPublicationChainTest(unittest.TestCase):
    """Uncommitted intermediate presentations need every ordinary edge checked."""

    @staticmethod
    def stages():
        source = {
            "id": "cpp_fundamentals__qa__retirement_publication_chain",
            "title": "Cadena de publicaciones QA",
            "pages": [
                {"blocks": [
                    {"type": "paragraph", "text": "Presentación original conservada."},
                    {"type": "fill_code", "question": "Respuesta original",
                     "code": "___", "answers": ["ok"]},
                ]},
                {"blocks": [{"type": "paragraph", "text": "Otra página intacta."}]},
            ],
        }
        before = layout.prepare_v2(source, "es", {"es": source})
        image = "qa_image_between_publications"
        plan = deepcopy(before["stateLayout"]["current"])
        plan[0]["blocks"].insert(1, image)
        intermediate = layout.reorder(before, plan, [{
            "id": image, "storage": {"page": 2, "block": 0},
            "progressPage": plan[0]["id"], "block": {
                "type": "image", "imageId": image, "src": "images/qa/" + image + ".png",
                "alt": "Imagen usada antes del retiro.", "caption": "Imagen histórica.",
            },
        }])
        retired_image = layout.retire(intermediate, [image])
        plan = deepcopy(retired_image["stateLayout"]["current"])
        plan[0]["blocks"].insert(1, "qa_replacement_activity")
        final = layout.reorder(retired_image, plan, [{
            "id": "qa_replacement_activity", "storage": {"page": 2, "block": 1},
            "progressPage": plan[0]["id"], "block": {
                "type": "fill_code", "question": "Actividad que reemplaza la imagen",
                "code": "replacement ___", "answers": ["nuevo"],
            },
        }])
        return before, intermediate, final

    def test_every_link_uses_the_unchanged_single_transition_guard(self):
        before, intermediate, final = self.stages()
        originals = deepcopy((before, intermediate, final))
        with self.assertRaisesRegex(
                layout.LayoutError,
                "Retirement reading snapshot does not match the complete previous published presentation"):
            layout.validate_transition(before, final)
        accepted = []
        strict = layout.validate_transition

        def verify(left, right):
            strict(left, right)
            accepted.append((deepcopy(left), deepcopy(right)))

        with patch.object(layout, "validate_transition", side_effect=verify):
            layout.validate_publication_transition(before, final)
        self.assertEqual(len(accepted), 2)
        self.assertEqual(accepted[0][0], before)
        rebuilt = accepted[0][1]
        self.assertEqual(rebuilt["pages"], intermediate["pages"])
        self.assertEqual(rebuilt["stateLayout"], intermediate["stateLayout"])
        self.assertEqual(accepted[1], (rebuilt, final))
        self.assertNotIn("qa_replacement_activity", layout._blocks(rebuilt))
        self.assertEqual(originals, (before, intermediate, final))

    def test_projection_for_existing_curricular_validation_uses_the_verified_chain(self):
        before, _, final = self.stages()
        aligned = layout.align_for_previous_validation(before, final)
        self.assertEqual(aligned["pages"], before["pages"])
        # This is the existing positional contract projection, not a newly
        # published document: its metadata deliberately retains final lineage.
        self.assertEqual(aligned["stateLayout"], final["stateLayout"])

    def test_self_consistent_snapshot_text_does_not_authorize_an_unproved_past(self):
        before, _, final = self.stages()
        altered = deepcopy(final)
        altered["stateLayout"]["retirementHistory"][0]["pages"][0]["blocks"][0]["text"] += " Inventado."
        # This reading-only text does not alter a response contract. A final
        # document's internal consistency therefore cannot prove publication.
        layout.validate(altered)
        with self.assertRaises(layout.LayoutError):
            layout.validate_publication_transition(before, altered)

    def test_new_owner_cannot_reuse_a_retired_or_original_storage_address(self):
        before, _, final = self.stages()
        for storage in ({"page": 2, "block": 0}, {"page": 0, "block": 1}):
            altered = deepcopy(final)
            altered["stateLayout"]["additions"][-1]["storage"] = storage
            with self.subTest(storage=storage), self.assertRaises(layout.LayoutError):
                layout.validate_publication_transition(before, altered)

    def test_chain_cannot_change_identity_answers_provenance_or_retired_payload(self):
        before, _, final = self.stages()
        for mutation in ("lesson", "family", "answer", "editorial-prefix", "retired-payload"):
            altered = deepcopy(final)
            meta = altered["stateLayout"]
            if mutation == "lesson":
                altered["id"] += "_foreign"
            elif mutation == "family":
                meta["family"]["es"]["sha256"] = "0" * 64
            elif mutation == "answer":
                layout._blocks(altered)["block_0000_0001"]["answers"] = ["otra"]
            elif mutation == "editorial-prefix":
                meta["retirementHistory"][0]["editorialPrefix"]["sha256"] = "0" * 64
            else:
                meta["retired"][0]["block"]["caption"] += " Otra imagen."
            with self.subTest(mutation=mutation), self.assertRaises(layout.LayoutError):
                layout.validate_publication_transition(before, altered)

    def test_missing_editorial_prefix_is_not_reconstructed_as_evidence(self):
        before, _, final = self.stages()
        legacy = deepcopy(final)
        del legacy["stateLayout"]["retirementHistory"][0]["editorialPrefix"]
        layout.validate(legacy)  # Read compatibility does not grant a publication proof.
        with self.assertRaisesRegex(layout.LayoutError, "authenticated retirement editorial prefix"):
            layout.validate_publication_transition(before, legacy)

    def test_multiple_retirements_validate_each_actual_intermediate_presentation(self):
        before, _, first = self.stages()
        plan = deepcopy(first["stateLayout"]["current"])
        plan[1]["blocks"].append("qa_later_note")
        second = layout.reorder(first, plan, [{
            "id": "qa_later_note", "storage": {"page": 2, "block": 2},
            "progressPage": plan[1]["id"],
            "block": {"type": "note", "text": "Presentación intermedia posterior."},
        }])
        final = layout.retire(second, ["qa_later_note"])
        with self.assertRaisesRegex(layout.LayoutError, "One transition can archive only"):
            layout.validate_transition(before, final)
        layout.validate_publication_transition(before, final)
        original = deepcopy(final)
        # An already-published history prefix is immutable even when its
        # altered prose could still pass the isolated document validator.
        final["stateLayout"]["retirementHistory"][0]["pages"][0]["blocks"][0]["text"] += " Alterado."
        layout.validate(final)
        with self.assertRaisesRegex(layout.LayoutError, "snapshots are append-only"):
            layout.validate_publication_transition(first, final)
        self.assertNotEqual(original, final)

    def test_unrecorded_intermediate_addition_cannot_be_skipped(self):
        before, _, final = self.stages()
        altered = deepcopy(final)
        # Both slots remain unique, but their declaration order would claim
        # the final exercise already existed before the archived image.
        altered["stateLayout"]["additions"].reverse()
        layout.validate(altered)
        with self.assertRaisesRegex(layout.LayoutError, "cannot skip an earlier addition"):
            layout.validate_publication_transition(before, altered)

    def test_real_work_scope_retirement_uses_the_pinned_previous_and_final_publications(self):
        relative = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                    "edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json")
        sources = (
            ("9aebd308b44dc930b8dedc765f96bcd170f502d8",
             "805698c3785afffb892521a1cd66d6994fd1ae43a15e81661d6735b224448f99"),
            ("8562de531765842e21932e1b78bb15f54ddebc20",
             "5fc581a2a302af14c2ba2c03790e16891ab4a98c0e2cc3ff6974cff0a9642f9a"),
        )
        documents = []
        for commit, expected in sources:
            payload = subprocess.check_output(["git", "show", commit + ":" + relative], cwd=ROOT)
            self.assertEqual(hashlib.sha256(payload).hexdigest(), expected)
            documents.append(layout.parse_json(payload.decode("utf-8")))
        before, final = documents
        original = deepcopy(documents)
        image = "u3l4_alcance_esquema_de_bloques"
        replacement = "u3l4_alcance_codigo_en_recuadros"
        self.assertNotIn(image, layout._all_blocks(before))
        self.assertEqual(final["stateLayout"]["retirementHistory"][0]["retired"], [image])
        with self.assertRaisesRegex(layout.LayoutError, "complete previous published presentation"):
            layout.validate_transition(before, final)
        layout.validate_publication_transition(before, final)
        aligned = layout.align_for_previous_validation(before, final)
        self.assertEqual([len(page["blocks"]) for page in aligned["pages"]],
                         [len(page["blocks"]) for page in before["pages"]])
        self.assertEqual(aligned["stateLayout"], final["stateLayout"])
        owner_slots = []
        for document in (before, final):
            mapping = layout.validate(document)
            owner_slots.append({identity: mapping[(p, b)]
                                for p, row in enumerate(document["stateLayout"]["current"])
                                for b, identity in enumerate(row["blocks"])})
        for identity, storage in owner_slots[0].items():
            self.assertEqual(owner_slots[1][identity], storage)
        declarations = {entry["id"]: entry for entry in final["stateLayout"]["additions"]}
        self.assertEqual(declarations[image]["storage"], {"page": 7, "block": 29})
        self.assertEqual(declarations[replacement]["storage"], {"page": 7, "block": 30})
        for mutation in ("snapshot", "occupied-storage"):
            altered = deepcopy(final)
            if mutation == "snapshot":
                snap = altered["stateLayout"]["retirementHistory"][0]
                location = next((p, b) for p, row in enumerate(snap["current"])
                                for b, identity in enumerate(row["blocks"]) if identity == image)
                snap["pages"][location[0]]["blocks"][location[1]]["caption"] += " Alterada."
            else:
                next(entry for entry in altered["stateLayout"]["additions"]
                     if entry["id"] == replacement)["storage"] = {"page": 7, "block": 29}
            with self.subTest(mutation=mutation), self.assertRaises(layout.LayoutError):
                layout.validate_publication_transition(before, altered)
        self.assertEqual(original, documents)


if __name__ == "__main__":
    unittest.main()


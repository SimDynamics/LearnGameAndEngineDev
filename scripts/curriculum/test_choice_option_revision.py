"""Seven exact option revisions preserve immutable contracts and editorial history."""
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "16c497c7751f49d1f3f641cf3937b0275aa62e0f"
PROPOSAL_BASELINE = "c570ceb3643592d16f46c25969cf82202313efe0"
PROPOSAL_SOURCE_SHA256 = "83d473a2b10bd8aad9c548e5688df66dc87ea0096b98eb4ed064cdcba961135f"
PROPOSAL_LESSON = layout._CHOICE_PREFIX + "diagnosticar_y_recuperar_hasta_fin_de_operacion"
PREFIX = "app/src/main/assets/content/es/programming/cpp_fundamentals/"


def asset_path(lesson):
    return "texto_interpretacion_y_flujos/lesson_" + lesson.split("__")[-1] + ".json"


@lru_cache(maxsize=None)
def published_bytes(lesson, locale="es"):
    baseline = PROPOSAL_BASELINE if lesson == PROPOSAL_LESSON else BASELINE
    return subprocess.check_output(["git", "show", baseline + ":" + PREFIX.replace("/es/", "/" + locale + "/") + asset_path(lesson)], cwd=contract.ROOT)


def published(lesson, locale="es"):
    return json.loads(published_bytes(lesson, locale))


def block(document, identity):
    return layout._blocks(document)[identity]


def slot(identity):
    _, page, position = identity.split("_")
    return int(page), int(position)


def revised(lesson, identity):
    return layout.revise_choice_option(published(lesson), identity)


def replace_terminal_proof(document, identity):
    proofs = [proof for proof in document["stateLayout"]["editorial"] if proof["id"] == identity]
    proofs[-1]["afterSha256"] = layout.fingerprint(block(document, identity))


def historical_block(document, identity):
    snapshot = document["stateLayout"]["retirementHistory"][0]
    return next(candidate for page, record in zip(snapshot["pages"], snapshot["current"])
                for candidate, owner in zip(page["blocks"], record["blocks"]) if owner == identity)


class ChoiceOptionRevisionTest(unittest.TestCase):
    def test_u11l14_proposals_match_the_exact_requested_fields_and_keep_all_other_bytes(self):
        pins = {
            "block_0007_0001": (1, "5794fa047a8ca524287843d5781a4df11705b7582092345247bd23a4f7538820",
                               "075fc288e13b1ea1a971f0da9b501632b51b0b2301e8d055d2967d16552eac0b"),
            "block_0003_0004": (0, "6bf57ddb3ae17371c3fa19164f5f206f7828b8e441451d62cf4fffea7abc0438",
                               "c96666829c6ae46967980c801c3c5ac54aafa363cb4381ad9b7033f929ff00b1"),
        }
        for identity, (index, before_sha, after_sha) in pins.items():
            with self.subTest(identity=identity):
                before, current = published(PROPOSAL_LESSON), revised(PROPOSAL_LESSON, identity)
                old, new = block(before, identity), block(current, identity)
                self.assertEqual(index, old["correctIndex"])
                self.assertEqual(before_sha, hashlib.sha256(old["options"][index].encode()).hexdigest())
                self.assertEqual(after_sha, hashlib.sha256(new["options"][index].encode()).hexdigest())
                expected = deepcopy(new); expected["options"][index] = old["options"][index]
                if identity == "block_0003_0004":
                    self.assertEqual("b2fc2397105a6bce67a5106761ebbd7ac65cd53ed12816e6e5378f87e5cc8dae",
                                     hashlib.sha256(old["explanation"].encode()).hexdigest())
                    self.assertEqual("4778999c0c13e7b94dc8ab9fb30c142c4274489890e9242c86c9d1570dada68c",
                                     hashlib.sha256(new["explanation"].encode()).hexdigest())
                    expected["explanation"] = old["explanation"]
                self.assertEqual(old, expected)
                self.assertEqual(old, block(layout.align_for_previous_validation(before, current), identity))
                self.assertEqual(layout.original_projection(before)["pages"], layout.original_projection(current)["pages"])

    def test_token_option_and_explanation_cannot_be_split_even_with_a_later_editorial_edge(self):
        identity = "block_0003_0004"; before = published(PROPOSAL_LESSON)
        good = revised(PROPOSAL_LESSON, identity); old = block(before, identity); new = block(good, identity)
        for explanation in (old["explanation"], "Una explicación sin revisar."):
            with self.subTest(explanation=explanation):
                bad = deepcopy(good); target = block(bad, identity); target["explanation"] = explanation
                bad["stateLayout"]["editorial"].append({"id": identity,
                    "beforeSha256": layout.fingerprint(new), "afterSha256": layout.fingerprint(target)})
                with self.assertRaisesRegex(layout.LayoutError, "same exact version"): layout.validate(bad)
                with self.assertRaises(layout.LayoutError): layout.align_for_previous_validation(before, bad)
                bad = layout.retire(good, ["block_0000_0001"])
                historical_block(bad, identity)["explanation"] = explanation
                with self.assertRaisesRegex(layout.LayoutError, "same exact version"): layout.validate(bad)
        # The converse half-change is equally invalid, even though explanation
        # text normally does not participate in the response contract digest.
        bad = deepcopy(before); block(bad, identity)["explanation"] = new["explanation"]
        bad["stateLayout"]["editorial"].append({"id": identity,
            "beforeSha256": layout.fingerprint(old), "afterSha256": layout.fingerprint(block(bad, identity))})
        with self.assertRaisesRegex(layout.LayoutError, "same exact version"): layout.validate(bad)

    def test_u11l14_revisions_append_independently_and_coexist_with_the_closed_compiler_proposal(self):
        before = published(PROPOSAL_LESSON); ids = ("block_0003_0004", "block_0007_0001")
        first = layout.revise_choice_option(before, ids[0]); both = layout.revise_choice_option(first, ids[1])
        self.assertEqual(first["stateLayout"]["choiceRevisions"], both["stateLayout"]["choiceRevisions"][:1])
        self.assertEqual(first["stateLayout"]["editorial"], both["stateLayout"]["editorial"][:-1])
        alternate = layout.revise_choice_option(layout.revise_choice_option(before, ids[1]), ids[0])
        self.assertEqual(both["pages"], alternate["pages"])
        layout.validate_transition(before, both); layout.validate_transition(before, alternate)
        expanded = layout.revise_compiler_cases(both, "block_0004_0004")
        self.assertEqual(both["stateLayout"]["choiceRevisions"], expanded["stateLayout"]["choiceRevisions"])
        self.assertEqual(layout.original_projection(before)["pages"], layout.original_projection(expanded)["pages"])

    def test_u11l14_cli_builds_two_real_proposals_without_overwriting_the_active_asset(self):
        source = contract.ROOT / (PREFIX + asset_path(PROPOSAL_LESSON)); original_bytes = source.read_bytes()
        current = json.loads(original_bytes); layout.validate(current)
        for identity in ("block_0003_0004", "block_0007_0001"):
            with self.subTest(identity=identity), tempfile.TemporaryDirectory() as temporary:
                spec = layout.CHOICE_OPTION_REVISIONS[PROPOSAL_LESSON, identity]
                archives = [item for item in current["stateLayout"].get("choiceRevisions", []) if item["id"] == identity]
                expected = layout.revise_choice_option(current, identity)
                if archives:
                    # A published revision is already adopted. Preserve every
                    # later choice/compiler/editorial edge in the active asset.
                    self.assertEqual(1, len(archives))
                    self.assertEqual(spec["full_before"], layout.fingerprint(archives[0]["block"]))
                    self.assertIn({"id": identity, "beforeSha256": spec["full_before"], "afterSha256": spec["full_after"]},
                                  current["stateLayout"]["editorial"])
                    self.assertEqual(current, expected)
                else:
                    self.assertEqual(spec["full_before"], layout.fingerprint(block(current, identity)))
                    self.assertNotEqual(layout.fingerprint(current), layout.fingerprint(expected))
                output = Path(temporary) / "proposal.json"
                command = [sys.executable, "-B", str(Path(layout.__file__)), "revise-choice-option", str(source),
                           "--locale", "es", "--base-ref", PROPOSAL_BASELINE, "--identity", identity, "--output", str(output)]
                result = subprocess.run(command, cwd=contract.ROOT, capture_output=True, text=True, encoding="utf-8")
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                proposal = output.read_bytes(); self.assertEqual(expected, json.loads(proposal))
                self.assertNotEqual(0, subprocess.run(command, cwd=contract.ROOT, capture_output=True).returncode)
                self.assertEqual(proposal, output.read_bytes())
                self.assertNotEqual(0, subprocess.run(command[:-1] + [str(source)], cwd=contract.ROOT, capture_output=True).returncode)
                self.assertEqual(original_bytes, source.read_bytes())

    def test_u11l14_cli_really_revises_authenticated_historical_snapshots(self):
        original_bytes = published_bytes(PROPOSAL_LESSON)
        # This independent byte pin authenticates the complete c570ceb document,
        # not a reconstruction from the integrated live asset or the allowlist.
        self.assertEqual(PROPOSAL_SOURCE_SHA256, hashlib.sha256(original_bytes).hexdigest())
        original = json.loads(original_bytes); layout.validate(original)
        for identity in ("block_0003_0004", "block_0007_0001"):
            with self.subTest(identity=identity), tempfile.TemporaryDirectory() as temporary:
                spec = layout.CHOICE_OPTION_REVISIONS[PROPOSAL_LESSON, identity]
                self.assertEqual(spec["full_before"], layout.fingerprint(block(original, identity)))
                expected = revised(PROPOSAL_LESSON, identity)
                self.assertNotEqual(layout.fingerprint(original), layout.fingerprint(expected))
                self.assertEqual(spec["full_after"], layout.fingerprint(block(expected, identity)))
                self.assertEqual([{"id": identity, "block": block(original, identity)}], expected["stateLayout"]["choiceRevisions"])
                self.assertEqual(original["stateLayout"]["editorial"], expected["stateLayout"]["editorial"][:-1])
                layout.validate_transition(original, expected)
                source = Path(temporary) / "historical-source.json"
                previous = Path(temporary) / "published-reference.json"
                output = Path(temporary) / "proposal.json"
                source.write_bytes(original_bytes); previous.write_bytes(original_bytes)
                command = [sys.executable, "-B", str(Path(layout.__file__)), "revise-choice-option", str(source),
                           "--previous", str(previous), "--identity", identity, "--output", str(output)]
                result = subprocess.run(command, cwd=contract.ROOT, capture_output=True, text=True, encoding="utf-8")
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                proposal = output.read_bytes(); self.assertEqual(expected, json.loads(proposal))
                self.assertNotEqual(0, subprocess.run(command, cwd=contract.ROOT, capture_output=True).returncode)
                self.assertEqual(proposal, output.read_bytes())
                for protected in (source, previous):
                    overwrite = subprocess.run(command[:-1] + [str(protected)], cwd=contract.ROOT, capture_output=True)
                    self.assertNotEqual(0, overwrite.returncode)
                    self.assertEqual(original_bytes, protected.read_bytes())
                self.assertEqual(original_bytes, source.read_bytes()); self.assertEqual(original_bytes, previous.read_bytes())
                self.assertEqual(proposal, output.read_bytes())

    def test_exact_seven_revisions_keep_all_other_fields_and_historical_addresses(self):
        self.assertEqual(7, len(layout.CHOICE_OPTION_REVISIONS))
        for (lesson, identity), spec in layout.CHOICE_OPTION_REVISIONS.items():
            with self.subTest(lesson=lesson, identity=identity):
                before = published(lesson)
                untouched = deepcopy(before)
                current = layout.revise_choice_option(before, identity)
                old, new = block(before, identity), block(current, identity)
                self.assertEqual(spec["full_before"], layout.fingerprint(old))
                self.assertEqual(spec["full_after"], layout.fingerprint(new))
                self.assertEqual(spec["protected_before"], layout.state_fingerprint(old))
                self.assertEqual(spec["protected_after"], layout.fingerprint(layout._choice_payload(new)))
                self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
                self.assertEqual(spec["before"], old["options"][spec["option"]])
                self.assertEqual(spec["after"], new["options"][spec["option"]])
                expected = deepcopy(old)
                expected["options"][spec["option"]] = spec["after"]
                if spec["feedback"] is not None:
                    expected["reviewFeedback"][spec["option"]] = spec["feedback"]
                if "paired_explanation" in spec:
                    expected["explanation"] = spec["paired_explanation"]["after"]
                self.assertEqual(expected, new)
                self.assertEqual(before, untouched)
                self.assertEqual(current, layout.revise_choice_option(current, identity))
                self.assertEqual([{"id": identity, "block": old}], current["stateLayout"]["choiceRevisions"])
                for field in ("origin", "current", "family", "additions", "retired", "retirementHistory", "orderRefinements"):
                    self.assertEqual(before["stateLayout"].get(field), current["stateLayout"].get(field))
                self.assertEqual(layout.validate(before), layout.validate(current))
                layout.validate_transition(before, current)
                layout.check_localized_family([current, published(lesson, "en"), published(lesson, "fr")], ["es", "en", "fr"])

    def test_detached_option_does_not_bypass_the_curricular_guard(self):
        for lesson, identity in layout.CHOICE_OPTION_REVISIONS:
            with self.subTest(identity=identity):
                before, current = published(lesson), revised(lesson, identity)
                page, position = slot(identity)
                self.assertFalse(contract.preserves_previous_exercise(PREFIX + asset_path(lesson), page, position,
                                                                     block(before, identity), block(current, identity)))
                aligned = layout.align_for_previous_validation(before, current)
                self.assertTrue(contract.preserves_previous_exercise(PREFIX + asset_path(lesson), page, position,
                                                                    block(before, identity), block(aligned, identity)))
                self.assertIn((asset_path(lesson), page + 1, position + 1), contract.CHOICE_OPTION_REVISION_LOCATIONS)

    def test_after_requires_v2_spanish_identity_archive_and_exact_editorial_edge(self):
        for lesson, identity in layout.CHOICE_OPTION_REVISIONS:
            good = revised(lesson, identity)
            def no_map(doc): doc.pop("stateLayout")
            def v1(doc): doc["stateLayout"] = {key: value for key, value in doc["stateLayout"].items() if key in {"origin", "current"}} | {"version": 1}
            def no_archive(doc): doc["stateLayout"].pop("choiceRevisions")
            def no_proof(doc): doc["stateLayout"]["editorial"] = [proof for proof in doc["stateLayout"]["editorial"] if proof["id"] != identity]
            def wrong_lesson(doc): doc["id"] += "_other"
            def wrong_locale(doc): doc["stateLayout"]["locale"] = "en"
            def wrong_identity(doc): doc["stateLayout"]["choiceRevisions"][0]["id"] = "block_0000_0000"
            for mutate in (no_map, v1, no_archive, no_proof, wrong_lesson, wrong_locale, wrong_identity):
                with self.subTest(lesson=lesson, mutation=mutate.__name__):
                    bad = deepcopy(good); mutate(bad)
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_archive_and_schema_cannot_be_fabricated_or_repeated(self):
        lesson, identity = next(iter(layout.CHOICE_OPTION_REVISIONS))
        for value in (None, {}, [{}], [{"id": identity, "block": []}],
                      [{"id": identity, "block": block(published(lesson), identity), "other": True}]):
            with self.subTest(value=value):
                bad = revised(lesson, identity); bad["stateLayout"]["choiceRevisions"] = value
                with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = revised(lesson, identity)
        bad["stateLayout"]["choiceRevisions"] *= 2
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = revised(lesson, identity)
        bad["stateLayout"]["choiceRevisions"][0]["block"]["reviewFeedback"][0] += " Otra guía."
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_other_options_correct_index_ids_code_and_extra_contract_fields_stay_protected(self):
        for (lesson, identity), spec in layout.CHOICE_OPTION_REVISIONS.items():
            for change in ("other_option", "correctIndex", "id", "code", "type", "option_wording"):
                with self.subTest(lesson=lesson, change=change):
                    bad = revised(lesson, identity); actual = block(bad, identity)
                    if change == "other_option": actual["options"][(spec["option"] + 1) % len(actual["options"])] += " Otra opción."
                    elif change == "option_wording": actual["options"][spec["option"]] += " Otra frase."
                    elif change == "correctIndex": actual["correctIndex"] = (actual["correctIndex"] + 1) % len(actual["options"])
                    elif change == "id": actual["id"] = "another_exercise"
                    elif change == "code": actual["code"] = "return 0;"
                    else: actual["type"] = "output_prediction"
                    replace_terminal_proof(bad, identity)
                    with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_first_revision_requires_exact_guidance_and_consecutive_reviewed_edge(self):
        lesson, identity = next(iter(layout.CHOICE_OPTION_REVISIONS))
        bad = revised(lesson, identity)
        block(bad, identity)["reviewFeedback"][2] += " Texto no revisado."
        replace_terminal_proof(bad, identity)
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = revised(lesson, identity)
        proof = bad["stateLayout"]["editorial"].pop()
        middle = "0" * 64
        bad["stateLayout"]["editorial"].extend([
            {**proof, "afterSha256": middle}, {**proof, "beforeSha256": middle}])
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_two_l8_revisions_append_without_rewriting_first_snapshot_or_provenance(self):
        entries = [(lesson, identity) for lesson, identity in layout.CHOICE_OPTION_REVISIONS if lesson.endswith("__tokens_con_categoria_y_valor")]
        lesson, first = entries[0]; second = entries[1][1]
        one = revised(lesson, first); both = layout.revise_choice_option(one, second)
        self.assertEqual(one["stateLayout"]["choiceRevisions"], both["stateLayout"]["choiceRevisions"][:1])
        self.assertEqual(one["stateLayout"]["editorial"], both["stateLayout"]["editorial"][:-1])
        layout.validate_transition(one, both)
        bad = deepcopy(both); bad["stateLayout"]["choiceRevisions"].reverse()
        layout.validate(bad)
        with self.assertRaisesRegex(layout.LayoutError, "append-only"): layout.validate_transition(both, bad)

    def test_retirement_reading_snapshot_cannot_invent_a_revision_without_its_archive(self):
        for (lesson, identity), spec in layout.CHOICE_OPTION_REVISIONS.items():
            with self.subTest(identity=identity):
                before = published(lesson)
                current = layout.retire(before, ["block_0000_0001"])
                forged = historical_block(current, identity)
                forged["options"][spec["option"]] = spec["after"]
                if spec["feedback"] is not None:
                    forged["reviewFeedback"][spec["option"]] = spec["feedback"]
                if "paired_explanation" in spec:
                    forged["explanation"] = spec["paired_explanation"]["after"]
                self.assertEqual(spec["full_after"], layout.fingerprint(forged))
                self.assertEqual(spec["full_before"], layout.fingerprint(block(current, identity)))
                self.assertNotIn("choiceRevisions", current["stateLayout"])
                with self.assertRaisesRegex(layout.LayoutError, "historical revised choice"):
                    layout.validate(current)
                with self.assertRaises(layout.LayoutError): layout.validate_transition(before, current)

    def test_reading_history_keeps_its_real_choice_version_before_and_after_revision(self):
        for (lesson, identity), spec in layout.CHOICE_OPTION_REVISIONS.items():
            with self.subTest(identity=identity):
                before = published(lesson)
                retired_first = layout.retire(before, ["block_0000_0001"])
                old_history = deepcopy(retired_first["stateLayout"]["retirementHistory"])
                revised_later = layout.revise_choice_option(retired_first, identity)
                layout.validate(revised_later)
                layout.validate_transition(retired_first, revised_later)
                self.assertEqual(old_history, revised_later["stateLayout"]["retirementHistory"])
                self.assertEqual(spec["full_before"], layout.fingerprint(historical_block(revised_later, identity)))
                revision_first = layout.revise_choice_option(before, identity)
                retired_later = layout.retire(revision_first, ["block_0000_0001"])
                layout.validate(retired_later)
                layout.validate_transition(revision_first, retired_later)
                self.assertEqual(spec["full_after"], layout.fingerprint(historical_block(retired_later, identity)))
                self.assertEqual(revision_first["stateLayout"]["choiceRevisions"], retired_later["stateLayout"]["choiceRevisions"])

    def test_historical_after_cannot_borrow_another_activitys_revision_archive(self):
        lesson, identity = next(iter(layout.CHOICE_OPTION_REVISIONS))
        current = layout.retire(revised(lesson, identity), ["block_0000_0001"])
        page, position = slot(identity)
        blocks = current["stateLayout"]["retirementHistory"][0]["pages"][page]["blocks"]
        blocks[position - 1], blocks[position] = blocks[position], blocks[position - 1]
        with self.assertRaisesRegex(layout.LayoutError, "historical revised choice"):
            layout.validate(current)

    def test_retirement_after_a_later_prose_revision_preserves_that_exact_presentation(self):
        lesson, identity = next(iter(layout.CHOICE_OPTION_REVISIONS))
        before = revised(lesson, identity); draft = deepcopy(before)
        block(draft, identity)["explanation"] += " Sigue el objeto que conserva cada estado."
        family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
        future = layout.prepare_v2(draft, "es", family, previous=before)
        current = layout.retire(future, ["block_0000_0001"])
        layout.validate(current)
        layout.validate_transition(future, current)
        self.assertEqual(block(future, identity), historical_block(current, identity))
        self.assertEqual(before["stateLayout"]["choiceRevisions"], current["stateLayout"]["choiceRevisions"])

    def test_future_prose_and_guidance_extend_chain_without_rewriting_the_archive(self):
        for lesson, identity in layout.CHOICE_OPTION_REVISIONS:
            with self.subTest(identity=identity):
                before = revised(lesson, identity); draft = deepcopy(before)
                actual = block(draft, identity); old_question = actual["question"]
                actual["question"] += " Sigue los objetos lectores."
                actual["reviewFeedback"][0] += " Conserva la posición de lectura."
                family = {locale: published(lesson, locale) for locale in ("es", "en", "fr")}
                current = layout.prepare_v2(draft, "es", family, previous=before)
                layout.validate_transition(published(lesson), current)
                self.assertEqual(before["stateLayout"]["choiceRevisions"], current["stateLayout"]["choiceRevisions"])
                self.assertEqual(before["stateLayout"]["editorial"], current["stateLayout"]["editorial"][:-1])
                self.assertEqual(current, layout.revise_choice_option(current, identity))
                page, position = slot(identity); aligned = layout.align_for_previous_validation(before, current)
                self.assertEqual(actual["question"], block(aligned, identity)["question"])
                self.assertEqual(actual["reviewFeedback"], block(aligned, identity)["reviewFeedback"])
                self.assertFalse(contract.preserves_previous_exercise(PREFIX + asset_path(lesson), page, position,
                                                                     block(before, identity), block(aligned, identity)))
                key = (asset_path(lesson), page + 1, position + 1, "multiple_choice", "question", actual.get("id"))
                pair = tuple(hashlib.sha256(value.encode("utf-8")).hexdigest() for value in (old_question, actual["question"]))
                with mock.patch.dict(contract.WORK_EXACT_EDITORIAL_REVISIONS, {key: pair}):
                    self.assertTrue(contract.preserves_previous_exercise(PREFIX + asset_path(lesson), page, position,
                                                                        block(before, identity), block(aligned, identity)))

    def test_reorder_preserves_choice_history_and_cannot_reattach_it_to_another_identity(self):
        lesson, identity = next(iter(layout.CHOICE_OPTION_REVISIONS))
        current = revised(lesson, identity)
        plan = deepcopy(current["stateLayout"]["current"])
        page, position = slot(identity)
        plan[page]["blocks"][position - 1], plan[page]["blocks"][position] = plan[page]["blocks"][position], plan[page]["blocks"][position - 1]
        moved = layout.reorder(current, plan)
        layout.validate_transition(published(lesson), moved)
        self.assertEqual(current["stateLayout"]["choiceRevisions"], moved["stateLayout"]["choiceRevisions"])
        self.assertEqual(block(current, identity), block(moved, identity))
        self.assertEqual((page, position), layout.validate(moved)[page, position - 1])
        bad = deepcopy(moved); bad["stateLayout"]["choiceRevisions"][0]["id"] = "block_0006_0003"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_helper_refuses_other_activity_or_changed_pre_revision_snapshot(self):
        lesson, identity = next(iter(layout.CHOICE_OPTION_REVISIONS))
        with self.assertRaises(layout.LayoutError): layout.revise_choice_option(published(lesson), "block_0000_0000")
        before = published(lesson); block(before, identity)["question"] += " Otra pregunta."
        proof = {"id": identity, "beforeSha256": layout.CHOICE_OPTION_REVISIONS[lesson, identity]["full_before"],
                 "afterSha256": layout.fingerprint(block(before, identity))}
        before["stateLayout"]["editorial"].append(proof); layout.validate(before)
        untouched = deepcopy(before)
        with self.assertRaises(layout.LayoutError): layout.revise_choice_option(before, identity)
        self.assertEqual(untouched, before)

    def test_cli_produces_only_a_new_proposal_and_never_overwrites_source_or_output(self):
        lesson, identity = next(iter(layout.CHOICE_OPTION_REVISIONS))
        source = contract.ROOT / (PREFIX + asset_path(lesson)); original_bytes = source.read_bytes()
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "proposal.json"
            command = [sys.executable, str(Path(layout.__file__)), "revise-choice-option", str(source), "--locale", "es",
                       "--base-ref", BASELINE, "--identity", identity, "--output", str(output)]
            result = subprocess.run(command, cwd=contract.ROOT, capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            proposal = output.read_bytes(); self.assertEqual(revised(lesson, identity), json.loads(proposal))
            again = subprocess.run(command, cwd=contract.ROOT, capture_output=True, text=True, encoding="utf-8")
            self.assertNotEqual(0, again.returncode)
            self.assertEqual(proposal, output.read_bytes())
        self.assertEqual(original_bytes, source.read_bytes())


if __name__ == "__main__":
    unittest.main()

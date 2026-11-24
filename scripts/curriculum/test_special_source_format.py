"""Authoring and unchanged-contract QA for three authentic special objectives."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import unittest

import special_source_format_fixture_source as fixture
import stable_state_layout as state


def snapshot_blocks(snapshot: dict) -> dict[str, dict]:
    return {identity: block
            for page, record in zip(snapshot["pages"], snapshot["current"])
            for identity, block in zip(record["blocks"], page["blocks"])}


class SpecialSourceFormatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.export_payloads()
        cls.manifest = json.loads(cls.payloads["manifest.json"])
        cls.lessons = {entry["id"]: entry for entry in cls.manifest["lessons"]}
        cls.pairs = {
            name: tuple(json.loads(cls.payloads[entry[key]]) for key in ("before", "after"))
            for name, entry in cls.lessons.items()}

    def test_complete_publications_and_isolated_format_archives(self):
        self.assertEqual(self.manifest["publishedCommit"], fixture.PUBLISHED_COMMIT)
        self.assertEqual(set(self.lessons), {"crt", "exit_stream", "exit_guard"})
        self.assertEqual(len(self.payloads), 42)
        for name, (before, after) in self.pairs.items():
            with self.subTest(publication=name):
                entry, pin = self.lessons[name], fixture.PUBLICATIONS[name]
                owner = pin["owner"]
                before_bytes = self.payloads[entry["before"]]
                self.assertEqual(fixture.verify_published_bytes(name, before_bytes), before_bytes)
                self.assertEqual(entry["beforeSha256"], hashlib.sha256(before_bytes).hexdigest())
                self.assertEqual(entry["afterSha256"], hashlib.sha256(self.payloads[entry["after"]]).hexdigest())
                original, current = state._blocks(before)[owner], state._blocks(after)[owner]
                self.assertNotEqual(original["initialCode"], current["initialCode"])
                self.assertTrue(state.code_format.require_source_format(original, current))
                self.assertEqual(current["outputCheck"], original["outputCheck"])
                projected = state.code_format.source_projection(current, original)
                self.assertEqual(projected, original)
                self.assertEqual(state.state_fingerprint(projected), state.state_fingerprint(original))
                self.assertEqual(state.validate(before), state.validate(after))
                # Only contextual projection gains format compatibility. The
                # exact-byte special recognizers themselves remain unchanged.
                if name == "crt":
                    self.assertTrue(state.reviewed_crt_output_variant(original))
                    self.assertFalse(state.reviewed_crt_output_variant(current))
                else:
                    self.assertTrue(state.reviewed_intentional_exit_goal(original))
                    self.assertFalse(state.reviewed_intentional_exit_goal(current))
                previous, revised = before["stateLayout"], after["stateLayout"]
                old_archives = previous.get("sourceFormatRevisions", [])
                self.assertEqual(revised["sourceFormatRevisions"], [
                    *old_archives, {"id": owner, "block": original}])
                self.assertEqual(revised["editorial"][:-1], previous["editorial"])
                excluded = {"sourceFormatRevisions", "editorial"}
                self.assertEqual({key: value for key, value in previous.items() if key not in excluded},
                                 {key: value for key, value in revised.items() if key not in excluded})

    def test_repeated_authoring_uses_complete_family_and_original_archive(self):
        for name, (before, after) in self.pairs.items():
            with self.subTest(publication=name):
                owner = fixture.PUBLICATIONS[name]["owner"]
                family = {locale: state.parse_json(fixture.published_bytes(name, locale).decode("utf-8"))
                          for locale in before["stateLayout"]["family"]}
                edited = deepcopy(after)
                current = state._blocks(edited)[owner]
                current["initialCode"] = fixture.formatted_source(current["initialCode"])
                revised = state.prepare_v2(edited, "es", family, after)
                state.validate_transition(before, revised)
                self.assertEqual(revised["stateLayout"]["sourceFormatRevisions"],
                                 after["stateLayout"]["sourceFormatRevisions"])
                self.assertEqual(revised["stateLayout"]["editorial"][:-1], after["stateLayout"]["editorial"])
                self.assertEqual(state._blocks(revised)[owner]["outputCheck"],
                                 state._blocks(before)[owner]["outputCheck"])
                self.assertEqual(state.validate(before), state.validate(revised))

    def test_shared_negative_documents_reject_load_and_transition(self):
        counts = {"crt": 10, "exit_stream": 8, "exit_guard": 8}
        resealed = {"source_token", "removed_output_check", "changed_stdout",
                    "changed_exit_code", "changed_stdin", "removed_crlf_variant", "changed_crlf_variant"}
        for name, (before, after) in self.pairs.items():
            owner = fixture.PUBLICATIONS[name]["owner"]
            cases = self.lessons[name]["negative"]
            self.assertEqual(len(cases), counts[name])
            for case in cases:
                with self.subTest(publication=name, case=case["name"]):
                    payload = self.payloads[case["path"]]
                    self.assertEqual(case["sha256"], hashlib.sha256(payload).hexdigest())
                    document = json.loads(payload)
                    if case["name"] in resealed:
                        head = next(edge for edge in reversed(document["stateLayout"]["editorial"])
                                    if edge["id"] == owner)
                        self.assertEqual(head["afterSha256"], state.fingerprint(state._blocks(document)[owner]))
                    with self.assertRaises(state.LayoutError):
                        state.validate(document)
                    with self.assertRaises(state.LayoutError):
                        state.validate_transition(before, document)
                    with self.assertRaises(state.LayoutError):
                        state.validate_transition(after, document)

    def test_authenticated_history_before_and_after_format(self):
        for name, (before, after) in self.pairs.items():
            owner = fixture.PUBLICATIONS[name]["owner"]
            records = {record["name"]: record for record in self.lessons[name]["historical"]}
            self.assertEqual(set(records), {"format_after_retirement", "retirement_after_format",
                                            "reformat_after_retirement"})
            for case, record in records.items():
                with self.subTest(publication=name, case=case):
                    payload = self.payloads[record["path"]]
                    self.assertEqual(record["sha256"], hashlib.sha256(payload).hexdigest())
                    document = json.loads(payload)
                    state.validate(document)
                    predecessor = before if case == "format_after_retirement" else after
                    state.validate_transition(predecessor, document)
                    snapshot, = document["stateLayout"]["retirementHistory"]
                    self.assertEqual(snapshot["retired"], ["block_0000_0000"])
                    frozen = snapshot_blocks(snapshot)[owner]
                    self.assertEqual(frozen, state._blocks(predecessor)[owner])
                    prefix = snapshot["editorialPrefix"]
                    self.assertEqual(prefix, state._retirement_editorial_prefix(document, prefix["count"]))
                    historical = state._historical_source_format_contract(
                        document, owner, frozen, editorial_prefix=prefix["count"])
                    self.assertEqual(historical, state._blocks(before)[owner])
                    self.assertIn(owner, state._blocks(document))
                    self.assertEqual(state._blocks(document)[owner]["outputCheck"], frozen["outputCheck"])
                    self.assertEqual(document["stateLayout"]["sourceFormatRevisions"],
                                     after["stateLayout"]["sourceFormatRevisions"])

    def test_original_archive_and_historical_source_cannot_be_replaced(self):
        for name, (before, after) in self.pairs.items():
            owner = fixture.PUBLICATIONS[name]["owner"]
            with self.subTest(publication=name, case="replaced_archive"):
                changed = deepcopy(after)
                archive = next(entry for entry in changed["stateLayout"]["sourceFormatRevisions"]
                               if entry["id"] == owner)
                archive["block"] = deepcopy(state._blocks(after)[owner])
                with self.assertRaises(state.LayoutError):
                    state.validate(changed)
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(before, changed)
            with self.subTest(publication=name, case="rewritten_snapshot"):
                record = next(record for record in self.lessons[name]["historical"]
                              if record["name"] == "reformat_after_retirement")
                changed = json.loads(self.payloads[record["path"]])
                snapshot = changed["stateLayout"]["retirementHistory"][0]
                block = snapshot_blocks(snapshot)[owner]
                block["initialCode"] = fixture.formatted_source(block["initialCode"])
                with self.assertRaises(state.LayoutError):
                    state.validate(changed)

    def test_format_channel_guard_preserves_authenticated_identifier_rename(self):
        # This later, separately authenticated source epoch was already
        # supported. Channel checks must not re-lex it as a whitespace edit.
        _, formatted = self.pairs["exit_stream"]
        owner = fixture.PUBLICATIONS["exit_stream"]["owner"]
        revised = deepcopy(formatted)
        original, current = state._blocks(formatted)[owner], state._blocks(revised)[owner]
        old_name, new_name = "nuevo_aceptados", "aceptados_leidos"
        self.assertEqual(original["initialCode"].count(old_name), 3)
        current["initialCode"] = current["initialCode"].replace(old_name, new_name)
        names = {"initialCode": {old_name: new_name}}
        occurrences = {"initialCode": {old_name: 3}}
        confirmed = state.identifier_rename.require_declared_cpp_renames(
            original, current, names, occurrences)
        meta = revised["stateLayout"]
        self.assertNotIn("identifierRenameRevisions", meta)
        _, storage, digests = state._origin(meta["origin"])
        page, block = storage[owner]
        archive = next(item["block"] for item in meta["sourceFormatRevisions"] if item["id"] == owner)
        record = {
            "id": owner, "block": deepcopy(original), "lessonId": revised["id"], "locale": "es",
            "historicalFamilySha256": meta["family"]["es"]["sha256"],
            "originalStateSha256": digests[owner], "stableStorage": {"page": page, "block": block},
            "editorialPrefixSha256": state.fingerprint({
                "lessonId": revised["id"], "locale": "es", "editorial": meta["editorial"]}),
            "names": names, "occurrences": occurrences, "confirmed": confirmed,
            "beforeSha256": state.fingerprint(original), "afterSha256": state.fingerprint(current),
            "priorSourceFormatSha256": state.fingerprint(archive),
        }
        meta["identifierRenameRevisions"] = [record]
        meta["editorial"].append({"id": owner, "beforeSha256": record["beforeSha256"],
                                  "afterSha256": record["afterSha256"]})
        state.validate_transition(formatted, revised)
        self.assertEqual(state.validate(formatted), state.validate(revised))
        self.assertEqual(current["outputCheck"], original["outputCheck"])

    def test_editorial_return_cannot_remove_the_projected_special_goal(self):
        def returned(after: dict, owner: str, target: dict) -> dict:
            document = deepcopy(after)
            original = state._blocks(after)[owner]
            current = state._blocks(document)[owner]
            current.clear()
            current.update(deepcopy(target))
            history = document["stateLayout"]["editorial"]
            edges = [edge for edge in history if edge["id"] == owner]
            nodes = [edges[0]["beforeSha256"], *(edge["afterSha256"] for edge in edges)]
            digest = state.fingerprint(target)
            revision = nodes.index(digest)
            history.append({
                "id": owner, "beforeSha256": state.fingerprint(original), "afterSha256": digest,
                "returnTo": {"revision": revision, "previousBlock": deepcopy(original),
                             "historySha256": state.fingerprint({
                                 "lessonId": document["id"], "locale": "es", "editorial": history})}})
            return document

        for name, (before, after) in self.pairs.items():
            with self.subTest(publication=name):
                owner = fixture.PUBLICATIONS[name]["owner"]
                reviewed = state._blocks(before)[owner]
                formatted = state._blocks(after)[owner]
                if name == "crt":
                    target = state.without_compiler_guidance(state.historical_crt_output_payload(reviewed))
                    expected = state.CRT_OUTPUT_REVISION["before"]
                    self.assertFalse(state.reviewed_crt_output_variant(formatted))
                    self.assertTrue(state.reviewed_crt_output_variant(
                        state.code_format.source_projection(formatted, reviewed)))
                else:
                    target = deepcopy(reviewed)
                    target.pop("outputCheck")
                    expected = state.reviewed_intentional_exit_spec(reviewed)["before"]
                    self.assertFalse(state.reviewed_intentional_exit_goal(formatted))
                    self.assertTrue(state.reviewed_intentional_exit_goal(
                        state.code_format.source_projection(formatted, reviewed)))
                # The removed-goal target is the exact authenticated original
                # editorial node, rather than a newly invented close variant.
                self.assertEqual(state.fingerprint(target), expected)
                invalid = returned(after, owner, target)
                # Stop at layout validation: the later archive-permanence
                # validators cannot make this return guard test pass for it.
                with self.assertRaisesRegex(state.LayoutError, "cannot be removed or replaced by an editorial return"):
                    state._validate_state_layout(invalid)
                valid = returned(after, owner, reviewed)
                state.validate_transition(after, valid)
                self.assertEqual(state._blocks(valid)[owner]["outputCheck"], reviewed["outputCheck"])


if __name__ == "__main__":
    unittest.main()

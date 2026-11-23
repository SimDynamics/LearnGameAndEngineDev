"""Authenticate U2L5's original and later physical compiler inputs."""
from __future__ import annotations

from copy import deepcopy
import json
import unittest
from unittest import mock

import test_local_int_live_compilation as live


class LocalIntLiveHistoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = live.publication_payloads()

    def altered_current(self, mutate):
        document = json.loads(self.payloads["current"])
        mutate(document)
        return {**self.payloads, "current": json.dumps(document, ensure_ascii=False).encode("utf-8")}

    def test_full_publications_keep_original_sources_and_current_physical_inputs(self):
        documents = live.verify_publications(self.payloads)
        original, formatted, baseline, current = documents.values()
        self.assertEqual([], original["stateLayout"].get("sourceFormatRevisions", []))
        self.assertEqual(20, len(formatted["stateLayout"]["sourceFormatRevisions"]))
        self.assertEqual(20, len(baseline["stateLayout"]["sourceFormatRevisions"]))
        self.assertEqual([44, 64, 79], [len(doc["stateLayout"]["editorial"])
                                       for doc in (original, formatted, baseline)])
        old, spaced, active = map(lambda doc: live.history.active_owners(doc)[0],
                                  (original, formatted, current))
        for _, owner, reference, _, _ in live.CONTRACTS:
            with self.subTest(owner=owner):
                self.assertNotEqual(old[reference]["code"], spaced[reference]["code"])
                self.assertEqual(old[owner]["outputCheck"], active[owner]["outputCheck"])
        self.assertEqual(self.payloads["current"], (live.ROOT / live.RELATIVE).read_bytes())

    def test_every_complete_historical_publication_rejects_changed_bytes(self):
        for name in live.PUBLICATIONS:
            with self.subTest(publication=name):
                with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                    live.verify_publications({**self.payloads, name: self.payloads[name] + b"\n"})

    def test_current_cannot_roll_back_or_erase_published_history(self):
        for name in ("original", "formatted"):
            with self.subTest(rollback=name):
                with self.assertRaises(live.state.LayoutError):
                    live.verify_publications({**self.payloads, "current": self.payloads[name]})
        for field in ("sourceFormatRevisions", "retirementHistory", "editorial"):
            with self.subTest(field=field):
                with self.assertRaises((live.state.LayoutError, ValueError)):
                    live.verify_publications(self.altered_current(
                        lambda document: document["stateLayout"][field].pop()))

    def test_tokens_goal_probe_and_canonical_source_are_independently_protected(self):
        def source(document):
            block = live.history.active_owners(document)[0]["block_0007_0001"]
            block["code"] = block["code"].replace("9.0", "10.0", 1)

        def goal(document):
            block = live.history.active_owners(document)[0]["block_0006_0002"]
            block["outputCheck"]["cases"][0]["outputs"][0] = "Parte: 9 m"

        def probe(document):
            document["compilerLocalIntChecks"][0]["probes"][0]["replacement"] += 1

        def canonical(document):
            entry = next(item for item in document["stateLayout"]["sourceFormatRevisions"]
                         if item["id"] == "block_0007_0001")
            entry["block"]["code"] += "\nint forged_format_token;\n"

        for mutate in (source, goal, probe, canonical):
            with self.subTest(mutation=mutate.__name__):
                with self.assertRaises((live.state.LayoutError, ValueError)):
                    live.verify_publications(self.altered_current(mutate))

    def test_later_authored_spacing_is_accepted_without_replacing_any_archive(self):
        document = json.loads(self.payloads["current"])
        before = deepcopy(document)
        identity = "block_0007_0001"
        block = live.history.active_owners(document)[0][identity]
        original = deepcopy(block)
        block["code"] = "\n" + block["code"]
        document["stateLayout"]["editorial"].append({
            "id": identity, "beforeSha256": live.state.fingerprint(original),
            "afterSha256": live.state.fingerprint(block)})
        live.state.validate_transition(before, document)
        payloads = {**self.payloads, "current": json.dumps(document, ensure_ascii=False).encode("utf-8")}
        live.verify_publications(payloads)
        self.assertEqual(before["stateLayout"]["sourceFormatRevisions"],
                         document["stateLayout"]["sourceFormatRevisions"])
        self.assertEqual(before["stateLayout"]["retirementHistory"],
                         document["stateLayout"]["retirementHistory"])
        self.assertEqual(before["stateLayout"]["editorial"],
                         document["stateLayout"]["editorial"][:-1])
        self.assertNotEqual(payloads["current"], self.payloads["current"])

    def test_missing_git_publications_never_fall_back_to_the_current_asset(self):
        with mock.patch.object(live.history, "publication_bytes",
                               side_effect=FileNotFoundError("missing historical Git object")):
            with self.assertRaises(FileNotFoundError):
                live.publication_payloads()


if __name__ == "__main__":
    unittest.main()

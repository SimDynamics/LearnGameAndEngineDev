"""Explicit same-owner editorial returns preserve their complete published prefix."""
from copy import deepcopy
from functools import lru_cache
import io
import json
from contextlib import redirect_stderr, redirect_stdout
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
UNIT = "app/src/main/assets/content/{}/programming/cpp_fundamentals/objetos_y_estado/lesson_estado_asignacion_y_copia.json"
ACTUAL_A = "12cf6c17ce1492b403b5f649a95be01a9cd8cc9e"
ACTUAL_B = "22443d88e807a330f05ca2ccc2f34df57d9f3b82"
ACTUAL_CASES = (
    ("block_0006_0001", "56aa7fbc312649200f5c8e4f09b9ee892be872d19cb8d378a5e4048b608386b6",
     "be3322fd95ee7ff42a3e52e0092b7da737a878d666133f49cff9cc651f788971", "Antes: `5`\nAhora: `8`", "Antes: 5\nAhora: 8"),
    ("block_0006_0005", "5459af71fdbd2bb10f0e3733cad545e8eeb9538bf7c80186161ac26d0e6ecaa6",
     "81d903df40525897458b6629f1f64cddd3c0ed8743bc22b7c796d6f23b6bc4b9", "Antes: `8`\nAhora: `8`", "Antes: 8\nAhora: 8"),
)


@lru_cache(maxsize=None)
def published(commit, locale):
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1")
    payload = subprocess.check_output(["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
        "show", f"{commit}:{UNIT.format(locale)}"], cwd=ROOT, env=environment)
    return layout.parse_json(payload.decode("utf-8-sig"))


def family_fixture():
    document = {"id": "qa_editorial_return", "title": "QA", "pages": [{"blocks": [
        {"type": "paragraph", "text": "A"}, {"type": "paragraph", "text": "Independent source"},
        {"type": "output_prediction", "question": "Original question", "answers": ["1"]},
        {"type": "cpp_compiler", "initialCode": "int main() {}"},
    ]}]}
    return {locale: deepcopy(document) for locale in ("es", "en", "fr")}


def block(document, identity="block_0000_0000"):
    return layout._blocks(document)[identity]


def prefix_fingerprint(document, prefix):
    return layout.fingerprint({"lessonId": document["id"], "locale": document["stateLayout"]["locale"], "editorial": prefix})


def versions():
    family = family_fixture(); one = layout.prepare_v2(family["es"], "es", family)
    changed = deepcopy(one); block(changed)["text"] = "B"
    block(changed, "block_0000_0001")["text"] = "Independent second version"
    two = layout.prepare_v2(changed, "es", family, one)
    changed = deepcopy(two); block(changed)["text"] = "A"
    returned = layout.prepare_v2(changed, "es", family, two, editorial_returns={"block_0000_0000": 0})
    return family, one, two, returned


class EditorialReturnTest(unittest.TestCase):
    def test_return_is_explicit_and_binds_the_complete_global_prefix(self):
        family, one, two, returned = versions()
        before = deepcopy(two); source = deepcopy(two); block(source)["text"] = "A"
        with self.assertRaisesRegex(layout.LayoutError, "Cyclic editorial proof"):
            layout.prepare_v2(source, "es", family, two)
        self.assertEqual(two, before)
        self.assertEqual(two["stateLayout"]["editorial"], returned["stateLayout"]["editorial"][:-1])
        edge = returned["stateLayout"]["editorial"][-1]
        self.assertEqual({"revision": 0, "historySha256": prefix_fingerprint(two, two["stateLayout"]["editorial"]),
                          "previousBlock": block(two)}, edge["returnTo"])
        self.assertIsNot(block(two), edge["returnTo"]["previousBlock"])
        self.assertEqual(layout.fingerprint(block(one)), edge["afterSha256"])
        self.assertEqual(layout.validate(one), layout.validate(returned))
        layout.validate_transition(one, returned)  # Equal payload after skipped B, history stays complete.
        layout.validate_transition(two, returned)
        self.assertEqual(returned, layout.prepare_v2(returned, "es", family, returned))

    def test_a_return_does_not_implicitly_authorize_later_repeated_payloads(self):
        family, _, _, returned = versions()
        source = deepcopy(returned); block(source)["text"] = "B"
        with self.assertRaises(layout.LayoutError): layout.prepare_v2(source, "es", family, returned)
        repeated = layout.prepare_v2(source, "es", family, returned, editorial_returns={"block_0000_0000": 1})
        self.assertEqual(1, repeated["stateLayout"]["editorial"][-1]["returnTo"]["revision"])
        source = deepcopy(repeated); block(source)["text"] = "New payload"
        ordinary = layout.prepare_v2(source, "es", family, repeated)
        self.assertNotIn("returnTo", ordinary["stateLayout"]["editorial"][-1])
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(source, "es", family, repeated, editorial_returns={"block_0000_0000": 0})

    def test_return_schema_indices_and_false_proofs_are_rejected(self):
        _, _, _, good = versions()
        marker = good["stateLayout"]["editorial"][-1]["returnTo"]
        values = (None, False, 0, [], {}, {"revision": 0}, {"historySha256": marker["historySha256"]},
                  {key: value for key, value in marker.items() if key != "previousBlock"},
                  {**marker, "extra": True}, {**marker, "revision": True}, {**marker, "revision": 0.0},
                  {**marker, "revision": -1}, {**marker, "revision": 1}, {**marker, "revision": 200},
                  {**marker, "historySha256": "0" * 64}, {**marker, "historySha256": "BAD"})
        for value in values:
            with self.subTest(marker=value):
                bad = deepcopy(good); bad["stateLayout"]["editorial"][-1]["returnTo"] = value
                with self.assertRaises(layout.LayoutError): layout.validate(bad)
        ordinary = deepcopy(good); ordinary["stateLayout"]["editorial"][0]["returnTo"] = marker
        with self.assertRaises(layout.LayoutError): layout.validate(ordinary)

    def test_previous_block_archive_requires_its_complete_hash_and_protected_contract(self):
        _, _, _, good = versions()
        for archive in (None, [], {}, {"type": "paragraph", "text": "WRONG"},
                        {"type": "paragraph", "text": "B", "id": "other_owner"}):
            with self.subTest(archive=archive):
                bad = deepcopy(good)
                bad["stateLayout"]["editorial"][-1]["returnTo"]["previousBlock"] = archive
                with self.assertRaises(layout.LayoutError): layout.validate(bad)
        # Matching a forged before hash alone must not authorize an archived
        # response/code/identity contract that differs from its active owner.
        for kind, changes in (("answer", {"type": "output_prediction", "question": "B", "answers": ["OTHER"]}),
                              ("code", {"type": "cpp_compiler", "initialCode": "OTHER"}),
                              ("identity", {"type": "paragraph", "text": "B", "id": "other_owner"})):
            with self.subTest(kind=kind):
                bad = deepcopy(good); history = bad["stateLayout"]["editorial"]
                archive_hash = layout.fingerprint(changes)
                history[0]["afterSha256"] = archive_hash
                history[-1]["beforeSha256"] = archive_hash
                history[-1]["returnTo"]["previousBlock"] = changes
                history[-1]["returnTo"]["historySha256"] = prefix_fingerprint(bad, history[:-1])
                with self.assertRaisesRegex(layout.LayoutError, "previous block.*contract"):
                    layout.validate(bad)

    def test_wrong_owner_locale_and_modified_global_prefix_do_not_reuse_a_return(self):
        _, _, previous, good = versions()
        def owner(doc): doc["stateLayout"]["editorial"][-1]["id"] = "block_0000_0001"
        def locale(doc): doc["stateLayout"]["locale"] = "en"
        def lesson(doc): doc["id"] += "_other"
        def truncated(doc): doc["stateLayout"]["editorial"].pop(1)
        def reordered(doc): doc["stateLayout"]["editorial"][:2] = reversed(doc["stateLayout"]["editorial"][:2])
        for change in (owner, locale, lesson, truncated, reordered):
            with self.subTest(change=change.__name__):
                bad = deepcopy(good); change(bad)
                with self.assertRaises(layout.LayoutError): layout.validate_transition(previous, bad)
        # Even recomputing a forged shorter prefix cannot erase published history.
        bad = deepcopy(good); truncated(bad)
        bad["stateLayout"]["editorial"][-1]["returnTo"]["historySha256"] = prefix_fingerprint(bad, bad["stateLayout"]["editorial"][:-1])
        with self.assertRaisesRegex(layout.LayoutError, "append-only"): layout.validate_transition(previous, bad)

    def test_explicit_requests_cannot_claim_noops_or_unpublished_nodes(self):
        family, one, two, good = versions()
        for requests in ([], {"block_0000_0000": True}, {"block_0000_0000": -1},
                         {"unknown": 0}, {"block_0000_0001": 0}):
            with self.subTest(requests=requests), self.assertRaises(layout.LayoutError):
                layout.prepare_v2(two, "es", family, two, editorial_returns=requests)
        with self.assertRaisesRegex(layout.LayoutError, "unchanged published proof prefix"):
            layout.prepare_v2(good, "es", family, two, editorial_returns={"block_0000_0000": 0})
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(family["es"], "es", family, editorial_returns={"block_0000_0000": 0})
        self.assertEqual(one, layout.prepare_v2(one, "es", family, one))

    def test_valid_return_cannot_change_protected_fields_or_publish_fake_noop_history(self):
        _, one, previous, good = versions()
        for kind in ("answer", "code", "type", "origin", "family", "storage"):
            with self.subTest(kind=kind):
                bad = deepcopy(good)
                if kind == "answer": block(bad, "block_0000_0002")["answers"] = ["OTHER"]
                if kind == "code": block(bad, "block_0000_0003")["initialCode"] = "OTHER"
                if kind == "type": block(bad)["type"] = "note"
                if kind == "origin": bad["stateLayout"]["origin"][0]["id"] = "other_page"
                if kind == "family": bad["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                if kind == "storage": bad["stateLayout"]["current"][0]["blocks"][:2] = reversed(bad["stateLayout"]["current"][0]["blocks"][:2])
                with self.assertRaises(layout.LayoutError): layout.validate_transition(previous, bad)
        bad = deepcopy(one); original_hash = layout.fingerprint(block(one))
        bad["stateLayout"]["editorial"] = [{"id": "block_0000_0000", "beforeSha256": "0" * 64, "afterSha256": original_hash}]
        with self.assertRaises(layout.LayoutError): layout.validate_transition(one, bad)

    def test_both_real_u2_paragraphs_return_from_exact_published_a_and_b_without_asset_writes(self):
        # Immutable Git snapshots avoid depending on Work leaving the current
        # assets at their intermediate presentation after this fix is integrated.
        before = deepcopy(published(ACTUAL_A, "es")); current = deepcopy(published(ACTUAL_B, "es"))
        family = {locale: deepcopy(published(ACTUAL_B, locale)) for locale in ("es", "en", "fr")}
        live_paths = [ROOT / UNIT.format(locale) for locale in ("es", "en", "fr")]
        live = {path: path.read_bytes() for path in live_paths}
        source = deepcopy(current)
        for identity, intermediate, original, old, neutral in ACTUAL_CASES:
            self.assertEqual(intermediate, layout.fingerprint(block(current, identity)))
            self.assertEqual(original, layout.fingerprint(block(before, identity)))
            self.assertEqual(1, block(source, identity)["text"].count(old))
            block(source, identity)["text"] = block(source, identity)["text"].replace(old, neutral)
            self.assertEqual(original, layout.fingerprint(block(source, identity)))
            self.assertEqual(layout.state_fingerprint(block(current, identity)), layout.state_fingerprint(block(source, identity)))
        prepared = layout.prepare_v2(source, "es", family, current,
            editorial_returns={identity: 0 for identity, *_ in ACTUAL_CASES})
        layout.validate_transition(current, prepared); layout.validate_transition(before, prepared)
        self.assertEqual(current["stateLayout"]["editorial"], prepared["stateLayout"]["editorial"][:-2])
        for offset in (2, 1):
            proof = prepared["stateLayout"]["editorial"][-offset]
            self.assertEqual({"revision": 0, "historySha256": prefix_fingerprint(prepared, prepared["stateLayout"]["editorial"][:-offset]),
                              "previousBlock": block(current, proof["id"])}, proof["returnTo"])
        for identity, current_block in layout._blocks(current).items():
            if identity not in {case[0] for case in ACTUAL_CASES}:
                self.assertEqual(current_block, block(prepared, identity))
        for field in current["stateLayout"]:
            if field != "editorial": self.assertEqual(current["stateLayout"][field], prepared["stateLayout"][field])
        self.assertEqual(live, {path: path.read_bytes() for path in live_paths})

    def test_cli_explicit_return_and_exclusive_output_preserve_the_source(self):
        family, _, previous, _ = versions(); source = deepcopy(previous); block(source)["text"] = "A"
        expected = layout.prepare_v2(source, "es", family, previous, editorial_returns={"block_0000_0000": 0})
        with tempfile.TemporaryDirectory() as temporary:
            source_file = Path(temporary) / "source.json"; output = Path(temporary) / "proposal.json"
            source_bytes = (json.dumps(source, ensure_ascii=False) + "\n").encode("utf-8"); source_file.write_bytes(source_bytes)
            command = ["stable_state_layout.py", "prepare", str(source_file), "--locale", "es", "--base-ref", "QA",
                "--editorial-return", "block_0000_0000:0", "--output", str(output)]
            with patch.object(layout, "published_family", return_value=(previous, family)), patch.object(sys, "argv", command), redirect_stdout(io.StringIO()):
                layout.main()
            self.assertEqual(expected, json.loads(output.read_bytes()))
            proposal = output.read_bytes()
            for destination in (output, source_file):
                with patch.object(layout, "published_family", return_value=(previous, family)), \
                        patch.object(sys, "argv", command[:-1] + [str(destination)]), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as failure:
                    layout.main()
                self.assertNotEqual(0, failure.exception.code)
            self.assertEqual(source_bytes, source_file.read_bytes()); self.assertEqual(proposal, output.read_bytes())
        for arguments in (["--editorial-return", "block_0000_0000:0", "--editorial-return", "block_0000_0000:0"],
                          ["--editorial-return", "block_0000_0000:-1"], ["--editorial-return", "block_0000_0000:true"]):
            with self.subTest(arguments=arguments), patch.object(sys, "argv", ["stable_state_layout.py", "prepare", "unused.json", *arguments]), \
                    redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as failure:
                layout.main()
            self.assertNotEqual(0, failure.exception.code)


if __name__ == "__main__":
    unittest.main(verbosity=2)

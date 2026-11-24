"""Source-bound reading overlays preserve the authentic exercise and history."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import code_scope_presentations as scopes
import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
LESSON = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
          "edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json")
BASE = "e338c6de2ac3479b44d67a95a57cb8bf734312d5"
BASE_BLOB = "e17824cda99abb7b863fc08ad60eb0d172e2b03b"
PRESENTED = "53578555e9a291dfe653cc0456fa535661df7ed3"
PRESENTED_BLOB = "32d94f9e25014791557e9b3ccb0c899ba4f6f0d7"
EXPLAINED = "ee940eb067c0ac8ff5a6217f2b6f79564b50e2cb"
EXPLAINED_BLOB = "0fd1793a34b0a49fcbb83d009afd24be54f56b38"
PUBLICATIONS = ((BASE, BASE_BLOB), (PRESENTED, PRESENTED_BLOB),
                (EXPLAINED, EXPLAINED_BLOB))
OWNER = "u3l4_alcance_codigo_en_recuadros"


def published_bytes(commit, expected_blob):
    raw = subprocess.check_output(["git", "show", commit + ":" + LESSON], cwd=ROOT)
    frame = b"blob " + str(len(raw)).encode() + b"\0" + raw
    if hashlib.sha1(frame).hexdigest() != expected_blob:
        raise AssertionError("The historical scope exercise is not the authenticated publication")
    return raw


class CodeScopePresentationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.publications = [json.loads(published_bytes(*publication))
                            for publication in PUBLICATIONS]
        cls.before, cls.presented, cls.explained = cls.publications
        cls.current = json.loads((ROOT / LESSON).read_text(encoding="utf-8"))

    @staticmethod
    def flatten(node):
        yield node
        for child in node["children"]:
            yield from CodeScopePresentationTest.flatten(child)

    def test_authentic_overlay_changes_no_exercise_or_history_field(self):
        # Prove the exact visual-only publication using its immutable bytes.
        # Later authored explanations may move this owner within its page;
        # they do not need to match an old document's presentation positions.
        restored = deepcopy(self.presented)
        metadata = restored.pop(scopes.ROOT_FIELD)
        self.assertEqual(restored, self.before)
        self.assertEqual(metadata["version"], 1)
        self.assertEqual(layout.validate(self.presented), layout.validate(self.before))
        layout.validate(self.current)
        # An explanation added after the overlay may itself be revised later.
        # Its real first publication is the base of that later editorial edge;
        # the final document alone must not invent an intermediate history.
        chain = (*self.publications, self.current)
        original = deepcopy(chain)
        for previous, current in zip(chain, chain[1:]):
            layout.validate_publication_transition(previous, current)
        self.assertEqual(chain, original)
        blocks = layout._blocks(self.current)
        resolved = scopes.validate(self.current, blocks=blocks)
        tree = resolved[(OWNER, 0)]
        self.assertEqual([(n["key"], n["text"], n["code"]) for n in self.flatten(tree)], [
            ("main", "Cuerpo de main", False),
            ("fichas", "const int fichas = 8;", True),
            ("if", "if (fichas >= 4)", True),
            ("bolsas", "const int bolsas = fichas / 4;", True),
            ("salida", "std::cout << bolsas;", True),
            ("return", "return 0;", True),
        ])
        self.assertEqual([n["key"] for n in tree["children"]], ["fichas", "if", "return"])
        self.assertEqual([n["key"] for n in tree["children"][1]["children"]], ["bolsas", "salida"])
        self.assertEqual(blocks[OWNER], layout._blocks(self.before)[OWNER])
        entry = next(x for x in self.current["stateLayout"]["additions"] if x["id"] == OWNER)
        self.assertEqual(entry["storage"], {"page": 7, "block": 30})

    def test_publication_bytes_cannot_be_changed_or_substituted(self):
        payloads = [published_bytes(*publication) for publication in PUBLICATIONS]
        for index, (commit, expected_blob) in enumerate(PUBLICATIONS):
            for payload in (payloads[index] + b"\n", payloads[(index + 1) % len(payloads)]):
                with self.subTest(commit=commit), patch.object(
                        subprocess, "check_output", return_value=payload):
                    with self.assertRaisesRegex(AssertionError, "authenticated publication"):
                        published_bytes(commit, expected_blob)

    def test_revised_new_explanations_require_their_actual_first_publication(self):
        earlier = layout._blocks(self.presented)
        introduced = layout._blocks(self.explained)
        revised = layout._blocks(self.current)
        owners = {entry["id"] for entry in self.current["stateLayout"]["editorial"]}
        later_edits = (introduced.keys() - earlier.keys()) & owners
        self.assertTrue(later_edits)
        self.assertTrue(any(introduced[owner] != revised[owner] for owner in later_edits))
        # The strict guard still rejects a jump that omits the real addition.
        with self.assertRaisesRegex(layout.LayoutError,
                                    "New activities cannot claim historical editorial provenance"):
            layout.validate_publication_transition(self.presented, self.current)

    def test_later_editorial_publication_cannot_change_the_scope_answer_or_owner(self):
        for mutation in ("answer", "owner", "retired-address", "original-address",
                         "source", "presentation", "retirement"):
            candidate = deepcopy(self.current)
            if mutation == "answer":
                layout._blocks(candidate)[OWNER]["responses"][0]["validator"]["expected"] = ["antes"]
            elif mutation == "owner":
                self.entry(candidate)["block"] = "u3l4_alcance_editor"
            elif mutation in {"retired-address", "original-address"}:
                addition = next(x for x in candidate["stateLayout"]["additions"] if x["id"] == OWNER)
                addition["storage"] = ({"page": 7, "block": 29} if mutation == "retired-address"
                                       else {"page": 0, "block": 0})
            elif mutation == "source":
                layout._blocks(candidate)[OWNER]["representations"][0]["points"][9]["labels"][0] = "const int fichas = 9;"
            elif mutation == "presentation":
                self.entry(candidate)["root"]["children"][0]["sourceSha256"] = "0" * 64
            else:
                candidate["stateLayout"]["retirementHistory"][0]["pages"][0]["blocks"][0]["text"] += " Alterado."
            with self.subTest(mutation=mutation), self.assertRaises(layout.LayoutError):
                layout.validate_publication_transition(self.explained, candidate)

    def assert_rejected(self, mutation):
        candidate = deepcopy(self.current)
        mutation(candidate)
        with self.assertRaises(layout.LayoutError):
            layout.validate(candidate)

    @staticmethod
    def entry(document):
        return document[scopes.ROOT_FIELD]["entries"][0]

    def test_stale_source_proof_and_borrowed_owners_are_rejected(self):
        def stale(document):
            self.entry(document)["root"]["children"][0]["sourceSha256"] = "0" * 64
        self.assert_rejected(stale)
        self.assert_rejected(lambda d: self.entry(d).__setitem__("block", "u3l4_alcance_editor"))
        self.assert_rejected(lambda d: self.entry(d).__setitem__("block", "unrelated_owner"))
        self.assert_rejected(lambda d: self.entry(d).__setitem__("representation", 1))

    def test_paths_cannot_leave_reading_representation_or_change_source_type(self):
        for pointer in ("/../responses/0/validator/expected/0", "/points/09/labels/0",
                        "/points/9/x", "/points/9/labels/9", "/points/~2/labels/0"):
            with self.subTest(pointer=pointer):
                self.assert_rejected(lambda d: self.entry(d)["root"]["children"][0].__setitem__("source", pointer))

    def test_duplicate_nodes_sources_and_entries_are_rejected(self):
        self.assert_rejected(lambda d: self.entry(d)["root"]["children"][-1].__setitem__("id", "fichas"))
        def repeat_source(document):
            children = self.entry(document)["root"]["children"]
            children[-1] = dict(children[0], id="different_key")
        self.assert_rejected(repeat_source)
        self.assert_rejected(lambda d: d[scopes.ROOT_FIELD]["entries"].append(deepcopy(self.entry(d))))

    def test_empty_groups_unknown_fields_and_untyped_leaves_are_rejected(self):
        self.assert_rejected(lambda d: self.entry(d)["root"].__setitem__("children", []))
        self.assert_rejected(lambda d: self.entry(d).__setitem__("language", "auto"))
        self.assert_rejected(lambda d: self.entry(d).__setitem__("expected", "before_return"))
        self.assert_rejected(lambda d: self.entry(d)["root"]["children"].append({"id": "plain", "title": "Unbound text"}))
        self.assert_rejected(lambda d: d[scopes.ROOT_FIELD].__setitem__("version", True))
        self.assert_rejected(lambda d: self.entry(d).__setitem__("representation", False))

    def test_projection_cannot_silently_rewrite_an_existing_instruction(self):
        candidate = deepcopy(self.current)
        blocks = layout._blocks(candidate)
        blocks[OWNER]["representations"][0]["points"][9]["labels"][0] = "const int fichas = 9;"
        with self.assertRaisesRegex(ValueError, "fingerprint"):
            scopes.validate(candidate, blocks=blocks)

    def test_physical_source_lines_remain_complete(self):
        for text in ("const int fichas = 8;\nreturn 0;", " ", "x\0y", "x\u2028y"):
            with self.subTest(text=repr(text)):
                candidate = deepcopy(self.current)
                blocks = layout._blocks(candidate)
                blocks[OWNER]["representations"][0]["points"][9]["labels"][0] = text
                self.entry(candidate)["root"]["children"][0]["sourceSha256"] = hashlib.sha256(text.encode()).hexdigest()
                with self.assertRaises(ValueError):
                    scopes.validate(candidate, blocks=blocks)

    def test_geometry_coordinates_are_not_code_or_answer_migrations(self):
        candidate = deepcopy(self.current)
        blocks = layout._blocks(candidate)
        blocks[OWNER]["representations"][0]["points"][9]["x"] = 765.25
        self.assertEqual(scopes.validate(candidate, blocks=blocks),
                         scopes.validate(self.current, blocks=layout._blocks(self.current)))
        # The presentation guard is not an answer-equivalence proof. The
        # mathematical block and its ordinary state validation stay separate.
        self.assertNotEqual(blocks[OWNER], layout._blocks(self.current)[OWNER])


if __name__ == "__main__":
    unittest.main()

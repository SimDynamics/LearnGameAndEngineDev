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
BEFORE_LATER_EDITORIAL = "39ba3cb196e196bac99c2d891b488f765679ad6d"
BEFORE_LATER_EDITORIAL_BLOB = "820d056b933573cb580554aa25031a3a2154556a"
OWNER = "u3l4_alcance_codigo_en_recuadros"
PROMPT_REVIEWED = "aae31e3d0ff202d700d3f3dd5ba27854eebe088d"
PROMPT_REVIEWED_BLOB = "420f3650e24d417cfac73f6ebde1678f353d4b3f"
# These are complete publications from the actual lesson history, not
# snapshots reconstructed from the final document's editorial hashes.
# The last four additions/reviews must not be collapsed into a single edge.
PUBLICATIONS = ((BASE, BASE_BLOB), (PRESENTED, PRESENTED_BLOB),
                (EXPLAINED, EXPLAINED_BLOB),
                (BEFORE_LATER_EDITORIAL, BEFORE_LATER_EDITORIAL_BLOB),
                (PROMPT_REVIEWED, PROMPT_REVIEWED_BLOB),
                ("652cdf7dcea14e27d98a2f214a42da0621353481",
                 "acdd5c2109c66a2cc4909695df49293a796adbb9"),
                ("08887bd1495ac745ec77b361b9dd9ac4670c23c5",
                 "dac3b2ff3d0841c588548415cebbd45e8fed3d18"),
                ("843ecc1cadca8dbaa102f30ef00a01278b1d9436",
                 "ab1c0099d2265a461692a72f7305e33a3560a3a1"),
                ("229c6e2b9d77b889f842c59a4ce825c2e0194dc4",
                 "776a45283a56ec4c00122c8c6ad0708380528808"))
CHOICE_TEXT_EDITS = {
    "/responses/0/options/0/label": ("En main, antes de la declaración de fichas", ("main", "fichas")),
    "/responses/0/options/1/label": ("Después del bloque del if, justo antes de return 0;", ("if", "return 0;")),
    "/responses/0/options/2/label": ("Dentro del if, inmediatamente después de la declaración de bolsas", ("if", "bolsas")),
    "/feedback/correct": ("La posición antes_de_return conserva la respuesta original.", ("antes_de_return",)),
    "/feedback/incorrect": ("Revisa el alcance de fichas antes de volver a elegir.", ("fichas",)),
}


def published_bytes(commit, expected_blob):
    raw = subprocess.check_output(["git", "show", commit + ":" + LESSON], cwd=ROOT)
    frame = b"blob " + str(len(raw)).encode() + b"\0" + raw
    if hashlib.sha1(frame).hexdigest() != expected_blob:
        raise AssertionError("The historical scope exercise is not the authenticated publication")
    return raw


def later_publication_bytes():
    """Read actual descendant lesson publications before the working tree.

    Do not synthesize an intermediate block or silently accept a skipped
    addition. Git's path history can contain merges; require each selected
    publication to descend from the preceding one. A divergent chain needs
    an explicitly authenticated path, not a timestamp-based ordering.
    """
    previous, previous_blob = PUBLICATIONS[-1]
    subprocess.run(["git", "merge-base", "--is-ancestor", previous, "HEAD"],
                   cwd=ROOT, check=True)
    commits = subprocess.check_output(
        ["git", "log", "--full-history", "--ancestry-path", "--topo-order",
         "--reverse", "--format=%H", previous + "..HEAD", "--", LESSON],
        cwd=ROOT, text=True).splitlines()
    for commit in commits:
        blob = subprocess.check_output(
            ["git", "rev-parse", commit + ":" + LESSON],
            cwd=ROOT, text=True).strip()
        if blob == previous_blob:
            continue
        subprocess.run(["git", "merge-base", "--is-ancestor", previous, commit],
                       cwd=ROOT, check=True)
        yield published_bytes(commit, blob)
        previous, previous_blob = commit, blob


class CodeScopePresentationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.publications = [json.loads(published_bytes(*publication))
                            for publication in PUBLICATIONS]
        cls.before, cls.presented, cls.explained = cls.publications[:3]
        cls.current = json.loads((ROOT / LESSON).read_text(encoding="utf-8"))
        cls.prompt_reviewed = cls.publications[4]
        cls.later_publications = [json.loads(raw) for raw in later_publication_bytes()]

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
        chain = (*self.publications, *self.later_publications, self.current)
        original = deepcopy(chain)
        for index, (previous, current) in enumerate(zip(chain, chain[1:]), start=1):
            with self.subTest(publication_edge=index):
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
        # Visible prompt/option prose can evolve through authenticated text
        # archives. Evaluation, source and the saved address remain original.
        self.assertEqual(layout._historical_math_text_contract(
            self.current, OWNER, blocks[OWNER]), layout._blocks(self.before)[OWNER])
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

    def test_nivel_revision_needs_the_actual_intermediate_publication(self):
        # Reproduce Work's failure on the fixed, authentic endpoints. The
        # newly added exercise has no editorial predecessor in 39ba3cb1.
        with self.assertRaisesRegex(layout.LayoutError,
                                    "New activities cannot claim historical editorial provenance"):
            layout.validate_publication_transition(self.publications[3], self.publications[-1])
        # All eight reviewed transitions remain independently checked above.

    def test_later_editorial_publication_cannot_change_the_scope_answer_or_owner(self):
        # Use the actual current predecessor so the negatives cannot pass
        # merely because an unrelated legitimate addition was skipped.
        layout.validate_publication_transition(self.current, self.current)
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
                layout.validate_publication_transition(self.current, candidate)

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

    @staticmethod
    def choice_text_edits():
        return {path: {"text": text, "spans": [
                    layout.text_semantics.select(text, token, "cpp", reading="technical")
                    for token in tokens]}
                for path, (text, tokens) in CHOICE_TEXT_EDITS.items()}

    def choice_text_candidate(self):
        return layout.revise_math_text_editorial(
            self.prompt_reviewed, OWNER, self.choice_text_edits())

    def test_reviewed_prompt_keeps_the_authentic_archived_scope_exercise(self):
        edited = self.prompt_reviewed
        original = layout._blocks(self.before)[OWNER]
        current = layout._blocks(edited)[OWNER]
        self.assertNotEqual(original["prompt"], current["prompt"])
        self.assertEqual(original, layout._historical_math_text_contract(edited, OWNER, current))
        previous = self.publications[3]
        layout.validate_publication_transition(previous, edited)
        self.assertEqual([item["field"] for item in edited["stateLayout"]["mathTextRevisions"]], ["/prompt"])

    def test_single_choice_labels_and_feedback_append_to_the_existing_prompt_archive(self):
        previous = self.prompt_reviewed
        snapshot = deepcopy(previous)
        current = self.choice_text_candidate()
        self.assertEqual(previous, snapshot)
        old_archives = previous["stateLayout"]["mathTextRevisions"]
        archives = current["stateLayout"]["mathTextRevisions"]
        self.assertEqual(archives[:len(old_archives)], old_archives)
        self.assertEqual(len(archives), len(old_archives) + 5)
        self.assertEqual({entry["rawJson"] for entry in archives}, {old_archives[0]["rawJson"]})
        self.assertEqual(len({(entry["id"], entry["field"]) for entry in archives}), len(archives))
        self.assertEqual(layout.validate(previous), layout.validate(current))
        self.assertEqual(layout.lineage(previous), layout.lineage(current))
        self.assertEqual(layout.original_projection(previous), layout.original_projection(current))
        before, after = layout._blocks(previous)[OWNER], layout._blocks(current)[OWNER]
        self.assertEqual(before["prompt"], after["prompt"])
        self.assertEqual([option["id"] for option in before["responses"][0]["options"]],
                         [option["id"] for option in after["responses"][0]["options"]])
        self.assertEqual(after["responses"][0]["validator"], {"type": "choice", "expected": ["antes_de_return"]})
        for path, edit in self.choice_text_edits().items():
            self.assertEqual(layout._math_text_field(after, path), edit["text"])
        self.assertEqual(layout._math_prompt_semantic_entry(previous, OWNER),
                         layout._math_prompt_semantic_entry(current, OWNER))
        layout.validate_transition(previous, current)
        aligned = layout.align_for_previous_validation(previous, current)
        self.assertEqual(layout._blocks(aligned)[OWNER], before)
        self.assertEqual(aligned.get("textSemantics"), previous.get("textSemantics"))

    def test_single_choice_fields_can_be_revised_in_separate_publications(self):
        previous = self.prompt_reviewed
        current = previous
        for path, edit in self.choice_text_edits().items():
            revised = layout.revise_math_text_editorial(current, OWNER, {path: edit})
            layout.validate_transition(current, revised)
            current = revised
        layout.validate_transition(previous, current)
        self.assertEqual(layout._blocks(current)[OWNER],
                         layout._blocks(self.choice_text_candidate())[OWNER])
        self.assertEqual(layout.original_projection(previous), layout.original_projection(current))

    def test_single_choice_text_cannot_change_ids_answers_source_or_undeclared_text(self):
        valid = self.choice_text_candidate()
        for variant in ("option-id", "response-id", "expected", "validator", "option-order",
                        "option-count", "source", "schema", "root-order", "prompt"):
            candidate = deepcopy(valid)
            block = layout._blocks(candidate)[OWNER]
            response = block["responses"][0]
            if variant == "option-id":
                response["options"][1]["id"] = "otra_posicion"
            elif variant == "response-id":
                response["id"] = "otra_respuesta"
            elif variant == "expected":
                response["validator"]["expected"] = ["antes"]
            elif variant == "validator":
                response["validator"]["type"] = "otro"
            elif variant == "option-order":
                response["options"].reverse()
            elif variant == "option-count":
                response["options"].append({"id": "cuarta", "label": "Otra posición."})
            elif variant == "source":
                block["representations"][0]["points"][9]["labels"][0] = "const int fichas = 9;"
            elif variant == "schema":
                block["schemaVersion"] = 1.0
            elif variant == "root-order":
                reordered = dict(reversed(list(block.items())))
                block.clear()
                block.update(reordered)
            else:
                block["prompt"] += " Texto sin descriptor actualizado."
            candidate["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(block)
            with self.subTest(variant=variant), self.assertRaises(layout.LayoutError):
                layout.validate_transition(self.prompt_reviewed, candidate)

    def test_single_choice_field_archives_cannot_be_forged_or_reassigned(self):
        valid = self.choice_text_candidate()
        for variant in ("duplicate", "path", "foreign-raw", "raw-whitespace", "descriptor",
                        "missing-marker", "rewritten-prompt-archive"):
            candidate = deepcopy(valid)
            archives = candidate["stateLayout"]["mathTextRevisions"]
            if variant == "duplicate":
                archives.append(deepcopy(archives[-1]))
            elif variant == "path":
                archives[1]["field"] = "/responses/0/options/00/label"
            elif variant == "foreign-raw":
                archives[1]["rawJson"] = layout._math_ordered_json(layout._blocks(self.prompt_reviewed)[OWNER])
            elif variant == "raw-whitespace":
                archives[1]["rawJson"] += " "
            elif variant == "descriptor":
                archives[1]["semanticBefore"] = deepcopy(archives[0]["semanticBefore"])
            elif variant == "missing-marker":
                candidate["textSemantics"]["fields"] = [
                    field for field in candidate["textSemantics"]["fields"]
                    if not (field["block"] == OWNER and field["path"] == archives[1]["field"])]
            else:
                archives[0]["semanticBefore"] = None
            with self.subTest(variant=variant), self.assertRaises(layout.LayoutError):
                layout.validate_transition(self.prompt_reviewed, candidate)

    def test_single_choice_editorial_return_retains_the_original_selection_contract(self):
        previous = self.prompt_reviewed
        current = self.choice_text_candidate()
        old = layout._blocks(previous)[OWNER]
        edits = {}
        for path in CHOICE_TEXT_EDITS:
            marker = layout._math_prompt_semantic_entry(previous, OWNER, path)
            edits[path] = {"text": layout._math_text_field(old, path),
                           "spans": deepcopy(marker["spans"]) if marker else []}
        edges = [edge for edge in current["stateLayout"]["editorial"] if edge["id"] == OWNER]
        nodes = [edges[0]["beforeSha256"]] + [edge["afterSha256"] for edge in edges]
        returned = layout.revise_math_text_editorial(
            current, OWNER, edits, return_to=nodes.index(layout.fingerprint(old)))
        self.assertEqual(layout._blocks(returned)[OWNER], old)
        self.assertEqual(returned["stateLayout"]["mathTextRevisions"],
                         current["stateLayout"]["mathTextRevisions"])
        self.assertEqual(layout.original_projection(returned), layout.original_projection(previous))
        layout.validate_transition(previous, returned)


if __name__ == "__main__":
    unittest.main()

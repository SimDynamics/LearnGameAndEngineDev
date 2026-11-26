"""Source-bound reading overlays preserve the authentic exercise and history."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import unittest
from typing import NamedTuple
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
# Recorded rejected publication, not an accepted predecessor or migration.
# Only its authentic exact rollback may return the chain to the last valid map.
REJECTED_STORAGE_PUBLICATION = (
    "2c3590c809d13cab02560335f6b2353d5655bbd2",
    "0ed1a59a3e5dee8bb36a9f99b40b567873b9959b",
    "33b497bbe50a57090a7ae8ab3d83bc0f208a1357",
    "a635ae9c2d3f138702af8a06026cb6b7c5f86217",
)
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


def _has_publication_path(before, after, edges):
    """Only follow real earlier blob transitions inherited from Git parents."""
    pending, seen = [before], set()
    while pending:
        node = pending.pop()
        if node == after:
            return True
        if node in seen:
            continue
        seen.add(node)
        pending.extend(target for source, target in edges if source == node)
    return False


def publication_edges(rows, blobs, base, head):
    """Return actual parent/child transitions, not a linearized merge log.

    A merge carrying one parent's unchanged blob may also have a stale
    parent. Skip that redundant edge only if the intermediate transitions
    are inherited from the actual parents and will themselves be checked.
    A new merge resolution, a divergent publication or a genuine reversion
    still requires its own direct transition. Equal blobs alone do not
    permit skipping a changed single-parent commit.
    """
    proofs = {base: frozenset()}
    revisions = {row[0] for row in rows}
    checks = []
    for commit, *parents in rows:
        direct = [parent for parent in parents if parent in proofs]
        if not direct:
            continue
        if any(parent in revisions and parent not in proofs for parent in parents):
            raise AssertionError("A divergent history predates the authenticated publication")
        inherited = frozenset().union(*(proofs[parent] for parent in direct))
        current = blobs[commit]
        carried = any(blobs[parent] == current for parent in direct)
        added = set()
        for parent in direct:
            before = blobs[parent]
            edge = (before, current)
            if before == current or edge in added:
                continue
            if carried and _has_publication_path(before, current, inherited):
                continue
            checks.append((parent, commit))
            added.add(edge)
        proofs[commit] = inherited | added
    if head not in proofs:
        raise AssertionError("The current lesson is not descended from its authenticated publication")
    return checks


class PublicationEdge(NamedTuple):
    parent: str
    commit: str
    before_blob: str
    after_blob: str
    before: dict
    after: dict


def validate_later_publications(edges, head_blob):
    """Preserve every rejection; require an exact published recovery.

    This is a history audit, not a compatibility rule for student state.
    Never accept the rejected map as a valid predecessor or translate its
    storage addresses. A current or unrepaired rejected publication fails.
    The only reviewed incident is bound to its real commit, parent and blobs.
    """
    parent, rejected, good_blob, bad_blob = REJECTED_STORAGE_PUBLICATION
    original = None
    restored = False
    for edge in edges:
        try:
            if edge.commit == rejected:
                if (original is not None or
                        edge[:4] != REJECTED_STORAGE_PUBLICATION):
                    raise layout.LayoutError("Rejected publication provenance changed")
                layout.validate(edge.before)
                # The entire lesson apart from the additions registry must
                # be unchanged. Do not turn unrelated failures into rollback
                # exceptions or allow a weaker transition validator.
                repaired = deepcopy(edge.after)
                repaired["stateLayout"]["additions"] = deepcopy(
                    edge.before["stateLayout"]["additions"])
                if repaired != edge.before:
                    raise layout.LayoutError("Rejected publication changed more than storage")
                old = edge.before["stateLayout"]["additions"]
                new = edge.after["stateLayout"]["additions"]
                by_id = {entry["id"]: entry for entry in new}
                if (len(old) != 106 or len(new) != 106 or len(by_id) != 106
                        or set(by_id) != {entry["id"] for entry in old}
                        or sum(entry["storage"] != by_id[entry["id"]]["storage"]
                               for entry in old) != 24):
                    raise layout.LayoutError("Rejected storage incident no longer matches its evidence")
                for entry in old:
                    other = dict(by_id[entry["id"]], storage=entry["storage"])
                    if other != entry:
                        raise layout.LayoutError("Rejected publication changed an activity contract")
                try:
                    layout.validate_publication_transition(edge.before, edge.after)
                except layout.LayoutError as error:
                    if "Activity additions are append-only" not in str(error):
                        raise
                else:
                    raise AssertionError("The rejected storage publication was incorrectly accepted")
                original = edge.before
            elif edge.before_blob == bad_blob:
                # The reverse of a rejected reassignment is not a normal
                # migration. Require the exact original blob, not merely the
                # same IDs or a new reordering that passes self-validation.
                if (original is None or edge.after_blob != good_blob
                        or edge.after != original):
                    raise layout.LayoutError("Rejected storage publication requires its exact original map")
                layout.validate_publication_transition(original, edge.after)
                restored = True
            else:
                if edge.after_blob == bad_blob:
                    raise layout.LayoutError("A new publication reused the rejected storage map")
                layout.validate_publication_transition(edge.before, edge.after)
        except (layout.LayoutError, AssertionError) as error:
            error.add_note("U3L4 publication edge: " + edge.parent + " -> " + edge.commit)
            raise
    if original is not None and (not restored or head_blob == bad_blob):
        raise layout.LayoutError(
            "Rejected U3L4 storage publication remains unrepaired; restore the exact preceding lesson")


def later_publication_documents():
    """Read authentic Git parent edges and the published HEAD independently."""
    base, expected = PUBLICATIONS[-1]
    head = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    subprocess.run(["git", "merge-base", "--is-ancestor", base, head],
                   cwd=ROOT, check=True)
    # No path simplification: the real parent graph is needed to distinguish
    # reciprocal merges from actual changes to the lesson. A path-filtered
    # full-history log can interleave old blobs after newer publications.
    rows = [line.split() for line in subprocess.check_output(
        ["git", "rev-list", "--topo-order", "--reverse", "--parents",
         base + ".." + head], cwd=ROOT, text=True).splitlines()]
    revisions = [base, *(row[0] for row in rows)]
    # Query all immutable blob identities in one process, without using the
    # mutable working tree or exceeding the Windows command-line limit.
    objects = subprocess.check_output(
        ["git", "cat-file", "--batch-check=%(objectname) %(objecttype)"],
        input="".join(commit + ":" + LESSON + "\n" for commit in revisions),
        cwd=ROOT, text=True).splitlines()
    if len(objects) != len(revisions):
        raise AssertionError("Incomplete Git publication identity response")
    blobs = {}
    for commit, line in zip(revisions, objects):
        fields = line.split()
        if (len(fields) != 2 or fields[1] != "blob" or len(fields[0]) != 40
                or any(char not in "0123456789abcdef" for char in fields[0])):
            raise AssertionError("Missing or invalid historical lesson blob: " + commit)
        blobs[commit] = fields[0]
    if blobs[base] != expected:
        raise AssertionError("The pinned lesson origin has changed")
    cache = {}

    def read(commit):
        blob = blobs[commit]
        if blob not in cache:
            cache[blob] = json.loads(published_bytes(commit, blob))
        return cache[blob]

    edges = publication_edges(rows, blobs, base, head)
    return ([PublicationEdge(parent, commit, blobs[parent], blobs[commit],
                             read(parent), read(commit)) for parent, commit in edges],
            blobs[head], read(head))


class CodeScopePresentationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.publications = [json.loads(published_bytes(*publication))
                            for publication in PUBLICATIONS]
        cls.before, cls.presented, cls.explained = cls.publications[:3]
        cls.current = json.loads((ROOT / LESSON).read_text(encoding="utf-8"))
        cls.prompt_reviewed = cls.publications[4]
        cls.later_edges, cls.published_head_blob, cls.published_head = later_publication_documents()

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
        edges = list(zip(self.publications, self.publications[1:]))
        original = deepcopy((edges, self.later_edges, self.published_head, self.current))
        for index, (previous, current) in enumerate(edges, start=1):
            with self.subTest(publication_edge=index):
                layout.validate_publication_transition(previous, current)
        validate_later_publications(self.later_edges, self.published_head_blob)
        layout.validate_publication_transition(self.published_head, self.current)
        self.assertEqual((edges, self.later_edges, self.published_head, self.current), original)
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

    def test_reciprocal_merges_keep_only_real_publication_edges(self):
        # Topology-only unit regression for Work/Codex's reciprocal merges.
        # The real lesson bytes are independently checked by the history test.
        rows = [("technical", "base"), ("one", "base"),
                ("work-merge", "one", "technical"), ("two", "work-merge"),
                ("codex-merge", "technical", "one"),
                ("codex-next", "codex-merge", "two"),
                ("work-next", "two", "codex-merge"),
                ("joined", "work-next", "codex-next"), ("three", "joined")]
        blobs = dict(zip(("base", *(row[0] for row in rows)),
                         ("A", "A", "B", "B", "C", "B", "C", "C", "C", "D")))
        self.assertEqual(publication_edges(rows, blobs, "base", "three"),
                         [("base", "one"), ("work-merge", "two"), ("joined", "three")])
        # Moving the independent earlier merge ahead of the newer publication
        # changes the linear log order, but not the actual provenance.
        rows[3], rows[4] = rows[4], rows[3]
        self.assertEqual(publication_edges(rows, blobs, "base", "three"),
                         [("base", "one"), ("work-merge", "two"), ("joined", "three")])

    def test_divergent_merge_and_reversion_still_require_validation(self):
        rows = [("left", "base"), ("right", "base"),
                ("merge", "left", "right"), ("revert", "merge")]
        blobs = {"base": "A", "left": "B", "right": "C",
                 "merge": "B", "revert": "A"}
        self.assertEqual(publication_edges(rows, blobs, "base", "revert"),
                         [("base", "left"), ("base", "right"),
                          ("right", "merge"), ("merge", "revert")])
        blobs["merge"] = "D"
        self.assertEqual(publication_edges(rows, blobs, "base", "revert"),
                         [("base", "left"), ("base", "right"),
                          ("left", "merge"), ("right", "merge"), ("merge", "revert")])

    def test_history_cannot_use_an_unanchored_side_branch(self):
        rows = [("side", "older"), ("merge", "base", "side")]
        with self.assertRaisesRegex(AssertionError, "predates"):
            publication_edges(rows, {"base": "A", "side": "B", "merge": "B"},
                              "base", "merge")

    def test_rejected_storage_publication_requires_real_exact_recovery(self):
        parent, rejected, good_blob, bad_blob = REJECTED_STORAGE_PUBLICATION
        good = json.loads(published_bytes(parent, good_blob))
        bad = json.loads(published_bytes(rejected, bad_blob))
        incident = PublicationEdge(parent, rejected, good_blob, bad_blob, good, bad)
        # Verify the actual rejected publication; no fabricated prior map.
        with self.assertRaisesRegex(layout.LayoutError, "remains unrepaired"):
            validate_later_publications([incident], bad_blob)
        # In-memory test of the required recovery, not a claim that Work
        # already published it. Production history must supply real Git edges.
        recovery = PublicationEdge(rejected, "qa-restoration", bad_blob,
                                   good_blob, bad, deepcopy(good))
        validate_later_publications([incident, recovery], good_blob)
        with self.assertRaisesRegex(layout.LayoutError, "remains unrepaired"):
            validate_later_publications([incident, recovery], bad_blob)
        forged = deepcopy(good)
        forged["stateLayout"]["additions"][82]["storage"] = {"page": 7, "block": 98}
        with self.assertRaisesRegex(layout.LayoutError, "exact original map"):
            validate_later_publications([incident, recovery._replace(after=forged)], good_blob)
        with self.assertRaisesRegex(layout.LayoutError, "exact original map"):
            validate_later_publications([incident, recovery._replace(after_blob="f" * 40)], good_blob)
        with self.assertRaisesRegex(layout.LayoutError, "reused the rejected storage map"):
            validate_later_publications([incident, recovery,
                incident._replace(commit="qa-new-reassignment")], bad_blob)
        with self.assertRaisesRegex(layout.LayoutError, "provenance changed"):
            validate_later_publications([incident._replace(parent="qa-other-parent")], bad_blob)

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
        # The active endpoint now also includes a math-text archive. It must
        # first be rejected for lacking that archive's already published owner;
        # the independent nivel test below still verifies the editorial guard.
        with self.assertRaisesRegex(layout.LayoutError,
                                    "A math text archive must retain an already published active v2 owner"):
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

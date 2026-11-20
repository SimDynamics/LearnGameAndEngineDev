"""U4L7 source formatting follows authenticated owners across later publications.

The format and retirement pairs are complete Git publications with independent
byte pins. Each transition uses its actual published parent. The current lesson
is checked for the six format owners and preserved provenance, not treated as
a single operation from an old publication. All proposed edits below are isolated
copies passed through the real authoring APIs; no predecessor is reconstructed
from the current curriculum.
"""
from __future__ import annotations

from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

import code_format_compat as source_format
import stable_state_layout as state


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "proyecto_assetcheck/lesson_disenar_probar_y_presentar_variante_propia.json"
)
PATH = ROOT / RELATIVE
LESSON = "programming_practice_tools__proyecto_assetcheck__disenar_probar_y_presentar_variante_propia"
PUBLICATIONS = {
    "before": (
        "da11ba0077b74f1a4dcf547d9f77afda31cc7503",
        "fa1d51a397dceb9030b954e12f3198b4db316b03b8ac881d63d62a8458ed800a",
    ),
    "formatted": (
        "2e814bd22dd3cc88a5b79ef93534ea75c64437cb",
        "dc8c263a0d8f3ad5810e9b3447406f4b07a02d143bd31bc40e2d0a2e34205d79",
    ),
    "retirement_parent": (
        "80e4f711e5ee30fc45093d79725b37e40fe60d34",
        "c2f47d0b21244feaa54c335849369dd0029ef4f152448798d9ff4698616cf671",
    ),
    "retirement": (
        "e5b20912931cd67eac06619abeaa7a9cdc421704",
        "21cbf34c7a14ae158711a368f19261de2c119be41fdfdc9fa1521c6d9376721e",
    ),
}
OWNERS = (
    "block_0005_0002", "block_0006_0003", "block_0007_0002",
    "block_0008_0002", "block_0009_0002", "block_0010_0002",
)
COMPILER = "block_0006_0003"
CMAKE = "block_0008_0002"


def verify_bytes(payload: bytes, publication: str) -> bytes:
    if hashlib.sha256(payload).hexdigest() != PUBLICATIONS[publication][1]:
        raise ValueError("U4L7 source differs from its pinned complete publication")
    return payload


@lru_cache(maxsize=None)
def historical_bytes(publication: str) -> bytes:
    commit, _ = PUBLICATIONS[publication]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{commit}:{RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE), publication)


class SourceFormatU4L7(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = json.loads(historical_bytes("before"))
        cls.formatted = json.loads(historical_bytes("formatted"))
        cls.retirement_parent = json.loads(historical_bytes("retirement_parent"))
        cls.retirement = json.loads(historical_bytes("retirement"))
        cls.doc = json.loads(PATH.read_bytes())

    def owners(self, document):
        """Resolve authored identities without silently truncating either list."""
        pages = document["pages"]
        current = document["stateLayout"]["current"]
        self.assertEqual(len(pages), len(current))
        blocks, positions = {}, {}
        page_ids = set()
        for p, (page, record) in enumerate(zip(pages, current)):
            self.assertNotIn(record["id"], page_ids)
            page_ids.add(record["id"])
            self.assertEqual(len(page["blocks"]), len(record["blocks"]))
            for b, (block, owner) in enumerate(zip(page["blocks"], record["blocks"])):
                self.assertNotIn(owner, blocks)
                blocks[owner], positions[owner] = block, (p, b)
        return blocks, positions

    def assert_sources_and_history(self, document):
        state.validate(document)
        before, _ = self.owners(self.before)
        published, _ = self.owners(self.formatted)
        current, _ = self.owners(document)
        entries = document["stateLayout"]["sourceFormatRevisions"]
        archives = {entry["id"]: entry["block"] for entry in entries}
        self.assertEqual(len(entries), len(archives))
        self.assertTrue(set(OWNERS) <= archives.keys())
        for owner in OWNERS:
            with self.subTest(owner=owner):
                self.assertEqual(archives[owner], before[owner])
                self.assertTrue(source_format.require_source_format(before[owner], published[owner]))
                source_format.require_source_lexical(before[owner], current[owner])
                protected = source_format.source_projection(current[owner], before[owner])
                self.assertEqual(state.state_fingerprint(protected), state.state_fingerprint(before[owner]))
                edge = {"id": owner,
                        "beforeSha256": state.fingerprint(before[owner]),
                        "afterSha256": state.fingerprint(published[owner])}
                published_edges = [proof for proof in self.formatted["stateLayout"]["editorial"]
                                   if proof["id"] == owner]
                current_edges = [proof for proof in document["stateLayout"]["editorial"]
                                 if proof["id"] == owner]
                self.assertIn(edge, published_edges)
                self.assertEqual(current_edges[:len(published_edges)], published_edges)

    def publish(self, previous, proposal):
        return state.prepare_v2(proposal, "es", {"es": previous}, previous)

    def test_complete_pinned_publications_and_real_transitions(self):
        for document in (self.before, self.formatted, self.retirement_parent,
                         self.retirement, self.doc):
            self.assertEqual(document["id"], LESSON)
            state.validate(document)
        # These are actual Git parent/publication pairs. Retirement captured
        # the complete later presentation, after hundreds of authored additions.
        state.validate_transition(self.before, self.formatted)
        state.validate_transition(self.retirement_parent, self.retirement)
        with self.assertRaisesRegex(state.LayoutError,
                "Retirement reading snapshot does not match the complete previous published presentation"):
            state.validate_transition(self.formatted, self.retirement)
        snapshots = self.retirement["stateLayout"]["retirementHistory"]
        self.assertEqual(len(snapshots), 1)
        snapshot = snapshots[0]
        self.assertEqual(snapshot["pages"], self.retirement_parent["pages"])
        self.assertEqual(snapshot["current"], self.retirement_parent["stateLayout"]["current"])
        self.assertEqual(snapshot["textSemantics"], self.retirement_parent["textSemantics"])
        self.assertEqual(snapshot["editorialPrefix"]["count"],
                         len(self.retirement_parent["stateLayout"]["editorial"]))
        self.assertEqual(self.doc["stateLayout"].get("retirementHistory", [])[:len(snapshots)], snapshots)
        for document in (self.retirement_parent, self.retirement, self.doc):
            self.assert_sources_and_history(document)

    def test_independent_byte_pins_reject_modified_or_substituted_sources(self):
        for publication in PUBLICATIONS:
            payload = historical_bytes(publication)
            with self.subTest(publication=publication):
                self.assertEqual(verify_bytes(payload, publication), payload)
                with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                    verify_bytes(payload + b"\n", publication)
                other = "formatted" if publication == "before" else "before"
                with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                    verify_bytes(historical_bytes(other), publication)

    def test_current_reordering_and_additions_preserve_owner_storage(self):
        _, published_positions = self.owners(self.formatted)
        _, current_positions = self.owners(self.doc)
        published_storage = state.validate(self.formatted)
        current_storage = state.validate(self.doc)
        for owner in OWNERS:
            with self.subTest(owner=owner):
                self.assertEqual(published_storage[published_positions[owner]],
                                 current_storage[current_positions[owner]])
        for field in ("origin", "family"):
            self.assertEqual(self.formatted["stateLayout"][field], self.doc["stateLayout"][field])

    def test_first_case_and_three_optional_cases_have_exact_separate_guides(self):
        before, _ = self.owners(self.before)
        current, _ = self.owners(self.doc)
        original = before[COMPILER]["outputCheck"]["cases"]
        cases = current[COMPILER]["outputCheck"]["cases"]
        self.assertEqual(len(original), 4)
        self.assertEqual(len(cases), len(original))
        # The engine uses the first declared case for completion. Preserve that
        # actual case and the remaining three in their original order; prose is
        # free to explain their purpose in separate authored blocks.
        for index, (previous, case) in enumerate(zip(original, cases), 1):
            with self.subTest(case=index):
                self.assertEqual(case["standardInput"], previous["standardInput"])
                self.assertEqual(case["outputs"], previous["outputs"])
                for name, expected in (("entrada", case["standardInput"]),
                                       ("salida", case["outputs"][0])):
                    block = current[f"u4l7_est_{name}_{index}"]
                    self.assertEqual((block["type"], block["language"]), ("code", "text"))
                    self.assertEqual(block["code"], expected)

    def test_later_cpp_spacing_with_fewer_blank_lines_is_valid(self):
        proposal = deepcopy(self.formatted)
        blocks, _ = self.owners(proposal)
        old = blocks[COMPILER]["initialCode"]
        updated, count = re.subn(r"\bint\s+grupos\s*=\s*0\s*;", "int grupos = 0;", old)
        self.assertEqual(count, 1)
        updated = updated.replace("\n\n", "\n")
        self.assertLess(updated.count("\n\n"), old.count("\n\n"))
        blocks[COMPILER]["initialCode"] = updated
        revised = self.publish(self.formatted, proposal)
        state.validate_transition(self.formatted, revised)
        self.assert_sources_and_history(revised)
        self.assertEqual(revised["stateLayout"]["sourceFormatRevisions"],
                         self.formatted["stateLayout"]["sourceFormatRevisions"])

    def test_later_cmake_invocation_reflow_is_valid(self):
        proposal = deepcopy(self.formatted)
        blocks, _ = self.owners(proposal)
        old = blocks[CMAKE]["code"]
        updated, count = re.subn(
            r"target_link_libraries\s*\(\s*assetcheck_tipos\s+PRIVATE\s+",
            "target_link_libraries( assetcheck_tipos   PRIVATE ", old)
        self.assertEqual(count, 1)
        blocks[CMAKE]["code"] = updated.replace("\n\n", "\n")
        self.assertNotEqual(blocks[CMAKE]["code"], old)
        revised = self.publish(self.formatted, proposal)
        state.validate_transition(self.formatted, revised)
        self.assert_sources_and_history(revised)

    def test_current_relocated_owners_accept_another_lexical_reflow(self):
        """Format the *current* enlarged lesson without moving owners back.

        Unlike the original six-format publication, this transition must
        retain all intervening text, retirement and editorial history.
        Both fewer and more empty physical lines are legitimate when the
        lexer proves the C++ or CMake token sequence is unchanged.
        """
        all_blocks, positions = self.owners(self.doc)
        old_archives = deepcopy(self.doc["stateLayout"]["sourceFormatRevisions"])
        old_proofs = deepcopy(self.doc["stateLayout"]["editorial"])
        # Reflow every published source owner after its later lesson reorder.
        # The original six archives must remain intact; none is a new puzzle.
        for owner in OWNERS:
            field = "initialCode" if owner == COMPILER else "code"
            with self.subTest(owner=owner):
                proposal = deepcopy(self.doc)
                blocks, projected_positions = self.owners(proposal)
                self.assertEqual(positions[owner], projected_positions[owner])
                original = all_blocks[owner]
                candidate = blocks[owner]
                self.assertIn(field, candidate)
                before_text = original[field]
                self.assertIn("\n", before_text)
                # Take away one superfluous blank line when present, or add
                # a separation at an existing line break, never alter tokens.
                if "\n\n" in before_text:
                    after_text = before_text.replace("\n\n", "\n", 1)
                else:
                    after_text = before_text.replace("\n", "\n\n", 1)
                self.assertNotEqual(before_text, after_text)
                candidate[field] = after_text
                if candidate["type"] == "cpp_compiler":
                    linked = [file for file in candidate.get("projectFiles", [])
                              if file["initialCode"] == before_text]
                    for file in linked:
                        file["initialCode"] = after_text
                self.assertTrue(source_format.require_source_lexical(original, candidate))
                revised = self.publish(self.doc, proposal)
                self.assertEqual(state.validate(self.doc), state.validate(revised))
                state.validate_transition(self.doc, revised)
                self.assert_sources_and_history(revised)
                self.assertEqual(revised["stateLayout"]["sourceFormatRevisions"], old_archives)
                self.assertEqual(revised["stateLayout"]["editorial"][:len(old_proofs)],
                                 old_proofs)
                self.assertEqual(revised["stateLayout"]["origin"],
                                 self.doc["stateLayout"]["origin"])
                self.assertEqual(revised["stateLayout"]["current"],
                                 self.doc["stateLayout"]["current"])
                self.assertEqual(revised["stateLayout"]["family"],
                                 self.doc["stateLayout"]["family"])
                self.assertEqual(revised["stateLayout"].get("retirementHistory", []),
                                 self.doc["stateLayout"].get("retirementHistory", []))
                self.assertEqual(
                    self.owners(revised)[0][owner][field], after_text)
                for key in ("outputCheck", "sourceFiles", "projectFiles", "cmake"):
                    if key == "projectFiles":
                        # The original source file itself may be reformatted,
                        # but not its paths, source selection or objective.
                        continue
                    self.assertEqual(original.get(key), self.owners(revised)[0][owner].get(key))

    def test_current_cmake_contract_changes_are_not_source_format(self):
        """Formatting may reflow CMake, not weaken linkage or the C++ standard."""
        original, _ = self.owners(self.doc)
        original_cmake = original[CMAKE]
        changes = (
            ("PRIVATE assetcheck_estadisticas assetcheck_manifiesto",
             "PUBLIC assetcheck_estadisticas assetcheck_manifiesto"),
            ("PUBLIC cxx_std_23", "PUBLIC cxx_std_20"),
        )
        for before, after in changes:
            with self.subTest(replacement=after):
                proposal = deepcopy(self.doc)
                blocks, _ = self.owners(proposal)
                source = blocks[CMAKE]["code"]
                self.assertEqual(source.count(before), 1)
                blocks[CMAKE]["code"] = source.replace(before, after, 1)
                with self.assertRaises(source_format.SourceFormatError):
                    source_format.require_source_lexical(original_cmake, blocks[CMAKE])
                with self.assertRaisesRegex(state.LayoutError, "Unsafe source formatting"):
                    self.publish(self.doc, proposal)

    def test_logic_change_is_rejected_with_generated_editorial_proof(self):
        proposal = deepcopy(self.doc)
        blocks, _ = self.owners(proposal)
        changed, count = re.subn(r"\bg\s*<\s*grupos\b", "g <= grupos",
                                blocks[COMPILER]["initialCode"])
        self.assertEqual(count, 1)
        blocks[COMPILER]["initialCode"] = changed
        with self.assertRaisesRegex(state.LayoutError, "Unsafe source formatting"):
            self.publish(self.doc, proposal)

    def test_oracle_input_and_first_case_order_cannot_be_revised_as_format(self):
        for change in ("output", "input", "first-case"):
            proposal = deepcopy(self.doc)
            blocks, _ = self.owners(proposal)
            cases = blocks[COMPILER]["outputCheck"]["cases"]
            if change == "output":
                cases[0]["outputs"][0] += "invented result\n"
            elif change == "input":
                cases[0]["standardInput"] += "invented input\n"
            else:
                cases[0], cases[1] = cases[1], cases[0]
            with self.subTest(change=change), self.assertRaisesRegex(
                    state.LayoutError, "response/code contract changed"):
                self.publish(self.doc, proposal)

    def test_published_sources_cannot_be_assigned_to_other_owners(self):
        proposal = deepcopy(self.doc)
        _, positions = self.owners(proposal)
        p, b = positions[OWNERS[0]]
        q, c = positions[OWNERS[2]]
        proposal["pages"][p]["blocks"][b], proposal["pages"][q]["blocks"][c] = (
            proposal["pages"][q]["blocks"][c], proposal["pages"][p]["blocks"][b])
        with self.assertRaisesRegex(state.LayoutError, "Unsafe source formatting"):
            self.publish(self.doc, proposal)

    def test_format_archive_cannot_borrow_another_owner_source(self):
        proposal = deepcopy(self.doc)
        archives = {entry["id"]: entry for entry in proposal["stateLayout"]["sourceFormatRevisions"]}
        archives[OWNERS[0]]["block"] = deepcopy(archives[OWNERS[2]]["block"])
        with self.assertRaisesRegex(state.LayoutError, "Source formatting archive changed"):
            state.validate_transition(self.doc, proposal)

    def test_fabricated_intermediate_publication_cannot_replace_authentic_history(self):
        proposal = deepcopy(self.formatted)
        blocks, _ = self.owners(proposal)
        invented = deepcopy(blocks[COMPILER])
        invented["initialCode"] = "\n" + invented["initialCode"]
        digest = state.fingerprint(invented)
        proofs = proposal["stateLayout"]["editorial"]
        at = next(i for i, proof in enumerate(proofs) if proof["id"] == COMPILER)
        genuine = proofs[at]
        # This forged chain is internally consistent and still ends at the real
        # block. Only the complete authenticated publication proves it is false.
        proofs[at:at + 1] = [
            {"id": COMPILER, "beforeSha256": genuine["beforeSha256"], "afterSha256": digest},
            {"id": COMPILER, "beforeSha256": digest, "afterSha256": genuine["afterSha256"]},
        ]
        state.validate(proposal)
        with self.assertRaisesRegex(state.LayoutError, "Editorial proof is append-only"):
            state.validate_transition(self.formatted, proposal)

    def test_misaligned_or_duplicate_current_identities_are_rejected(self):
        for change in ("alignment", "duplicate"):
            proposal = deepcopy(self.doc)
            _, positions = self.owners(proposal)
            p, b = positions[COMPILER]
            ids = proposal["stateLayout"]["current"][p]["blocks"]
            if change == "alignment":
                ids.pop(b)
            else:
                ids[b] = OWNERS[0]
            with self.subTest(change=change), self.assertRaises(state.LayoutError):
                state.validate(proposal)


if __name__ == "__main__":
    unittest.main()

"""QA helpers for authentic source-format publications after lesson edits.

Historical input is a complete, byte-pinned Git publication.  A current block
is selected by its stable owner, including an authenticated retired block;
display coordinates and the position of an editorial edge are not identities.
"""
from __future__ import annotations

from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import NamedTuple

import code_format_compat as source_format
import stable_state_layout as state
from windows_cpp_ci import msvc_cpp23_command, windows_executable


class Publication(NamedTuple):
    commit: str
    sha256: str


def verify_publication_bytes(payload: bytes, publication: Publication) -> bytes:
    if hashlib.sha256(payload).hexdigest() != publication.sha256:
        raise ValueError("Source differs from its pinned complete publication")
    return payload


@lru_cache(maxsize=None)
def publication_bytes(root: Path, relative: str, publication: Publication) -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    payload = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={root}",
         "show", f"{publication.commit}:{relative}"],
        cwd=root, env=environment, stderr=subprocess.PIPE)
    return verify_publication_bytes(payload, publication)


def load_publication(root: Path, relative: str, publication: Publication) -> dict:
    return json.loads(publication_bytes(root, relative, publication))


def active_owners(document: dict) -> tuple[dict, dict]:
    pages, layout = document["pages"], document["stateLayout"]["current"]
    if len(pages) != len(layout):
        raise ValueError("Page content and stable layout have different lengths")
    blocks, positions, page_ids = {}, {}, set()
    for p, (page, record) in enumerate(zip(pages, layout)):
        if record["id"] in page_ids or len(page["blocks"]) != len(record["blocks"]):
            raise ValueError("Duplicate stable page or mismatched block layout")
        page_ids.add(record["id"])
        for b, (block, owner) in enumerate(zip(page["blocks"], record["blocks"])):
            if owner in blocks:
                raise ValueError("Duplicate active owner")
            blocks[owner], positions[owner] = block, (p, b)
    return blocks, positions


def all_owners(document: dict) -> dict:
    blocks, _ = active_owners(document)
    for entry in document["stateLayout"].get("retired", []):
        if entry["id"] in blocks:
            raise ValueError("Duplicate active or retired owner")
        blocks[entry["id"]] = entry["block"]
    return blocks


def assert_stable_storage(case, earlier: dict, later: dict, owners) -> None:
    before, old_positions = active_owners(earlier)
    current, positions = active_owners(later)
    old_storage, storage = state.validate(earlier), state.validate(later)
    for owner in set(owners) & before.keys() & current.keys():
        case.assertEqual(storage[positions[owner]], old_storage[old_positions[owner]], owner)
    for field in ("origin", "family"):
        case.assertEqual(later["stateLayout"][field], earlier["stateLayout"][field], field)


def assert_format_history(case, before: dict, formatted: dict,
                          current: dict, owners) -> None:
    state.validate(current)
    original, spaced, resolved = map(all_owners, (before, formatted, current))
    entries = current["stateLayout"]["sourceFormatRevisions"]
    archives = {entry["id"]: entry["block"] for entry in entries}
    case.assertEqual(len(entries), len(archives))
    case.assertTrue(set(owners) <= archives.keys())
    for owner in owners:
        with case.subTest(owner=owner):
            case.assertEqual(archives[owner], original[owner])
            case.assertTrue(source_format.require_source_format(original[owner], spaced[owner]))
            source_format.require_source_lexical(original[owner], resolved[owner])
            projected = source_format.source_projection(resolved[owner], original[owner])
            case.assertEqual(state.state_fingerprint(projected),
                             state.state_fingerprint(original[owner]))
            edge = {"id": owner,
                    "beforeSha256": state.fingerprint(original[owner]),
                    "afterSha256": state.fingerprint(spaced[owner])}
            published = [entry for entry in formatted["stateLayout"]["editorial"]
                         if entry["id"] == owner]
            present = [entry for entry in current["stateLayout"]["editorial"]
                       if entry["id"] == owner]
            case.assertIn(edge, published)
            case.assertEqual(present[:len(published)], published)


def assert_retirement_snapshot(case, parent: dict, retired: dict, owners) -> None:
    expected = set(owners)
    snapshots = [entry for entry in retired["stateLayout"]["retirementHistory"]
                 if set(entry["retired"]) == expected]
    case.assertEqual(1, len(snapshots), "Missing or ambiguous published retirement")
    snapshot = snapshots[0]
    case.assertEqual(snapshot["pages"], parent["pages"])
    case.assertEqual(snapshot["current"], parent["stateLayout"]["current"])
    for field, default in (("compilerPuzzlesRequired", False), ("referenceAfter", []),
                           ("activityAfter", []), ("textSemantics", [])):
        case.assertEqual(snapshot[field], parent.get(field, default), field)
    editorial = parent["stateLayout"]["editorial"]
    case.assertEqual(snapshot["editorialPrefix"], {
        "count": len(editorial), "sha256": state.fingerprint({
            "lessonId": parent["id"], "locale": parent["stateLayout"]["locale"],
            "editorial": editorial})})
    previous, active = active_owners(parent)[0], active_owners(retired)[0]
    resolved = all_owners(retired)
    for owner in owners:
        case.assertNotIn(owner, active)
        case.assertEqual(resolved[owner], previous[owner], owner)


def source_field(block: dict) -> str:
    return "initialCode" if block["type"] == "cpp_compiler" else (
        "template" if block["type"] == "inline_fill_code" else "code")


def compilable_source(block: dict) -> str:
    source = block[source_field(block)]
    if block["type"] == "fill_code":
        if source.count("___") != 1 or not block["answers"]:
            raise ValueError("Fill-code source and authored answer slot differ")
        source = source.replace("___", block["answers"][0], 1)
    if block["type"] == "inline_fill_code":
        if source.count("[]") != len(block["answers"]):
            raise ValueError("Inline source and authored answer slots differ")
        for answer in block["answers"]:
            source = source.replace("[]", answer, 1)
    return source


class FormatBatch:
    """Complete real publications and their matching active/archived sources."""

    def __init__(self, root: Path, relative: str, publications: dict,
                 owners, retired_owners):
        self.root, self.relative, self.publications = root, relative, publications
        self.owners, self.retired_owners = tuple(owners), set(retired_owners)
        self.documents = {name: load_publication(root, relative, publication)
                          for name, publication in publications.items()}
        self.before = self.documents["before"]
        self.formatted = self.documents["formatted"]
        self.retirement_parent = self.documents["retirement_parent"]
        self.retirement = self.documents["retirement"]
        self.current = json.loads((root / relative).read_bytes())

    def assert_history(self, case) -> None:
        for document in (*self.documents.values(), self.current):
            case.assertEqual(document["id"], self.before["id"])
            state.validate(document)
        state.validate_transition(self.before, self.formatted)
        state.validate_transition(self.formatted, self.retirement_parent)
        state.validate_transition(self.retirement_parent, self.retirement)
        case.assertEqual(state.validate(self.before), state.validate(self.formatted))
        assert_retirement_snapshot(case, self.retirement_parent,
                                   self.retirement, self.retired_owners)
        for document in (self.formatted, self.retirement, self.current):
            assert_format_history(case, self.before, self.formatted, document, self.owners)
            assert_stable_storage(case, self.formatted, document, self.owners)
        assert_retirement_snapshot(case, self.retirement_parent,
                                   self.current, self.retired_owners)

    def assert_negative_controls(self, case) -> None:
        for name, publication in self.publications.items():
            payload = publication_bytes(self.root, self.relative, publication)
            with case.subTest(publication=name):
                case.assertEqual(verify_publication_bytes(payload, publication), payload)
                with case.assertRaisesRegex(ValueError, "pinned complete publication"):
                    verify_publication_bytes(payload + b"\n", publication)
                other = "formatted" if name == "before" else "before"
                with case.assertRaisesRegex(ValueError, "pinned complete publication"):
                    verify_publication_bytes(publication_bytes(
                        self.root, self.relative, self.publications[other]), publication)
        mutations = []
        logic = deepcopy(self.current)
        owner = self.owners[0]
        block = all_owners(logic)[owner]
        block[source_field(block)] += "\nint forged_format_token;\n"
        with case.assertRaises(source_format.SourceFormatError):
            source_format.require_source_lexical(all_owners(self.before)[owner], block)
        mutations.append(("source tokens", logic))
        oracle = deepcopy(self.current)
        resolved = all_owners(oracle)
        editor = next(resolved[owner] for owner in sorted(self.retired_owners)
                      if resolved[owner].get("type") == "cpp_compiler"
                      and resolved[owner].get("outputCheck"))
        editor["outputCheck"]["cases"][0]["outputs"] = ["forged oracle"]
        mutations.append(("archived output oracle", oracle))
        answer = deepcopy(self.current)
        prediction = next(block for owner, block in all_owners(answer).items()
                          if owner in self.owners and block.get("type") == "output_prediction")
        prediction["answers"] = ["forged prediction"]
        mutations.append(("historical response", answer))
        archive = deepcopy(self.current)
        records = archive["stateLayout"]["sourceFormatRevisions"]
        records[0]["id"], records[1]["id"] = records[1]["id"], records[0]["id"]
        mutations.append(("source owner", archive))
        history = deepcopy(self.current)
        history["stateLayout"]["retirementHistory"][0]["editorialPrefix"]["count"] -= 1
        mutations.append(("published retirement", history))
        for name, document in mutations:
            with case.subTest(mutation=name), case.assertRaises(state.LayoutError):
                state.validate(document)

    def assert_msvc_outputs(self, case, *, incorrect_initial=()) -> None:
        binaries, executions = {}, {}
        original = all_owners(self.before)
        with tempfile.TemporaryDirectory(prefix="source-format-history-") as folder:
            def observe(block, inputs, owner):
                code = compilable_source(block)
                if code not in binaries:
                    number = len(binaries)
                    src = Path(folder) / f"source-{number}.cpp"
                    exe = Path(folder) / f"source-{number}"
                    src.write_text(code, encoding="utf-8")
                    built = subprocess.run(msvc_cpp23_command(exe, src),
                        cwd=folder, capture_output=True, text=True, timeout=90)
                    case.assertEqual(0, built.returncode, owner + built.stdout + built.stderr)
                    binaries[code] = windows_executable(exe)
                results = []
                for stdin in inputs:
                    key = (code, stdin)
                    if key not in executions:
                        run = subprocess.run([str(binaries[code])], input=stdin,
                            cwd=folder, capture_output=True, text=True, timeout=15)
                        case.assertEqual(0, run.returncode, owner + run.stderr)
                        case.assertEqual("", run.stderr, owner)
                        executions[key] = (run.returncode, run.stdout, run.stderr)
                    results.append(executions[key])
                return results

            for name, document in (("formatted", self.formatted),
                                   ("retirement", self.retirement), ("current", self.current)):
                assert_format_history(case, self.before, self.formatted, document, self.owners)
                resolved = all_owners(document)
                for owner in self.owners:
                    with case.subTest(publication=name, owner=owner):
                        old, new = original[owner], resolved[owner]
                        cases = new.get("outputCheck", {}).get("cases", [])
                        inputs = [entry["standardInput"] for entry in cases] or [
                            new.get("initialStandardInput", "")]
                        before, after = observe(old, inputs, owner), observe(new, inputs, owner)
                        case.assertEqual(before, after, owner)
                        for index, (result, entry) in enumerate(zip(after, cases)):
                            if owner in incorrect_initial and index == 0:
                                case.assertNotIn(result[1], entry["outputs"], owner)
                            else:
                                case.assertIn(result[1], entry["outputs"], owner)
                        if new["type"] == "output_prediction":
                            case.assertIn(after[0][1], new["answers"], owner)
            print(f"PASS {self.before['id']} MSVC: {len(binaries)} compiled sources, "
                  f"{len(executions)} executed input cases, {len(self.owners)} authenticated owners")

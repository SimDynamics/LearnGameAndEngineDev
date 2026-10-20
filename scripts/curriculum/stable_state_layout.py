"""Prepare and verify lossless lesson reorders. No student data or database is opened.

Each immutable identity resolves to its original storage address. State-bearing
blocks (including editors with no curricular ID) therefore keep the same saved
answer/draft keys. A reorder cannot modify response contracts, add or remove activities implicitly.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


class LayoutError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise LayoutError(message)


def fingerprint(value: object) -> str:
    """Matches LessonStateLayoutJson.fingerprint, including UTF-16 key ordering."""
    digest = hashlib.sha256()

    def token(text: str) -> None:
        digest.update(text.encode("utf-8"))

    def string(text: str) -> None:
        encoded = text.encode("utf-8", errors="strict")
        token(f"s{len(encoded)}:")
        digest.update(encoded)

    def visit(node: object, depth: int = 0) -> None:
        require(depth <= 64, "JSON nesting exceeds the runtime limit")
        if node is None:
            token("z")
        elif type(node) is bool:
            token("t" if node else "f")
        elif type(node) is str:
            string(node)
        elif type(node) in (int, float, Decimal):
            number = Decimal(str(node))
            require(number.is_finite(), "A JSON number must be finite")
            # No context-dependent Decimal.normalize rounding of long integers.
            sign, digits, exponent = number.as_tuple()
            require(abs(exponent) <= 10_000 and len(digits) <= 10_000, "Number outside runtime limits")
            text = format(number, "f")
            if "." in text:
                text = text.rstrip("0").rstrip(".")
            if number == 0:
                text = "0"
            token("n" + text + ";")
        elif type(node) is list:
            token("[")
            for item in node:
                visit(item, depth + 1)
            token("]")
        elif type(node) is dict:
            require(all(type(k) is str for k in node), "JSON object keys must be strings")
            token("{")
            for key in sorted(node, key=lambda key: key.encode("utf-16-be")):
                string(key)
                visit(node[key], depth + 1)
            token("}")
        else:
            raise LayoutError("Unsupported JSON value")

    visit(value)
    return digest.hexdigest()


def state_payload(block: dict) -> dict:
    """Only explicit editorial fields may vary without changing a response contract.

    The existing curricular validator still decides which wording changes are
    authorized. Mathematics retains its exact content-fingerprint contract.
    """
    kind = block.get("type")
    if kind in {"heading", "paragraph", "note"}:
        # Reading coverage belongs to an authored block identity, not its wording.
        return {key: value for key, value in block.items() if key not in {"text", "emphasis"}}
    if kind in {"multiple_choice", "fill_code", "output_prediction", "order_code",
                "drag_fill_code", "inline_fill_code"}:
        return {key: value for key, value in block.items()
                if key not in {"question", "explanation", "reviewFeedback"}}
    if kind == "cpp_compiler":
        payload = deepcopy(block)
        config = payload.get("outputCheck")
        if type(config) is dict and type(config.get("cases")) is list:
            for case in config["cases"]:
                if type(case) is dict:
                    case.pop("explanation", None)
                    case.pop("reviewFeedback", None)
        return payload
    return block


def state_fingerprint(block: dict) -> str:
    return fingerprint(state_payload(block))


def _pages(document: dict) -> list[dict]:
    require(type(document) is dict, "A lesson must be a JSON object")
    pages = document.get("pages")
    require(type(pages) is list and 0 < len(pages) <= 2048, "Expected nonempty pages")
    require(all(type(p) is dict and type(p.get("blocks", [])) is list for p in pages), "Malformed pages")
    require(sum(len(p.get("blocks", [])) for p in pages) <= 100_000, "Too many blocks")
    require(all(type(b) is dict for p in pages for b in p.get("blocks", [])), "A block must be an object")
    return pages


def prepare(document: dict) -> dict:
    """Bootstrap from a published positional layout, before its first reorder."""
    if "stateLayout" in document:
        validate(document)
        return deepcopy(document)
    result = deepcopy(document)
    origin, current = [], []
    for p, page in enumerate(_pages(document)):
        page_id = f"page_{p:04d}"
        blocks = [{"id": f"block_{p:04d}_{b:04d}", "sha256": state_fingerprint(block)}
                  for b, block in enumerate(page.get("blocks", []))]
        origin.append({"id": page_id, "blocks": blocks})
        current.append({"id": page_id, "blocks": [b["id"] for b in blocks]})
    result["stateLayout"] = {"version": 1, "origin": origin, "current": current}
    validate(result)
    return result


def validate(document: dict) -> dict[tuple[int, int], tuple[int, int]]:
    """Return current coordinates -> original immutable coordinates, or fail closed."""
    pages = _pages(document)
    if "stateLayout" not in document:
        return {(p, b): (p, b) for p, page in enumerate(pages) for b in range(len(page.get("blocks", [])))}
    meta = document["stateLayout"]
    require(type(meta) is dict and set(meta) == {"version", "origin", "current"}, "Invalid stateLayout fields")
    require(type(meta["version"]) is int and meta["version"] == 1, "Unsupported stateLayout version")
    origin, current = meta["origin"], meta["current"]
    require(type(origin) is list and type(current) is list and len(origin) == len(current) == len(pages),
            "A reorder must preserve the page count")
    require(origin, "No original pages")
    origin_pages, origin_blocks, digests = [], {}, {}
    current_pages, current_blocks, mapping = [], [], {}

    def identifier(value: object) -> str:
        require(type(value) is str and re.fullmatch(r"[A-Za-z0-9_-]{1,128}", value) is not None,
                "Invalid stable identity")
        return value

    for p, page in enumerate(origin):
        require(type(page) is dict and set(page) == {"id", "blocks"}, "Malformed original page")
        origin_pages.append(identifier(page["id"]))
        require(type(page["blocks"]) is list, "Malformed original blocks")
        for b, block in enumerate(page["blocks"]):
            require(type(block) is dict and set(block) == {"id", "sha256"}, "Malformed original block")
            block_id = identifier(block["id"])
            require(block_id not in origin_blocks, "Duplicate original block identity")
            require(type(block["sha256"]) is str and re.fullmatch(r"[0-9a-f]{64}", block["sha256"]),
                    "Invalid block digest")
            origin_blocks[block_id] = (p, b)
            digests[block_id] = block["sha256"]
    require(len(origin_blocks) <= 100_000, "Too many original blocks")
    for p, page in enumerate(current):
        require(type(page) is dict and set(page) == {"id", "blocks"}, "Malformed current page")
        current_pages.append(identifier(page["id"]))
        require(type(page["blocks"]) is list and len(page["blocks"]) == len(pages[p].get("blocks", [])),
                "Current layout does not match the visible blocks")
        for b, block_id in enumerate(page["blocks"]):
            block_id = identifier(block_id)
            require(block_id in origin_blocks, "Unknown block identity")
            require(state_fingerprint(pages[p]["blocks"][b]) == digests[block_id],
                    "A response/code contract changed or belongs to another identity")
            current_blocks.append(block_id)
            mapping[p, b] = origin_blocks[block_id]
    require(len(set(origin_pages)) == len(origin_pages), "Duplicate original page identity")
    require(len(set(current_pages)) == len(current_pages) and set(current_pages) == set(origin_pages),
            "Page identities changed")
    require(len(set(current_blocks)) == len(current_blocks) and set(current_blocks) == set(origin_blocks),
            "Block identities changed")
    return mapping


def reorder(document: dict, plan: list[dict]) -> dict:
    """Move existing page shells and blocks according to an explicit ID-only plan."""
    before = prepare(document)
    pages_by_id, blocks_by_id = {}, {}
    for page, identity in zip(before["pages"], before["stateLayout"]["current"]):
        pages_by_id[identity["id"]] = {key: deepcopy(value) for key, value in page.items() if key != "blocks"}
        blocks_by_id.update(zip(identity["blocks"], deepcopy(page.get("blocks", []))))
    result = deepcopy(before)
    require(type(plan) is list, "Expected a page-layout list")
    result["pages"] = []
    for identity in plan:
        require(type(identity) is dict and set(identity) == {"id", "blocks"}, "Invalid reorder plan")
        require(identity["id"] in pages_by_id and type(identity["blocks"]) is list, "Unknown page in plan")
        require(all(i in blocks_by_id for i in identity["blocks"]), "Unknown block in plan")
        result["pages"].append({**deepcopy(pages_by_id[identity["id"]]),
                                "blocks": [deepcopy(blocks_by_id[i]) for i in identity["blocks"]]})
    result["stateLayout"]["current"] = deepcopy(plan)
    validate_transition(before, result)
    return result


def validate_transition(previous: dict, current: dict) -> None:
    """Never authorize a renamed origin or changed answer while allowing a reorder."""
    require("stateLayout" in current, "A reordered lesson needs an identity manifest")
    baseline = prepare(previous)
    validate(current)
    require(current["stateLayout"]["origin"] == baseline["stateLayout"]["origin"],
            "The immutable origin was changed; do not reset the baseline")
    previous_other = {k: v for k, v in baseline.items() if k not in {"pages", "stateLayout"}}
    current_other = {k: v for k, v in current.items() if k not in {"pages", "stateLayout"}}
    require(previous_other == current_other, "A reorder cannot modify lesson metadata")
    before_shells = {item["id"]: {k: v for k, v in page.items() if k != "blocks"}
                     for page, item in zip(baseline["pages"], baseline["stateLayout"]["current"])}
    after_shells = {item["id"]: {k: v for k, v in page.items() if k != "blocks"}
                    for page, item in zip(current["pages"], current["stateLayout"]["current"])}
    require(before_shells == after_shells, "Page metadata was changed")
    before_blocks = {identity: fingerprint(block)
                     for page, record in zip(baseline["pages"], baseline["stateLayout"]["current"])
                     for identity, block in zip(record["blocks"], page.get("blocks", []))}
    after_blocks = {identity: fingerprint(block)
                    for page, record in zip(current["pages"], current["stateLayout"]["current"])
                    for identity, block in zip(record["blocks"], page.get("blocks", []))}
    def math_serializations(document: dict) -> dict[str, str]:
        # Android's existing MathPuzzleState hashes JSONObject.toString(), whose
        # insertion order matters. Keep that legacy fingerprint intact as well
        # as the order-independent state-map digest; do not rewrite saved states.
        return {identity: json.dumps(block, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
                for page, record in zip(document["pages"], document["stateLayout"]["current"])
                for identity, block in zip(record["blocks"], page.get("blocks", []))
                if str(block.get("type", "")).startswith("math_")}
    require(math_serializations(baseline) == math_serializations(current),
            "Mathematical payloads must preserve their historical JSON key order and values")
    if "stateLayout" not in previous or baseline["stateLayout"]["current"] != current["stateLayout"]["current"]:
        # Editorial payloads intentionally omit prose from their state hashes.
        # On first adoption or a move, compare full blocks so a regenerated map
        # cannot attach read coverage to a different, equally shaped paragraph.
        require(before_blocks == after_blocks,
                "First adoption and reorders must preserve complete blocks; "
                "publish editorial revisions separately before or after the move")
    else:
        old_owners = {}
        for identity, signature in before_blocks.items():
            old_owners.setdefault(signature, set()).add(identity)
        for identity, signature in after_blocks.items():
            require(signature not in old_owners or identity in old_owners[signature],
                    "Published content was assigned to another identity without a verified move")


def align_for_previous_validation(previous: dict, current: dict) -> dict:
    """Project new blocks onto HEAD's positions only AFTER validating the whole move.

    The existing validator then continues checking all response/code contracts.
    Legacy content without identity metadata retains the original position rules.
    """
    if "stateLayout" not in previous and "stateLayout" not in current:
        return current
    validate_transition(previous, current)
    old = prepare(previous)
    blocks = {identity: block for page, record in zip(current["pages"], current["stateLayout"]["current"])
              for identity, block in zip(record["blocks"], page.get("blocks", []))}
    page_by_id = {record["id"]: page for page, record in zip(current["pages"], current["stateLayout"]["current"])}
    result = deepcopy(current)
    result["pages"] = [{**deepcopy(page_by_id[record["id"]]),
                        "blocks": [deepcopy(blocks[i]) for i in record["blocks"]]}
                       for record in old["stateLayout"]["current"]]
    return result


def lineage(document: dict) -> tuple:
    metadata = prepare(document)["stateLayout"]
    return tuple((page["id"], tuple(block["id"] for block in page["blocks"]))
                 for page in metadata["origin"])


def check_localized_family(documents: list[dict]) -> None:
    """Call with every shipped translation before publishing the first adoption."""
    require(bool(documents), "No lessons to verify")
    require(all("stateLayout" in d for d in documents), "Every shipped translation needs a state map")
    for document in documents:
        validate(document)
    require(len({d["id"] for d in documents}) == 1, "Not the same lesson")
    require(len({lineage(d) for d in documents}) == 1, "Translations disagree on stable identities")
    # Existing exercise IDs and block types must represent the same original slots.
    descriptors = []
    for doc in documents:
        mapping = validate(doc)
        descriptors.append({original: (doc["pages"][p]["blocks"][b].get("type"),
                                       doc["pages"][p]["blocks"][b].get("id"))
                            for (p, b), original in mapping.items()})
    require(all(d == descriptors[0] for d in descriptors[1:]), "Translations have different original activities")


def parse_json(text: str):
    def pairs(items):
        result = {}
        for k, v in items:
            require(k not in result, "Duplicate JSON key")
            result[k] = v
        return result
    return json.loads(text, object_pairs_hook=pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(LayoutError("Non-finite number")))


def load(path: Path):
    return parse_json(path.read_text(encoding="utf-8-sig"))


CONTENT_DIRECTORY = "app/src/main/assets/content"


def resolve_base_commit(repository: Path, base_ref: str) -> str:
    """Pin the caller's verified published ref before reading any baseline file."""
    require(bool(base_ref), "Provide the verified published commit with --base-ref")
    result = subprocess.run(
        ["git", "rev-parse", "--verify", "--end-of-options", base_ref + "^{commit}"],
        cwd=repository, capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "Cannot resolve the published baseline: " + result.stderr.strip())
    commit = result.stdout.strip()
    require(re.fullmatch(r"[0-9a-f]{40,64}", commit) is not None, "Invalid baseline commit")
    return commit


def _git_document(repository: Path, commit: str, relative: str) -> dict:
    result = subprocess.run(["git", "show", commit + ":" + relative], cwd=repository,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "Cannot read baseline lesson " + relative)
    return parse_json(result.stdout.lstrip("\ufeff"))


def _lesson_paths_at(repository: Path, commit: str) -> set[str]:
    result = subprocess.run(["git", "ls-tree", "-r", "--name-only", "-z", commit,
                             "--", CONTENT_DIRECTORY], cwd=repository,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "Cannot list baseline lessons: " + result.stderr.strip())
    return {path for path in result.stdout.split("\0")
            if PurePosixPath(path).name.startswith("lesson_") and path.endswith(".json")}


def _mapped_paths_at(repository: Path, commit: str) -> set[str]:
    # Also inspect the old tree: deleting a manifest must not bypass the gate.
    result = subprocess.run(["git", "grep", "-l", "-z", "-F", '"stateLayout"', commit,
                             "--", CONTENT_DIRECTORY], cwd=repository,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode in (0, 1), "Cannot inspect baseline state maps: " + result.stderr.strip())
    prefix = commit + ":"
    return {item[len(prefix):] for item in result.stdout.split("\0")
            if item.startswith(prefix) and item.endswith(".json")
            and PurePosixPath(item).name.startswith("lesson_")}


def check_repository(repository: Path, base_ref: str | None) -> dict:
    """Check every route and shipped locale against a pinned published tree.

    This is a publication guard, not just a self-consistency check. No network,
    branch, student preference, content, or Git index is changed. A caller must
    fetch/verify the remote HEAD itself and supply that exact baseline.
    """
    repository = repository.resolve()
    content = repository / CONTENT_DIRECTORY
    require(content.is_dir(), "Missing curriculum content directory")
    documents = {path.relative_to(repository).as_posix(): load(path)
                 for path in sorted(content.rglob("lesson_*.json"))}
    mapped = {path for path, document in documents.items() if "stateLayout" in document}
    # A metadata-bearing tree cannot claim safety without its published history.
    require(not mapped or base_ref is not None,
            "stateLayout publication requires --base-ref with the verified remote HEAD")
    if base_ref is None:
        if (repository / ".git").exists():
            head = resolve_base_commit(repository, "HEAD")
            require(not any("stateLayout" in _git_document(repository, head, path)
                            for path in _mapped_paths_at(repository, head)),
                    "Published stateLayout metadata requires --base-ref even when it was removed locally")
        return {"base_commit": None, "mapped_lessons": 0, "families": 0}
    commit = resolve_base_commit(repository, base_ref)
    prior_paths = _lesson_paths_at(repository, commit)
    prior_mapped = {}
    for path in _mapped_paths_at(repository, commit):
        previous = _git_document(repository, commit, path)
        if "stateLayout" in previous:
            prior_mapped[path] = previous
    for path in mapped | prior_mapped.keys():
        require(path in documents, "A mapped lesson was removed: " + path)
        current = documents[path]
        try:
            if path in prior_paths:
                previous = prior_mapped.get(path)
                if previous is None:
                    previous = _git_document(repository, commit, path)
                validate_transition(previous, current)
            else:
                # A new lesson has no positional state in the published app.
                validate(current)
        except (ValueError, KeyError, TypeError) as exc:
            raise LayoutError(path + ": " + str(exc)) from exc
    families = {}
    for path, document in documents.items():
        relative = PurePosixPath(path).relative_to(CONTENT_DIRECTORY)
        require(len(relative.parts) >= 2, "A lesson must belong to a locale: " + path)
        family = PurePosixPath(*relative.parts[1:]).as_posix()
        families.setdefault(family, []).append((path, document))
    checked_families = 0
    for family, members in families.items():
        if not any(path in mapped for path, _ in members):
            continue
        try:
            check_localized_family([document for _, document in members])
        except (ValueError, KeyError, TypeError) as exc:
            raise LayoutError(family + ": " + str(exc)) from exc
        checked_families += 1
    return {"base_commit": commit, "mapped_lessons": len(mapped), "families": checked_families}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "reorder", "check", "check-tree"))
    parser.add_argument("source", type=Path, help="Lesson JSON; repository directory for check-tree")
    parser.add_argument("--plan", type=Path, help="JSON list of stable page/block IDs in display order")
    parser.add_argument("--output", type=Path, help="New path; never overwrite a Work/Codex file")
    parser.add_argument("--previous", type=Path, help="Required published lesson for prepare/reorder/check")
    parser.add_argument("--base-ref", help="Required verified remote HEAD for check-tree")
    args = parser.parse_args()
    try:
        if args.action == "check-tree":
            require(args.base_ref is not None, "check-tree requires --base-ref with the verified remote HEAD")
            report = check_repository(args.source, args.base_ref)
            print(f"State transitions and shipped translations checked against {report['base_commit']}: "
                  f"{report['mapped_lessons']} mapped lessons in {report['families']} families. "
                  "This does not verify an Android build or device execution.")
            return
        require(args.previous is not None,
                "prepare/reorder/check requires --previous from the verified published commit")
        previous = load(args.previous)
        source = load(args.source)
        if args.action == "prepare":
            result = prepare(source)
        elif args.action == "reorder":
            require(args.plan is not None, "reorder requires --plan")
            result = reorder(source, load(args.plan))
        else:
            validate(source)
            validate_transition(previous, source)
            print("Lesson transition valid; run check-tree for all shipped translations before publication. "
                  "No student data or files changed.")
            return
        require(args.output is not None, "Use --output with a NEW path")
        validate_transition(previous, result)
        # Exclusive creation keeps unrelated local edits intact.
        with args.output.open("x", encoding="utf-8", newline="\n") as out:
            json.dump(result, out, ensure_ascii=False, indent=2, allow_nan=False)
            out.write("\n")
        print("Prepared without changing the source; verify every shipped translation before publishing.")
    except (LayoutError, OSError, ValueError) as exc:
        parser.exit(1, f"Not completed: {exc}\n")


if __name__ == "__main__":
    main()

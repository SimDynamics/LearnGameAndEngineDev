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


LOCALES = frozenset({"es", "en", "fr"})
V2_FIELDS = {"version", "locale", "origin", "current", "family", "additions", "editorial"}
V2_OPTIONAL_FIELDS = frozenset({"retired", "retirementHistory"})
RETIRABLE_TYPES = frozenset({"heading", "paragraph", "note", "code", "cpp_compiler"})
PRESENTATION_FIELDS = frozenset({"referenceAfter", "activityAfter"})
FLOW_FIELDS = frozenset({"compilerPuzzlesRequired"})


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
    # A relationship cannot manufacture its own historical identities merely
    # by passing through the bootstrap helper.
    validate(document)
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


def _without_layout(document: dict) -> dict:
    return {key: value for key, value in document.items() if key != "stateLayout"}


def _identity(value: object) -> str:
    require(type(value) is str and re.fullmatch(r"[A-Za-z0-9_-]{1,128}", value) is not None,
            "Invalid stable identity")
    return value


def _digest(value: object) -> str:
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value) is not None,
            "Invalid block digest")
    return value


def _origin(origin: object) -> tuple[list[str], dict[str, tuple[int, int]], dict[str, str]]:
    require(type(origin) is list and 0 < len(origin) <= 2048, "Invalid historical origin")
    pages, blocks, digests = [], {}, {}
    for p, page in enumerate(origin):
        require(type(page) is dict and set(page) == {"id", "blocks"}, "Malformed original page")
        pages.append(_identity(page["id"]))
        require(type(page["blocks"]) is list, "Malformed original blocks")
        for b, block in enumerate(page["blocks"]):
            require(type(block) is dict and set(block) == {"id", "sha256"}, "Malformed original block")
            identity = _identity(block["id"])
            require(identity not in blocks, "Duplicate original block identity")
            blocks[identity] = (p, b)
            digests[identity] = _digest(block["sha256"])
    require(len(set(pages)) == len(pages), "Duplicate original page identity")
    require(len(blocks) <= 100_000, "Too many original blocks")
    return pages, blocks, digests


def _blocks(document: dict) -> dict[str, dict]:
    return {identity: block
            for page, record in zip(document["pages"], document["stateLayout"]["current"])
            for identity, block in zip(record["blocks"], page.get("blocks", []))}


def _all_blocks(document: dict) -> dict[str, dict]:
    """Historical validation includes archived contracts, never their presentation."""
    return {**_blocks(document), **{item["id"]: item["block"]
                                  for item in document["stateLayout"].get("retired", [])}}


def _validate_v2(document: dict) -> dict[tuple[int, int], tuple[int, int]]:
    pages = _pages(document)
    meta = document["stateLayout"]
    require(V2_FIELDS <= set(meta) <= V2_FIELDS | V2_OPTIONAL_FIELDS, "Invalid stateLayout v2 fields")
    require(type(meta["locale"]) is str and meta["locale"] in LOCALES, "Unsupported stateLayout locale")
    page_ids, historical, digests = _origin(meta["origin"])
    family = meta["family"]
    require(type(family) is dict and family and set(family) <= LOCALES,
            "Malformed historical language family")
    require(meta["locale"] in family, "The language family omits this locale")
    family_ids = set()
    for entry in family.values():
        require(type(entry) is dict and set(entry) == {"sha256", "origin"}, "Malformed language source")
        _digest(entry["sha256"])
        _, identities, _ = _origin(entry["origin"])
        family_ids.update(identities)
    require(family[meta["locale"]]["origin"] == meta["origin"],
            "This locale must keep its own historical origin")
    reserved_pages = max(len(entry["origin"]) for entry in family.values())
    additions = meta["additions"]
    require(type(additions) is list and len(historical) + len(additions) <= 100_000,
            "Malformed or excessive activity additions")
    storage = dict(historical)
    added_slots = set()
    for addition in additions:
        require(type(addition) is dict and set(addition) == {"id", "sha256", "storage", "progressPage"},
                "Malformed activity addition")
        identity = _identity(addition["id"])
        require(identity not in storage and identity not in family_ids, "An addition reuses a historical identity")
        slot = addition["storage"]
        require(type(slot) is dict and set(slot) == {"page", "block"}, "Malformed new storage slot")
        require(type(slot["page"]) is int and reserved_pages <= slot["page"] <= 1_000_000
                and type(slot["block"]) is int and 0 <= slot["block"] <= 100_000,
                "New activity storage must be outside every historical language origin")
        position = slot["page"], slot["block"]
        require(position not in added_slots, "Duplicate new storage slot")
        require(addition["progressPage"] in page_ids, "Unknown addition progress page")
        added_slots.add(position)
        storage[identity] = position
        digests[identity] = _digest(addition["sha256"])
    current = meta["current"]
    require(type(current) is list and len(current) == len(pages) == len(page_ids),
            "This extension preserves historical pages")
    seen_pages, seen_blocks, mapping = [], [], {}
    for p, record in enumerate(current):
        require(type(record) is dict and set(record) == {"id", "blocks"}, "Malformed current page")
        seen_pages.append(_identity(record["id"]))
        require(type(record["blocks"]) is list and len(record["blocks"]) == len(pages[p].get("blocks", [])),
                "Current layout does not match the visible blocks")
        for b, identity in enumerate(record["blocks"]):
            identity = _identity(identity)
            require(identity in storage, "Unknown block identity")
            require(state_fingerprint(pages[p]["blocks"][b]) == digests[identity],
                    "A response/code contract changed or belongs to another identity")
            seen_blocks.append(identity)
            mapping[p, b] = storage[identity]
    require(len(set(seen_pages)) == len(seen_pages) and set(seen_pages) == set(page_ids), "Page identities changed")
    retired = meta.get("retired", [])
    require(type(retired) is list and len(retired) <= 100_000, "Malformed retired activity archive")
    require(not retired or meta["locale"] == "es", "Retirement is currently authorized only for Spanish")
    retired_ids = set()
    for item in retired:
        require(type(item) is dict and set(item) == {"id", "page", "next", "block"},
                "A retired activity needs its identity, page, successor and complete block")
        identity = _identity(item["id"])
        require(identity in storage and identity not in retired_ids and identity not in seen_blocks,
                "A retired identity is unknown, duplicated or still active")
        require(item["page"] in page_ids, "Unknown retired activity page")
        require(type(item["next"]) is str and (item["next"] == "" or item["next"] in storage)
                and item["next"] != identity, "Invalid retired activity successor")
        require(type(item["block"]) is dict and item["block"].get("type") in RETIRABLE_TYPES,
                "Only compiler activities and their presentation blocks can be retired")
        require(state_fingerprint(item["block"]) == digests[identity],
                "The retired response/code contract changed")
        retired_ids.add(identity)
    require(len(set(seen_blocks)) == len(seen_blocks) and set(seen_blocks) | retired_ids == set(storage),
            "Block identities changed")
    successors = {item["id"]: item["next"] for item in retired}
    for identity in successors:
        visited = set()
        while identity in successors:
            require(identity not in visited, "Cyclic retired activity successors")
            visited.add(identity)
            identity = successors[identity]
    editorial = meta["editorial"]
    require(type(editorial) is list and len(editorial) <= 100_000, "Malformed editorial proof")
    chains = {}
    for proof in editorial:
        require(type(proof) is dict and set(proof) == {"id", "beforeSha256", "afterSha256"},
                "Malformed editorial proof")
        identity = _identity(proof["id"])
        require(identity in storage, "Unknown editorial identity")
        before, after = _digest(proof["beforeSha256"]), _digest(proof["afterSha256"])
        require(before != after, "An editorial proof must describe a real revision")
        if identity in chains:
            require(chains[identity][-1] == before, "Broken editorial proof chain")
        else:
            chains[identity] = [before]
        require(after not in chains[identity], "Cyclic editorial proof")
        chains[identity].append(after)
    actual = _all_blocks(document)
    for identity, chain in chains.items():
        require(chain[-1] == fingerprint(actual[identity]), "Editorial proof does not describe the current block")
    return mapping


def prepare_v2(document: dict, locale: str, family_documents: dict[str, dict],
               previous: dict | None = None) -> dict:
    """Anchor real language histories, without changing any translated source file.

    Source prose may be revised in its published slots; all moves are an explicit
    subsequent ID-only plan. Full before/after proofs make both one safe delivery.
    """
    require(type(document) is dict and type(family_documents) is dict
            and all(type(source) is dict for source in family_documents.values()),
            "Expected lesson objects for the published language family")
    require(type(locale) is str and locale in LOCALES and locale in family_documents,
            "Missing published locale source")
    require(set(family_documents) <= LOCALES, "Unsupported language family")
    previous = family_documents[locale] if previous is None else previous
    anchored = [source["stateLayout"]["family"] for source in family_documents.values()
                if type(source.get("stateLayout")) is dict and source["stateLayout"].get("version") == 2]
    if anchored:
        check_localized_family(list(family_documents.values()), list(family_documents))
        family_manifest = deepcopy(anchored[0])
    else:
        family_manifest = {
            language: {"sha256": fingerprint(_without_layout(source)),
                       "origin": deepcopy(prepare(source)["stateLayout"]["origin"])}
            for language, source in sorted(family_documents.items())}
    if type(previous.get("stateLayout")) is dict and previous["stateLayout"].get("version") == 2:
        require(type(document.get("stateLayout")) is dict and document["stateLayout"].get("version") == 2,
                "Published v2 metadata cannot be removed or regenerated")
        result = deepcopy(document)
        if result["stateLayout"].get("editorial") == previous["stateLayout"]["editorial"]:
            before, after = _all_blocks(previous), _all_blocks(result)
            for identity, block in before.items():
                require(identity in after, "An existing activity was removed")
                if fingerprint(block) != fingerprint(after[identity]):
                    result["stateLayout"]["editorial"].append({
                        "id": identity, "beforeSha256": fingerprint(block),
                        "afterSha256": fingerprint(after[identity])})
        validate_transition(previous, result)
        return result
    baseline = prepare(previous)
    require(len(_pages(document)) == len(baseline["pages"]), "Historical pages cannot be added or removed")
    require(all(len(a.get("blocks", [])) == len(b.get("blocks", []))
                for a, b in zip(document["pages"], baseline["pages"])),
            "Prepare before adding blocks; use explicit additions with an ID plan")
    result = deepcopy(document)
    result["stateLayout"] = {
        "version": 2, "locale": locale,
        "origin": deepcopy(baseline["stateLayout"]["origin"]),
        "current": deepcopy(baseline["stateLayout"]["current"]),
        "family": family_manifest,
        "additions": [], "editorial": [],
    }
    before, after = _blocks(baseline), _blocks(result)
    result["stateLayout"]["editorial"] = [
        {"id": identity, "beforeSha256": fingerprint(block), "afterSha256": fingerprint(after[identity])}
        for identity, block in before.items() if fingerprint(block) != fingerprint(after[identity])]
    validate_transition(previous, result)
    return result


def _validate_state_layout(document: dict) -> dict[tuple[int, int], tuple[int, int]]:
    pages = _pages(document)
    if "stateLayout" not in document:
        return {(p, b): (p, b) for p, page in enumerate(pages) for b in range(len(page.get("blocks", [])))}
    meta = document["stateLayout"]
    if type(meta) is dict and type(meta.get("version")) is int and meta["version"] == 2:
        return _validate_v2(document)
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


def _validate_presentation_relationships(document: dict) -> None:
    """Windows presentation links use existing identities, never response payloads.

    Empty optional lists retain the native parser's legacy no-op behavior. Any
    actual link needs a fully validated stateLayout, and ordering always follows
    visible positions rather than historical persistence addresses.
    """
    relations = {}
    for field in PRESENTATION_FIELDS:
        if field not in document:
            continue
        value = document[field]
        require(type(value) is list and len(value) <= 100_000,
                f"{field} must be a relationship list")
        relations[field] = value
    if not any(relations.values()):
        return
    require("stateLayout" in document, "Presentation relationships require stable state-layout identities")
    blocks = _blocks(document)
    positions = {identity: (p, b)
                 for p, record in enumerate(document["stateLayout"]["current"])
                 for b, identity in enumerate(record["blocks"])}
    references = set()
    for field, links in relations.items():
        activities = set()
        entries_key = "blocks" if field == "referenceAfter" else "requires"
        for link in links:
            require(type(link) is dict and set(link) == {"activity", entries_key},
                    f"Malformed {field} relationship")
            activity = _identity(link["activity"])
            require(activity not in activities, f"Duplicate {field} activity")
            activities.add(activity)
            require(activity in blocks, f"{field} activity does not exist")
            target = blocks[activity]
            require(target.get("type") == "cpp_compiler", f"{field} activity must be a compiler")
            if field == "referenceAfter":
                require(type(target.get("outputCheck")) is dict,
                        "Reference activity requires an authored output check")
            entries = link[entries_key]
            require(type(entries) is list and 0 < len(entries) <= 100_000,
                    f"{field} relationship needs a nonempty {entries_key} list")
            seen = set()
            for value in entries:
                identity = _identity(value)
                require(identity != activity and identity not in seen,
                        f"Duplicate or self-referencing {field} identity")
                seen.add(identity)
                require(identity in blocks, f"{field} block does not exist")
                block = blocks[identity]
                if field == "referenceAfter":
                    require(identity not in references, "Duplicate reference presentation block")
                    references.add(identity)
                    require(block.get("type") in {"heading", "paragraph", "note", "code"},
                            "An exercise or editor cannot be hidden as a reference")
                    require(positions[identity] > positions[activity],
                            "A reference must follow its activity in visual order")
                else:
                    require(block.get("type") == "cpp_compiler" and type(block.get("outputCheck")) is dict,
                            "An activity prerequisite requires a compiler with an authored output check")
                    require(positions[identity] < positions[activity],
                            "An activity prerequisite must precede its activity in visual order")


def _validate_retirement_history(document: dict) -> None:
    meta = document.get("stateLayout", {})
    if "retirementHistory" not in meta:
        return
    require(meta.get("version") == 2, "Retirement reading history requires stateLayout v2")
    history = meta["retirementHistory"]
    require(type(history) is list and len(history) <= 100_000, "Malformed retirement reading history")
    archived = [entry["id"] for entry in meta.get("retired", [])]
    available = _all_blocks(document)
    page_ids = {record["id"] for record in meta["current"]}
    page_shells = {record["id"]: {key: value for key, value in page.items() if key != "blocks"}
                   for page, record in zip(document["pages"], meta["current"])}
    covered, batches = [], []
    for snapshot in history:
        require(type(snapshot) is dict and set(snapshot) == {
            "retired", "current", "pages", "compilerPuzzlesRequired", "referenceAfter", "activityAfter"},
            "A retirement reading snapshot needs the complete previous pages, layout, flow and relationships")
        require(type(snapshot["compilerPuzzlesRequired"]) is bool, "Historical compiler flow must be boolean")
        ids = snapshot["retired"]
        require(type(ids) is list and ids, "An archival reading snapshot needs its retirement batch")
        for identity in ids:
            _identity(identity)
        require(len(set(ids)) == len(ids) and set(ids) <= set(archived), "Unknown or repeated reading-history retirement")
        pages = _pages(snapshot)
        current = snapshot["current"]
        require(type(current) is list and len(current) == len(pages) == len(page_ids), "Historical reading page count changed")
        seen_pages, seen_blocks = set(), set()
        for page, record in zip(pages, current):
            require(type(record) is dict and set(record) == {"id", "blocks"}, "Malformed historical reading page")
            identity = _identity(record["id"])
            require(identity in page_ids and identity not in seen_pages, "Historical reading page identity changed")
            seen_pages.add(identity)
            require({key: value for key, value in page.items() if key != "blocks"} == page_shells[identity],
                    "Historical reading page metadata changed")
            require(type(record["blocks"]) is list and len(record["blocks"]) == len(page.get("blocks", [])),
                    "Historical reading blocks do not match their identities")
            for block_id, block in zip(record["blocks"], page.get("blocks", [])):
                _identity(block_id)
                require(block_id in available and block_id not in seen_blocks, "Unknown or repeated historical reading block")
                require(state_fingerprint(block) == state_fingerprint(available[block_id]),
                        "Historical reading snapshot changed a response/code contract")
                seen_blocks.add(block_id)
        historical = {"pages": pages, "stateLayout": {"current": current},
                      "referenceAfter": snapshot["referenceAfter"], "activityAfter": snapshot["activityAfter"]}
        _validate_presentation_relationships(historical)
        covered.extend(ids)
        batches.append((set(ids), seen_blocks))
    require(len(set(covered)) == len(covered) and (not covered or covered == archived[-len(covered):]),
            "Retirement reading batches must own an exact suffix of the archived identities")
    active = set(_blocks(document))
    additions = {entry["id"] for entry in meta["additions"]}
    for ids, before in reversed(batches):
        # Later v2 additions did not exist in this presentation. Only an
        # addition may be absent; original identities and this batch remain
        # mandatory. Transition validation separately pins the exact snapshot.
        expected = active | ids
        require(ids <= before <= expected and expected - before <= additions and not active & ids,
                "Historical reading layout does not match its retirement batch")
        active = before


def validate(document: dict) -> dict[tuple[int, int], tuple[int, int]]:
    """Return immutable storage coordinates after validating state and links."""
    if "compilerPuzzlesRequired" in document:
        require(type(document["compilerPuzzlesRequired"]) is bool, "compilerPuzzlesRequired must be boolean")
    mapping = _validate_state_layout(document)
    _validate_presentation_relationships(document)
    _validate_retirement_history(document)
    return mapping


def _transition_metadata(document: dict) -> dict:
    # Only these validated root presentation extensions may change independently
    # of an immutable origin. All other lesson metadata remains protected.
    return {k: v for k, v in document.items()
            if k not in {"pages", "stateLayout"} | PRESENTATION_FIELDS | FLOW_FIELDS}


def reorder(document: dict, plan: list[dict], additions: list[dict] | None = None) -> dict:
    """Move existing page shells and blocks according to an explicit ID-only plan."""
    before = prepare(document)
    pages_by_id, blocks_by_id = {}, {}
    for page, identity in zip(before["pages"], before["stateLayout"]["current"]):
        pages_by_id[identity["id"]] = {key: deepcopy(value) for key, value in page.items() if key != "blocks"}
        blocks_by_id.update(zip(identity["blocks"], deepcopy(page.get("blocks", []))))
    result = deepcopy(before)
    if additions is not None:
        require(before["stateLayout"]["version"] == 2, "Activity additions require stateLayout v2")
        require(type(additions) is list, "Expected an activity-addition list")
        for addition in additions:
            require(type(addition) is dict and set(addition) == {"id", "storage", "progressPage", "block"},
                    "An authoring addition needs id, storage, progressPage and block")
            identity = _identity(addition["id"])
            require(identity not in blocks_by_id, "An addition cannot replace an existing block")
            require(type(addition["block"]) is dict, "A new block must be an object")
            blocks_by_id[identity] = deepcopy(addition["block"])
            result["stateLayout"]["additions"].append({
                "id": identity, "sha256": state_fingerprint(addition["block"]),
                "storage": deepcopy(addition["storage"]), "progressPage": addition["progressPage"]})
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


def retire(document: dict, identities: list[str], *, activity_after: list[dict] | None = None,
           reference_after: list[dict] | None = None,
           compiler_puzzles_required: bool | None = None) -> dict:
    """Archive explicitly selected Spanish activities without recycling their slots.

    Relationship updates are authored by the caller in the same transition. No
    prerequisite is silently removed, and this helper never writes source files.
    """
    validate(document)
    require(document.get("stateLayout", {}).get("version") == 2,
            "Retirement requires an existing stateLayout v2 family")
    require(document["stateLayout"]["locale"] == "es", "Retirement is currently authorized only for Spanish")
    require(type(identities) is list and identities, "Supply a nonempty retirement plan")
    for identity in identities:
        _identity(identity)
    require(len(set(identities)) == len(identities), "Supply a unique retirement plan")
    active = _blocks(document)
    require(set(identities) <= active.keys(), "Only active identities can be retired")
    result = deepcopy(document)
    archive = result["stateLayout"].setdefault("retired", [])
    retiring = set(identities)
    snapshot = {"retired": [], "current": deepcopy(document["stateLayout"]["current"]),
                "pages": deepcopy(document["pages"]),
                "compilerPuzzlesRequired": document.get("compilerPuzzlesRequired", False),
                "referenceAfter": deepcopy(document.get("referenceAfter", [])),
                "activityAfter": deepcopy(document.get("activityAfter", []))}
    for page, record in zip(result["pages"], result["stateLayout"]["current"]):
        old_ids, old_blocks = record["blocks"], page.get("blocks", [])
        for index, identity in enumerate(old_ids):
            if identity not in retiring:
                continue
            require(old_blocks[index].get("type") in RETIRABLE_TYPES,
                    "Only compiler activities and their presentation blocks can be retired")
            successor = next((candidate for candidate in old_ids[index + 1:] if candidate not in retiring), "")
            archive.append({"id": identity, "page": record["id"], "next": successor,
                            "block": deepcopy(old_blocks[index])})
            snapshot["retired"].append(identity)
        page["blocks"] = [block for identity, block in zip(old_ids, old_blocks) if identity not in retiring]
        record["blocks"] = [identity for identity in old_ids if identity not in retiring]
    result["stateLayout"].setdefault("retirementHistory", []).append(snapshot)
    for field, relationships in (("activityAfter", activity_after), ("referenceAfter", reference_after)):
        if relationships is not None:
            result[field] = deepcopy(relationships)
    if compiler_puzzles_required is not None:
        require(type(compiler_puzzles_required) is bool, "compilerPuzzlesRequired must be boolean")
        result["compilerPuzzlesRequired"] = compiler_puzzles_required
    validate_transition(document, result)
    return result


def _validate_transition_v2(previous: dict, current: dict) -> None:
    validate(current)
    baseline = prepare(previous)
    old_meta, meta = baseline["stateLayout"], current["stateLayout"]
    require(meta["origin"] == old_meta["origin"], "The immutable origin was changed; do not reset the baseline")
    old_v2 = old_meta["version"] == 2
    if old_v2:
        require(meta["locale"] == old_meta["locale"], "The historical locale cannot be renamed")
        require(meta["family"] == old_meta["family"], "The immutable historical language family changed")
    else:
        own = meta["family"][meta["locale"]]
        require(own["sha256"] == fingerprint(_without_layout(previous)) and own["origin"] == old_meta["origin"],
                "The historical locale is not anchored to the published source")
    require(_transition_metadata(baseline) == _transition_metadata(current),
            "A reorder cannot modify lesson metadata")
    require(not baseline.get("compilerPuzzlesRequired", False) or current.get("compilerPuzzlesRequired", False),
            "Published mandatory compiler puzzles cannot be silently disabled")
    def shells(document):
        return {record["id"]: {k: v for k, v in page.items() if k != "blocks"}
                for page, record in zip(document["pages"], document["stateLayout"]["current"])}
    require(shells(baseline) == shells(current), "Page metadata was changed")
    old_additions = old_meta["additions"] if old_v2 else []
    require(meta["additions"][:len(old_additions)] == old_additions,
            "Activity additions are append-only; identities and storage cannot be recycled")
    old_editorial = old_meta["editorial"] if old_v2 else []
    require(meta["editorial"][:len(old_editorial)] == old_editorial,
            "Editorial proof is append-only; the published provenance cannot be replaced")
    old_retired = old_meta.get("retired", []) if old_v2 else []
    retired = meta.get("retired", [])
    require(retired[:len(old_retired)] == old_retired,
            "Retired activity archives are append-only and cannot be restored or rewritten")
    before, after = _all_blocks(baseline), _all_blocks(current)
    old_positions = {identity: (record["id"], index, record["blocks"])
                     for record in old_meta["current"] for index, identity in enumerate(record["blocks"])}
    newly_retired = {item["id"] for item in retired[len(old_retired):]}
    old_history = old_meta.get("retirementHistory", []) if old_v2 else []
    history = meta.get("retirementHistory", [])
    require(history[:len(old_history)] == old_history, "Retirement reading snapshots are append-only")
    appended_history = history[len(old_history):]
    require(not newly_retired or appended_history,
            "A new retirement must archive its previous reading presentation")
    if appended_history:
        require(len(appended_history) == 1 and newly_retired,
                "One transition can archive only its actual previous reading layout")
        snapshot = appended_history[0]
        expected = {"retired": [item["id"] for item in retired[len(old_retired):]],
                    "current": old_meta["current"], "pages": baseline["pages"],
                    "compilerPuzzlesRequired": baseline.get("compilerPuzzlesRequired", False),
                    "referenceAfter": baseline.get("referenceAfter", []),
                    "activityAfter": baseline.get("activityAfter", [])}
        require(fingerprint(snapshot) == fingerprint(expected),
                "Retirement reading snapshot does not match the complete previous published presentation")
    for item in retired[len(old_retired):]:
        identity = item["id"]
        require(identity in old_positions, "A retirement must archive an existing active block")
        require(fingerprint(item["block"]) == fingerprint(before[identity]),
                "A retired block must preserve its complete published snapshot")
        page, index, siblings = old_positions[identity]
        successor = next((candidate for candidate in siblings[index + 1:] if candidate not in newly_retired), "")
        require(item["page"] == page and item["next"] == successor,
                "A retirement must preserve its actual previous position and successor")
    require(before.keys() <= after.keys(), "An existing activity was removed")
    appended = {}
    for proof in meta["editorial"][len(old_editorial):]:
        require(proof["id"] in before, "New activities cannot claim historical editorial provenance")
        appended.setdefault(proof["id"], []).append(proof)
    owners = {}
    for identity, block in before.items():
        owners.setdefault(fingerprint(block), set()).add(identity)
    for identity, block in before.items():
        actual = after[identity]
        require(state_fingerprint(block) == state_fingerprint(actual),
                "An existing response/code contract changed")
        if str(block.get("type", "")).startswith("math_"):
            serialize = lambda value: json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
            require(serialize(block) == serialize(actual),
                    "Mathematical payloads must preserve their historical JSON key order and values")
        old_hash, new_hash = fingerprint(block), fingerprint(actual)
        proofs = appended.get(identity, [])
        if old_hash == new_hash:
            require(not proofs, "An unchanged block cannot acquire an invented editorial history")
        else:
            require(proofs and proofs[0]["beforeSha256"] == old_hash
                    and proofs[-1]["afterSha256"] == new_hash,
                    "A simultaneous editorial revision needs its explicit published before/after proof")
        require(new_hash not in owners or identity in owners[new_hash],
                "Published content was assigned to another identity without a verified move")


def validate_transition(previous: dict, current: dict) -> None:
    """Never authorize a renamed origin or changed answer while allowing a reorder."""
    require("stateLayout" in current, "A reordered lesson needs an identity manifest")
    if type(current["stateLayout"]) is dict and current["stateLayout"].get("version") == 2:
        _validate_transition_v2(previous, current)
        return
    require(not (type(previous.get("stateLayout")) is dict and previous["stateLayout"].get("version") == 2),
            "Published v2 metadata cannot be removed or downgraded")
    baseline = prepare(previous)
    validate(current)
    require(current["stateLayout"]["origin"] == baseline["stateLayout"]["origin"],
            "The immutable origin was changed; do not reset the baseline")
    previous_other = _transition_metadata(baseline)
    current_other = _transition_metadata(current)
    require(previous_other == current_other, "A reorder cannot modify lesson metadata")
    require(not baseline.get("compilerPuzzlesRequired", False) or current.get("compilerPuzzlesRequired", False),
            "Published mandatory compiler puzzles cannot be silently disabled")
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
        validate(previous)
        validate(current)
        return current
    validate_transition(previous, current)
    old = prepare(previous)
    blocks = _all_blocks(current)
    page_by_id = {record["id"]: page for page, record in zip(current["pages"], current["stateLayout"]["current"])}
    result = deepcopy(current)
    result["pages"] = [{**deepcopy(page_by_id[record["id"]]),
                        "blocks": [deepcopy(blocks[i]) for i in record["blocks"]]}
                       for record in old["stateLayout"]["current"]]
    return result


def original_projection(document: dict) -> dict:
    """In-memory lookup of historical sources for existing curricular exceptions.

    This is neither a new lesson nor a regenerated manifest. New activities are
    intentionally absent from the original source lookup, never from validation.
    """
    if "stateLayout" not in document:
        return document
    validate(document)
    blocks = _all_blocks(document)
    shells = {record["id"]: {key: value for key, value in page.items() if key != "blocks"}
              for page, record in zip(document["pages"], document["stateLayout"]["current"])}
    result = deepcopy(_without_layout(document))
    result["pages"] = [{**deepcopy(shells[record["id"]]),
                        "blocks": [deepcopy(blocks[block["id"]]) for block in record["blocks"]]}
                       for record in document["stateLayout"]["origin"]]
    return result


def lineage(document: dict) -> tuple:
    metadata = prepare(document)["stateLayout"]
    return tuple((page["id"], tuple(block["id"] for block in page["blocks"]))
                 for page in metadata["origin"])


def check_localized_family(documents: list[dict], locales: list[str] | None = None) -> None:
    """Call with every shipped translation before publishing the first adoption."""
    require(bool(documents), "No lessons to verify")
    v2 = [d for d in documents if type(d.get("stateLayout")) is dict and d["stateLayout"].get("version") == 2]
    if v2:
        for document in documents:
            validate(document)
        require(len({d["id"] for d in documents}) == 1, "Not the same lesson")
        family = v2[0]["stateLayout"]["family"]
        require(all(d["stateLayout"]["family"] == family for d in v2),
                "Localized v2 maps disagree on the historical language family")
        if locales is None:
            require(len(v2) == len(documents), "Historical translations need their explicit source locales")
            locales = [d["stateLayout"]["locale"] for d in documents]
        require(len(locales) == len(documents) and len(set(locales)) == len(locales),
                "Duplicate or missing localized lesson source")
        require(set(locales) == set(family), "The historical language family must include every shipped translation")
        for locale, document in zip(locales, documents):
            require(locale in LOCALES, "Unsupported historical locale")
            meta = document.get("stateLayout")
            if meta is None and "stateLayout" not in document:
                require(fingerprint(document) == family[locale]["sha256"]
                        and prepare(document)["stateLayout"]["origin"] == family[locale]["origin"],
                        "An unmapped historical translation changed; keep it intact or adopt its own v2 map")
            else:
                require(meta["origin"] == family[locale]["origin"], "A translation rewrote its historical origin")
                if meta["version"] == 2:
                    require(meta["locale"] == locale, "The map locale does not match its lesson source")
        # Slots of additions must not acquire independent owners in another map.
        slots, owners = {}, {}
        for document in v2:
            blocks = _all_blocks(document)
            for addition in document["stateLayout"]["additions"]:
                slot = (addition["storage"]["page"], addition["storage"]["block"])
                require(slot not in slots or slots[slot] == addition["id"],
                        "Localized new activities collide in shared storage")
                slots[slot] = addition["id"]
                block = blocks[addition["id"]]
                descriptor = slot, block.get("type"), block.get("id")
                require(addition["id"] not in owners or owners[addition["id"]] == descriptor,
                        "A localized new identity changed storage or activity ownership")
                owners[addition["id"]] = descriptor
        return
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
    for path, document in documents.items():
        if (PRESENTATION_FIELDS | FLOW_FIELDS) & document.keys():
            try:
                validate(document)
            except (ValueError, KeyError, TypeError) as exc:
                raise LayoutError(f"Invalid presentation relationships in {path}: {exc}") from exc
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
            locales = [PurePosixPath(path).relative_to(CONTENT_DIRECTORY).parts[0] for path, _ in members]
            check_localized_family([document for _, document in members], locales)
            v2 = [document for _, document in members
                  if type(document.get("stateLayout")) is dict and document["stateLayout"].get("version") == 2]
            if v2:
                manifest = v2[0]["stateLayout"]["family"]
                for locale, source in manifest.items():
                    relative = f"{CONTENT_DIRECTORY}/{locale}/{family}"
                    require(relative in prior_paths, "A historical language source is absent from the published baseline")
                    previous = prior_mapped.get(relative) or _git_document(repository, commit, relative)
                    old_meta = previous.get("stateLayout")
                    if type(old_meta) is dict and old_meta.get("version") == 2:
                        require(source == old_meta["family"][locale], "The published language provenance was replaced")
                    else:
                        require(source["sha256"] == fingerprint(_without_layout(previous))
                                and source["origin"] == prepare(previous)["stateLayout"]["origin"],
                                "A historical language source is not anchored to the published baseline")
        except (ValueError, KeyError, TypeError) as exc:
            raise LayoutError(family + ": " + str(exc)) from exc
        checked_families += 1
    return {"base_commit": commit, "mapped_lessons": len(mapped), "families": checked_families}


def published_family(source: Path, locale: str, base_ref: str) -> tuple[dict, dict[str, dict]]:
    """Read original assets directly from Git; do not export backup copies."""
    source = source.resolve()
    result = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=source.parent,
                            capture_output=True, text=True, encoding="utf-8")
    require(result.returncode == 0, "The source must be an asset in its existing Git checkout")
    repository = Path(result.stdout.strip()).resolve()
    try:
        relative = source.relative_to(repository).as_posix()
        asset = PurePosixPath(relative).relative_to(CONTENT_DIRECTORY)
    except ValueError as exc:
        raise LayoutError("The source must be a curricular lesson asset") from exc
    require(len(asset.parts) >= 2 and asset.parts[0] == locale,
            "--locale must match the curricular source path")
    commit = resolve_base_commit(repository, base_ref)
    tail = PurePosixPath(*asset.parts[1:]).as_posix()
    existing = _lesson_paths_at(repository, commit)
    family = {}
    for language in sorted(LOCALES):
        path = f"{CONTENT_DIRECTORY}/{language}/{tail}"
        if path in existing:
            family[language] = _git_document(repository, commit, path)
    require(locale in family, "The published baseline does not contain this locale")
    return family[locale], family


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "reorder", "check", "check-tree"))
    parser.add_argument("source", type=Path, help="Lesson JSON; repository directory for check-tree")
    parser.add_argument("--plan", type=Path, help="JSON list of stable page/block IDs in display order")
    parser.add_argument("--output", type=Path, help="New path; never overwrite a Work/Codex file")
    parser.add_argument("--previous", type=Path, help="Required published lesson for prepare/reorder/check")
    parser.add_argument("--base-ref", help="Required verified remote HEAD for check-tree")
    parser.add_argument("--locale", choices=sorted(LOCALES), help="Use v2 and read all published language assets from Git")
    parser.add_argument("--additions", type=Path,
                        help="Authoring JSON list: id, storage, progressPage and block; requires v2 and --plan")
    args = parser.parse_args()
    try:
        if args.action == "check-tree":
            require(args.base_ref is not None, "check-tree requires --base-ref with the verified remote HEAD")
            report = check_repository(args.source, args.base_ref)
            print(f"State transitions and shipped translations checked against {report['base_commit']}: "
                  f"{report['mapped_lessons']} mapped lessons in {report['families']} families. "
                  "This does not verify an Android build or device execution.")
            return
        source = load(args.source)
        require(type(source) is dict, "A lesson must be a JSON object")
        locale = args.locale
        if locale is None and type(source.get("stateLayout")) is dict and source["stateLayout"].get("version") == 2:
            locale = source["stateLayout"]["locale"]
        if args.base_ref is not None and locale is not None:
            require(args.previous is None, "Use either Git --base-ref or a manual --previous, not both")
            previous, family = published_family(args.source, locale, args.base_ref)
        else:
            require(args.previous is not None,
                    "prepare/reorder/check requires --previous from the verified published commit, "
                    "or --locale with Git --base-ref")
            require(args.locale is None, "First v2 adoption uses --locale and Git --base-ref")
            previous, family = load(args.previous), None
        require(args.additions is None or args.plan is not None, "Activity additions require an explicit --plan")
        if args.action == "prepare":
            result = prepare_v2(source, locale, family, previous) if family is not None else prepare(source)
            if args.plan is not None:
                result = reorder(result, load(args.plan), load(args.additions) if args.additions else None)
        elif args.action == "reorder":
            require(args.plan is not None, "reorder requires --plan")
            result = reorder(source, load(args.plan), load(args.additions) if args.additions else None)
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
    except (LayoutError, OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(1, f"Not completed: {exc}\n")


if __name__ == "__main__":
    main()

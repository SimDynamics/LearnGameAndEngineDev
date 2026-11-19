"""Read-only authoring contract for a mathematical matching activity's public text.

An author's new identifiers can appear in a matching puzzle's prompt, feedback
and source cards. Their visible wording is not a new evaluated answer. The
complete historical puzzle (including matching IDs and expected mapping) must
remain the evaluation source. This module drafts evidence only; the Python and
Windows production stateLayout loaders must separately support the journal
before it can be published.
"""
from __future__ import annotations

from copy import deepcopy
import re

import stable_state_layout as state
import text_semantics


class MatchingTextError(ValueError):
    """The proposed text is not an authenticated presentation-only revision."""


def _matching_text_fields(block: dict) -> dict[str, str]:
    """List the supported *visible* text paths, never answer or validator data."""
    if type(block) is not dict or block.get("type") != "math_puzzle":
        raise MatchingTextError("Text review requires an existing math_puzzle block")
    responses = block.get("responses")
    if type(responses) is not list or not responses or not all(
            type(item) is dict and item.get("type") == "math_matching_drag"
            for item in responses):
        raise MatchingTextError("Text review requires matching-drag responses")
    result = {}
    for path in ("/prompt", "/feedback/correct", "/feedback/incorrect"):
        node = _get(block, path)
        if type(node) is str:
            result[path] = node
    for r, response in enumerate(responses):
        if type(response.get("label")) is str:
            result[f"/responses/{r}/label"] = response["label"]
        for collection in ("sources", "targets"):
            cards = response.get(collection)
            if type(cards) is not list:
                raise MatchingTextError("A matching response requires card lists")
            for index, card in enumerate(cards):
                if type(card) is not dict:
                    raise MatchingTextError("A matching card must retain its original structure")
                label = card.get("label")
                if type(label) is str:
                    result[f"/responses/{r}/{collection}/{index}/label"] = label
                content = card.get("content", [])
                if type(content) is not list:
                    raise MatchingTextError("A matching card has an invalid presentation")
                for c, fragment in enumerate(content):
                    if (type(fragment) is dict and fragment.get("type") == "text"
                            and type(fragment.get("text")) is str):
                        result[f"/responses/{r}/{collection}/{index}/content/{c}/text"] = fragment["text"]
    return result


def _get(block: dict, path: str):
    node = block
    for segment in path.split("/")[1:]:
        node = node[int(segment)] if type(node) is list else node[segment]
    return node


def _set(block: dict, path: str, text: str) -> None:
    node = block
    segments = path.split("/")[1:]
    for segment in segments[:-1]:
        node = node[int(segment)] if type(node) is list else node[segment]
    if type(node) is list:
        node[int(segments[-1])] = text
    else:
        node[segments[-1]] = text


def require_matching_text_only(before: dict, after: dict, declared_paths: list[str]) -> list[str]:
    """Verify exactly the declared strings change; no grader/order/schema edits."""
    if type(declared_paths) is not list or not declared_paths or len(declared_paths) > 128:
        raise MatchingTextError("Matching text needs 1..128 declared visible fields")
    if (any(type(path) is not str or not path.startswith("/")
            for path in declared_paths)
            or declared_paths != sorted(set(declared_paths))):
        raise MatchingTextError("Matching paths must be unique, canonical and ordered")
    original = _matching_text_fields(before)
    proposed = _matching_text_fields(after)
    if original.keys() != proposed.keys() or not set(declared_paths) <= original.keys():
        raise MatchingTextError("Matching source, card or public text paths changed")
    actual_changes = sorted(path for path in original
                            if original[path] != proposed[path])
    if actual_changes != declared_paths:
        raise MatchingTextError("A matching review changed an undeclared field or omitted a change")
    if any(not proposed[path].strip() for path in declared_paths):
        raise MatchingTextError("Matching review cannot create an empty visible label")
    # If the original source card echoed the content literally, the revised
    # source must preserve that synchronization. IDs still own the comparison.
    for response_index, response in enumerate(before["responses"]):
        for group in ("sources", "targets"):
            for card_index, card in enumerate(response[group]):
                prefix = f"/responses/{response_index}/{group}/{card_index}"
                label = prefix + "/label"
                content_path = prefix + "/content/0/text"
                if (label in original and content_path in original
                        and original[label] == original[content_path]
                        and proposed[label] != proposed[content_path]):
                    raise MatchingTextError("Matching card label and its source text disagree")
    restored = deepcopy(after)
    for path in declared_paths:
        _set(restored, path, original[path])
    # The full ordered JSON round-trip also protects numeric spelling/type,
    # array order, validator.expected, IDs, source/target cardinality and
    # the student's representation. Equality alone would coerce True == 1.
    if state._math_ordered_json(restored) != state._math_ordered_json(before):
        raise MatchingTextError("Matching review changed grading, JSON order or other data")
    return declared_paths


def _markers(document: dict, identity: str) -> dict[str, dict]:
    overlay = document.get(text_semantics.ROOT_FIELD, {})
    if type(overlay) is not dict or type(overlay.get("fields", [])) is not list:
        raise MatchingTextError("Malformed public-text semantics")
    return {entry["path"]: entry for entry in overlay.get("fields", [])
            if type(entry) is dict and entry.get("block") == identity}


def preview_math_matching_text_review(
    previous: dict, identity: str,
    replacement: dict[str, str], spans: dict[str, list[dict]],
) -> tuple[dict, dict]:
    """Build one unpublished candidate and authentic journal from a real v2 owner."""
    state.validate(previous)
    metadata = previous["stateLayout"]
    if (metadata["version"] != 2 or metadata["locale"] != "es"
            or type(replacement) is not dict or not replacement
            or type(spans) is not dict):
        raise MatchingTextError("A matching review needs a Spanish v2 authoring plan")
    before = state._blocks(previous).get(identity)
    if before is None:
        raise MatchingTextError("Unknown or inactive matching response owner")
    if any(item["id"] == identity for item in metadata.get("mathTextRevisions", [])):
        raise MatchingTextError("A matching review cannot borrow a separate prompt archive")
    if any(item["id"] == identity for item in metadata.get("mathMatchingTextRevisions", [])):
        raise MatchingTextError("The published matching review cannot be overwritten")
    if any(type(path) is not str or type(value) is not str
           for path, value in replacement.items()):
        raise MatchingTextError("Matching paths and replacements must be strings")
    paths = sorted(replacement)
    revised = deepcopy(before)
    allowed = _matching_text_fields(before)
    if not replacement.keys() <= allowed.keys():
        raise MatchingTextError("A replacement cannot edit matching answer identities")
    for path, value in replacement.items():
        _set(revised, path, value)
    require_matching_text_only(before, revised, paths)

    public = text_semantics.public_text_fields(before)
    revised_public = text_semantics.public_text_fields(revised)
    marked = set(paths) & set(public)
    if marked != set(spans) or not marked <= set(revised_public):
        raise MatchingTextError("Every changed narrated field needs authored UTF-16 spans")
    before_marks = _markers(previous, identity)
    for path in marked:
        if path in before_marks and before_marks[path]["spans"] and not spans[path]:
            raise MatchingTextError("A marked passage cannot silently lose all its highlighting")
    result = deepcopy(previous)
    block = state._blocks(result)[identity]
    for path, value in replacement.items():
        _set(block, path, value)
    overlay = result.setdefault(text_semantics.ROOT_FIELD,
                                {"version": 1, "fields": []})
    new_marks = {}
    for path in sorted(marked):
        if type(spans[path]) is not list:
            raise MatchingTextError("Authored span selections must be a list")
        descriptor = text_semantics.make_field(result, identity, path, spans[path])
        new_marks[path] = descriptor
        matches = [i for i, field in enumerate(overlay["fields"])
                   if field.get("block") == identity and field.get("path") == path]
        if len(matches) > 1:
            raise MatchingTextError("Duplicate historical text markers")
        if matches:
            overlay["fields"][matches[0]] = descriptor
        else:
            overlay["fields"].append(descriptor)
    try:
        text_semantics.validate(result)
    except (TypeError, ValueError, KeyError) as exc:
        raise MatchingTextError("A revised source has invalid text marks") from exc

    proof = {
        "id": identity,
        "rawJson": state._math_ordered_json(before),
        "paths": paths,
        "semanticBefore": {p: deepcopy(before_marks.get(p)) for p in paths},
        "semanticAfter": {p: deepcopy(new_marks.get(p)) for p in paths},
        "editorialIndex": len(metadata["editorial"]),
        "historySha256": state.fingerprint({
            "lessonId": previous["id"], "locale": metadata["locale"],
            "editorial": metadata["editorial"]}),
        "beforeSha256": state.fingerprint(before),
        "afterSha256": state.fingerprint(block),
    }
    result["stateLayout"].setdefault("mathMatchingTextRevisions", []).append(proof)
    result["stateLayout"]["editorial"].append({
        "id": identity, "beforeSha256": proof["beforeSha256"],
        "afterSha256": proof["afterSha256"]})
    return result, proof


def verify_matching_preview(previous: dict, draft: dict, proof: dict) -> bool:
    """Authenticate the proposal in memory; production does not read it yet."""
    state.validate(previous)
    if type(proof) is not dict or set(proof) != {
        "id", "rawJson", "paths", "semanticBefore", "semanticAfter",
        "editorialIndex", "historySha256", "beforeSha256", "afterSha256"
    }:
        raise MatchingTextError("Incomplete or forged matching review journal")
    old = state._blocks(previous).get(proof["id"])
    current = state._blocks(draft).get(proof["id"])
    if type(old) is not dict or type(current) is not dict:
        raise MatchingTextError("Matching review owner moved or disappeared")
    require_matching_text_only(old, current, proof["paths"])
    original_marks = _markers(previous, proof["id"])
    revised_marks = _markers(draft, proof["id"])
    if (proof["semanticBefore"] != {
            path: original_marks.get(path) for path in proof["paths"]}
            or proof["semanticAfter"] != {
                path: revised_marks.get(path) for path in proof["paths"]}):
        raise MatchingTextError("A matching review swapped its original or current semantic marks")
    if (proof["rawJson"] != state._math_ordered_json(old)
            or proof["beforeSha256"] != state.fingerprint(old)
            or proof["afterSha256"] != state.fingerprint(current)):
        raise MatchingTextError("A matching review changed its complete historical owner")
    meta = previous["stateLayout"]
    if (proof["editorialIndex"] != len(meta["editorial"])
            or proof["historySha256"] != state.fingerprint({
                "lessonId": previous["id"], "locale": meta["locale"],
                "editorial": meta["editorial"]})):
        raise MatchingTextError("A matching review borrowed a foreign publication prefix")
    restored = deepcopy(draft)
    if type(restored.get("stateLayout")) is not dict:
        raise MatchingTextError("Missing draft's stable state map")
    revisions = restored["stateLayout"].pop("mathMatchingTextRevisions", None)
    if not isinstance(revisions, list) or len(revisions) != 1 or revisions[0] != proof:
        raise MatchingTextError("Matching review journal differs from its declaration")
    editorial = restored["stateLayout"]["editorial"]
    if len(editorial) != len(meta["editorial"]) + 1:
        raise MatchingTextError("Matching review has an unexpected editorial history")
    last = editorial.pop()
    if last != {"id": proof["id"],
                "beforeSha256": proof["beforeSha256"],
                "afterSha256": proof["afterSha256"]}:
        raise MatchingTextError("Matching review lacks its exact editorial edge")
    for page, record in zip(restored["pages"], meta["current"]):
        for i, identity in enumerate(record["blocks"]):
            if identity == proof["id"]:
                page["blocks"][i] = deepcopy(old)
    overlay = restored.get(text_semantics.ROOT_FIELD)
    if overlay is not None:
        for path in proof["paths"]:
            if path not in text_semantics.public_text_fields(old):
                continue  # Canvas-only labels have no semantic field marker.
            fields = overlay["fields"]
            for i in reversed(range(len(fields))):
                if fields[i].get("block") == proof["id"] and fields[i].get("path") == path:
                    fields.pop(i)
            previous_marker = original_marks.get(path)
            if previous_marker is not None:
                fields.append(deepcopy(previous_marker))
        # Compare sets by (owner,path) to avoid mistaking insertion order for
        # meaning; the actual document still preserves original field ordering.
        before_overlay = previous.get(text_semantics.ROOT_FIELD)
        if before_overlay is not None:
            reordered = sorted(overlay["fields"],key=lambda f:(f["block"],f["path"]))
            expected = sorted(before_overlay["fields"],key=lambda f:(f["block"],f["path"]))
            if reordered != expected:
                raise MatchingTextError("Matching review rewrote another owner's text semantics")
            restored[text_semantics.ROOT_FIELD] = deepcopy(before_overlay)
        elif overlay.get("fields") or overlay.get("rootFields"):
            raise MatchingTextError("Matching review inserted unrelated text semantics")
        else:
            restored.pop(text_semantics.ROOT_FIELD,None)
    if (state.fingerprint(restored) != state.fingerprint(previous)
            or state._math_ordered_json(restored) != state._math_ordered_json(previous)):
        raise MatchingTextError("A matching review altered other lessons, progress or grader data")
    return True


def authenticated_matching_text_originals(
    document: dict, active: dict[str, dict], digests: dict[str, str],
) -> dict[str, dict]:
    """Authenticate recognized text-only archives before grading projection.

    The journal describes one closed editorial revision per active matching
    owner. It authenticates UTF-16 markers and the prior ordered mathematical
    payload without introducing a new saved response identity.
    """
    metadata = document["stateLayout"]
    records = metadata.get("mathMatchingTextRevisions", [])
    if type(records) is not list or len(records) > 64:
        raise MatchingTextError("Matching text archive count is invalid")
    if not records:
        return {}
    if metadata.get("version") != 2 or metadata.get("locale") != "es":
        raise MatchingTextError("Matching text archives require Spanish stateLayout v2")
    archive_keys = {
        "id", "rawJson", "paths", "semanticBefore", "semanticAfter",
        "editorialIndex", "historySha256", "beforeSha256", "afterSha256",
    }
    other_math = {
        item["id"] for item in metadata.get("mathTextRevisions", [])
    }
    originals = {}
    for item in records:
        if type(item) is not dict or set(item) != archive_keys:
            raise MatchingTextError("Malformed matching text archive")
        identity = item["id"]
        if (type(identity) is not str or identity not in active or identity not in digests
                or identity in originals or identity in other_math):
            raise MatchingTextError("Matching archive has an unknown, duplicate or incompatible owner")
        raw = item["rawJson"]
        if type(raw) is not str or len(raw.encode("utf-8")) > 32 * 1024 * 1024:
            raise MatchingTextError("Matching archive requires bounded original raw JSON")
        try:
            previous = state.parse_json(raw)
        except (ValueError, TypeError, KeyError) as error:
            raise MatchingTextError("Matching archive contains invalid original JSON") from error
        if state._math_ordered_json(previous) != raw:
            raise MatchingTextError("Matching archive rewrote JSON property order or numbers")
        if state.state_fingerprint(previous) != digests[identity]:
            raise MatchingTextError("Matching archive differs from the original answer signature")
        current = active[identity]
        require_matching_text_only(previous, current, item["paths"])
        if (item["beforeSha256"] != state.fingerprint(previous)
                or item["afterSha256"] != state.fingerprint(current)):
            raise MatchingTextError("Matching archive hashes do not match its complete block pair")
        index = item["editorialIndex"]
        editorial = metadata["editorial"]
        if type(index) is not int or not 0 <= index < len(editorial):
            raise MatchingTextError("Matching archive has an invalid editorial position")
        edge = {
            "id": identity,
            "beforeSha256": item["beforeSha256"],
            "afterSha256": item["afterSha256"],
        }
        if (editorial[index] != edge or
                item["historySha256"] != state.fingerprint({
                    "lessonId": document["id"], "locale": metadata["locale"],
                    "editorial": editorial[:index]})):
            raise MatchingTextError("Matching archive borrowed an unrelated editorial proof")
        if any(proof["id"] == identity for proof in editorial[index + 1:]):
            raise MatchingTextError("A reviewed matching owner needs a single final text revision")

        if (type(item["semanticBefore"]) is not dict or
                type(item["semanticAfter"]) is not dict or
                set(item["semanticBefore"]) != set(item["paths"]) or
                set(item["semanticAfter"]) != set(item["paths"])):
            raise MatchingTextError("Matching archive needs one old/new semantic pair per text path")
        before_public = text_semantics.public_text_fields(previous)
        after_public = text_semantics.public_text_fields(current)
        changed_paths = item["paths"]
        current_markers = _markers(document, identity)
        for path in changed_paths:
            earlier = item["semanticBefore"][path]
            revised = item["semanticAfter"][path]
            if path not in before_public:
                if earlier is not None or revised is not None:
                    raise MatchingTextError("A Canvas-only card label cannot forge narration marks")
                continue
            if path not in after_public or revised is None:
                raise MatchingTextError("A changed narrated field needs its current marker")
            if (earlier is not None and
                    (type(earlier) is not dict or
                     set(earlier) != {"block", "path", "sourceSha256", "spans"})):
                raise MatchingTextError("Malformed original matching source descriptor")
            if (type(revised) is not dict or
                    set(revised) != {"block", "path", "sourceSha256", "spans"} or
                    current_markers.get(path) != revised):
                raise MatchingTextError("Matching archive is missing its authenticated current descriptor")
            for marker, text in ((earlier, before_public[path]),
                                 (revised, after_public[path])):
                if marker is None:
                    continue
                if marker["block"] != identity or marker["path"] != path:
                    raise MatchingTextError("A matching text descriptor belongs to another field")
                try:
                    text_semantics._descriptor(marker, text)
                except (ValueError, TypeError, KeyError) as error:
                    raise MatchingTextError("A matching field has invalid UTF-16 source marks") from error
            if earlier is not None and earlier["spans"] and not revised["spans"]:
                raise MatchingTextError("A matching review cannot discard marked technical spans")
        originals[identity] = previous
    return originals


def historical_matching_text_semantics(
    document: dict, overlay: dict | None,
    *, identities: set[str] | None = None,
) -> dict | None:
    """Restore only fields described by authenticated matching-text revisions.

    A historical reader must retain the original audible emphasis and reading
    ranges, not assign already-read coverage to newly edited passages. Card
    labels unsupported by the narrator have no textSemantics descriptors.
    """
    result = deepcopy(overlay)
    if result is None:
        return None
    if type(result) is not dict or type(result.get("fields")) is not list:
        raise MatchingTextError("Historical matching text overlay is invalid")
    for journal in document.get("stateLayout", {}).get("mathMatchingTextRevisions", []):
        if identities is not None and journal["id"] not in identities:
            continue
        for path in journal["paths"]:
            old = journal["semanticBefore"][path]
            new = journal["semanticAfter"][path]
            if old is None and new is None:
                continue
            matches = [i for i, entry in enumerate(result["fields"])
                       if entry.get("block") == journal["id"]
                       and entry.get("path") == path]
            if len(matches) > 1:
                raise MatchingTextError("Historical matching text has duplicate public owners")
            if matches:
                index = matches[0]
                if old is None:
                    result["fields"].pop(index)
                else:
                    result["fields"][index] = deepcopy(old)
            elif old is not None:
                result["fields"].append(deepcopy(old))
    return result if result["fields"] or result.get("rootFields") else None

"""Preview an exact C++ rename after an already published source reformat.

READ ONLY. A preview is not a recognized stateLayout archive, a curriculum
publication, a completed lesson or a historical migration. The Python and
native loaders must independently authorize a future revision record.
"""
from __future__ import annotations

from copy import deepcopy

import code_format_compat as source_format
import identifier_rename_compat as rename
import stable_state_layout as state


class RenamePreviewError(ValueError):
    pass


def preview_published_rename(document: dict, identity: str, proposed_block: dict,
                             names: dict[str, dict[str, str]],
                             counts: dict[str, dict[str, int]]) -> dict:
    """Validate provenance and return an in-memory archival drafting packet.

    This deliberately requires a real published v2 Spanish lesson and the
    current complete source-format predecessor for the owner. It does not
    mutate the supplied documents or add unsupported layout metadata.
    """
    state.validate(document)
    meta = document["stateLayout"]
    if meta["version"] != 2 or meta["locale"] != "es":
        raise RenamePreviewError("Only an authenticated Spanish v2 owner can be previewed")
    blocks = state._blocks(document)
    if identity not in blocks or identity not in {
            value for page in meta["current"] for value in page["blocks"]}:
        raise RenamePreviewError("Rename preview requires an existing active stable owner")
    old = blocks[identity]
    if old.get("type") not in {
            "code", "cpp_compiler", "output_prediction", "fill_code",
            "inline_fill_code", "order_code", "drag_fill_code"}:
        raise RenamePreviewError("Only an existing C++ curricular source can be renamed")
    records = [item for item in meta.get("sourceFormatRevisions", [])
               if item["id"] == identity]
    if len(records) != 1:
        raise RenamePreviewError(
            "This preview requires the owner's exact prior source-format archive")
    predecessor = records[0]["block"]
    if not isinstance(proposed_block, dict):
        raise RenamePreviewError("The proposed owner must be a complete authored block")
    try:
        changed = rename.require_declared_cpp_renames(old, proposed_block, names, counts)
        # Independent verification of the already published reformat. The
        # rename comes AFTER its active, formatted source, never before it.
        source_format.require_source_lexical(predecessor, old)
        if not rename.same_json_value_and_types(
                source_format.source_projection(proposed_block, old), old):
            raise RenamePreviewError("Renaming failed to restore the full current owner")
    except (source_format.SourceFormatError, rename.IdentifierRenameError) as exc:
        raise RenamePreviewError(f"Unsafe C++ identifier revision: {exc}") from exc
    history = [entry for entry in meta["editorial"] if entry["id"] == identity]
    old_hash, new_hash = state.fingerprint(old), state.fingerprint(proposed_block)
    if not history or history[-1]["afterSha256"] != old_hash or old_hash == new_hash:
        raise RenamePreviewError("The current complete owner lacks its exact editorial head")

    # Bind this drafting packet to the ORIGINAL historical storage address,
    # never to its visual page/block after course reordering or extension.
    # The immutable v2 origin and authenticated additions are the only
    # authorities for the learning-progress address and response signature.
    _, original_storage, original_digests = state._origin(meta["origin"])
    added = [item for item in meta["additions"] if item["id"] == identity]
    if len(added) > 1 or (added and identity in original_storage):
        raise RenamePreviewError("A stable historical owner cannot have two storage origins")
    if added:
        storage = added[0]["storage"]
        origin_digest = added[0]["sha256"]
        address = {"page": storage["page"], "block": storage["block"]}
    else:
        if identity not in original_storage:
            raise RenamePreviewError("The original stable owner has no storage address")
        page, position = original_storage[identity]
        address = {"page": page, "block": position}
        origin_digest = original_digests[identity]

    return {
        "lessonId": document["id"],
        "locale": meta["locale"],
        "historicalFamilySha256": meta["family"][meta["locale"]]["sha256"],
        "originalStateSha256": origin_digest,
        "stableStorage": address,
        "editorialPrefixSha256": state.fingerprint({
            "lessonId": document["id"], "locale": meta["locale"],
            "editorial": meta["editorial"],
        }),
        "id": identity,
        "block": deepcopy(old),
        "names": deepcopy(names),
        "occurrences": deepcopy(counts),
        "beforeSha256": old_hash,
        "afterSha256": new_hash,
        "priorSourceFormatSha256": state.fingerprint(predecessor),
        "confirmed": changed,
    }

def verify_exact_published_rename_packet(
    published_document: dict, record: dict, proposed_block: dict,
) -> bool:
    """Fail closed on a forged or incomplete archival authoring packet.

    Recompute the entire record from the authenticated PREVIOUS lesson
    publication, then compare every historical field. Do not trust a caller's
    owner name, storage position, family, editorial chain, source bytes or
    proposed digest merely because those values are well-formed hashes.
    The proposed revision is not accepted by the runtime yet.
    """
    if type(record) is not dict:
        raise RenamePreviewError("A rename archival packet must be a complete object")
    if set(record) != {
        "lessonId", "locale", "historicalFamilySha256", "originalStateSha256",
        "stableStorage", "editorialPrefixSha256", "id", "block",
        "names", "occurrences", "beforeSha256", "afterSha256",
        "priorSourceFormatSha256", "confirmed",
    }:
        raise RenamePreviewError("The rename archival packet has missing or extra fields")
    try:
        expected = preview_published_rename(
            published_document, record["id"], proposed_block,
            record["names"], record["occurrences"])
    except (KeyError, TypeError, ValueError) as exc:
        raise RenamePreviewError("Invalid or incompatible rename archival packet") from exc
    if not rename.same_json_value_and_types(record, expected):
        raise RenamePreviewError(
            "Renaming changed its authenticated owner, source, storage or editorial provenance")
    return True


def validate_read_only_rename_draft(previous: dict, draft: dict) -> dict:
    """Preflight a complete lesson publication containing proposed C++ renames.

    This is deliberately NOT stable_state_layout.validate(draft): production
    loaders do not recognize identifierRenameRevisions yet. Instead restore
    the whole previous publication, including all editorial proof prefixes,
    published source formats, family, answers, storage and text semantics.
    Only exact authenticated identifier substitutions may differ.

    The returned report is evidence for authoring, not permission for Windows
    to open the candidate or rewrite learner drafts and receipts.
    """
    state.validate(previous)
    if type(draft) is not dict or type(draft.get("stateLayout")) is not dict:
        raise RenamePreviewError("A rename draft needs complete v2 state metadata")
    source_meta = previous["stateLayout"]
    candidate_meta = draft["stateLayout"]
    records = candidate_meta.get("identifierRenameRevisions")
    if type(records) is not list or not (1 <= len(records) <= 64):
        raise RenamePreviewError("A rename draft needs 1 to 64 authenticated records")
    old_editorial = source_meta["editorial"]
    new_editorial = candidate_meta.get("editorial")
    if (type(new_editorial) is not list
            or len(new_editorial) != len(old_editorial) + len(records)
            or not rename.same_json_value_and_types(
                new_editorial[:len(old_editorial)], old_editorial)):
        raise RenamePreviewError("A rename draft changed its published editorial prefix")

    original_blocks = state._blocks(previous)
    proposed_blocks = state._blocks(draft)
    if original_blocks.keys() != proposed_blocks.keys():
        raise RenamePreviewError("A rename draft added or removed a stable block identity")

    # Build the authenticated positional lookup without trusting current
    # ordinal positions of old historical storage. Only the stable ID maps
    # the displayed block; its saved address comes from the signed packet.
    positions = {
        identity: (p, b)
        for p, page in enumerate(source_meta["current"])
        for b, identity in enumerate(page["blocks"])
    }
    confirmed = []
    seen = set()
    restored = deepcopy(draft)
    for ordinal, packet in enumerate(records):
        if type(packet) is not dict:
            raise RenamePreviewError("A rename draft contains a malformed packet")
        identity = packet.get("id")
        if (type(identity) is not str or identity not in positions
                or identity in seen):
            raise RenamePreviewError("A rename draft repeats or borrows a stable identity")
        seen.add(identity)
        current_block = proposed_blocks[identity]
        verify_exact_published_rename_packet(previous, packet, current_block)
        expected_edge = {
            "id": identity,
            "beforeSha256": packet["beforeSha256"],
            "afterSha256": packet["afterSha256"],
        }
        if not rename.same_json_value_and_types(
                new_editorial[len(old_editorial) + ordinal], expected_edge):
            raise RenamePreviewError(
                "A renamed source needs its own exact, appended editorial edge")
        p, b = positions[identity]
        restored["pages"][p]["blocks"][b] = deepcopy(packet["block"])
        confirmed.append({
            "id": identity,
            "originalStateSha256": packet["originalStateSha256"],
            "storage": deepcopy(packet["stableStorage"]),
            "beforeSha256": packet["beforeSha256"],
            "afterSha256": packet["afterSha256"],
        })

    for identity in original_blocks:
        if identity not in seen and not rename.same_json_value_and_types(
                original_blocks[identity], proposed_blocks[identity]):
            raise RenamePreviewError(
                "A rename draft altered a different lesson block: " + identity)

    del restored["stateLayout"]["identifierRenameRevisions"]
    restored["stateLayout"]["editorial"] = deepcopy(old_editorial)
    if not rename.same_json_value_and_types(restored, previous):
        raise RenamePreviewError(
            "A rename draft changed other lesson metadata, positions, answers or resources")
    # This exact published document has already passed validate(previous).
    # Its restoration proves it retains the same state addresses and progress
    # contract, without importing either draft into a learner profile.
    return {"lessonId": previous["id"], "locale": source_meta["locale"],
            "owners": confirmed, "publishedStateUnchanged": True,
            "runtimeLoadSupported": False}


def build_read_only_rename_draft(
    previous: dict, proposed_blocks: dict[str, dict],
    records: list[dict],
) -> dict:
    """Construct a complete in-memory draft without publishing it anywhere."""
    state.validate(previous)
    if (type(proposed_blocks) is not dict or type(records) is not list
            or not records or set(proposed_blocks) != {
                item.get("id") for item in records if type(item) is dict}):
        raise RenamePreviewError("The draft needs one proposed source per signed owner")
    candidate = deepcopy(previous)
    positions = {
        identity: (p, b)
        for p, page in enumerate(previous["stateLayout"]["current"])
        for b, identity in enumerate(page["blocks"])
    }
    if "identifierRenameRevisions" in candidate["stateLayout"]:
        raise RenamePreviewError("A published rename archive cannot be replaced")
    for packet in records:
        identity = packet["id"]
        if identity not in positions:
            raise RenamePreviewError("Unknown stable owner in rename draft")
        current_block = proposed_blocks[identity]
        verify_exact_published_rename_packet(previous, packet, current_block)
        page, index = positions[identity]
        candidate["pages"][page]["blocks"][index] = deepcopy(current_block)
        candidate["stateLayout"]["editorial"].append({
            "id": identity,
            "beforeSha256": packet["beforeSha256"],
            "afterSha256": packet["afterSha256"],
        })
    candidate["stateLayout"]["identifierRenameRevisions"] = deepcopy(records)
    validate_read_only_rename_draft(previous, candidate)
    return candidate

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
        if source_format.source_projection(proposed_block, old) != old:
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

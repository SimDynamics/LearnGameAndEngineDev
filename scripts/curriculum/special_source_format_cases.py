"""Shared isolated QA mutations for the three authenticated special goals.

The fixture provider owns publication pins. These helpers never read or write
assets: they derive in-memory documents from its complete verified pair.
"""
from __future__ import annotations

from copy import deepcopy

import stable_state_layout as state


def _seal_current(document: dict, owner: str) -> None:
    """Keep the editorial head current so a negative tests its actual contract."""
    head = next(edge for edge in reversed(document["stateLayout"]["editorial"])
                if edge["id"] == owner)
    head["afterSha256"] = state.fingerprint(state._blocks(document)[owner])


def _replace_identity(value, owner: str, replacement: str):
    if isinstance(value, dict):
        return {key: _replace_identity(item, owner, replacement)
                for key, item in value.items()}
    if isinstance(value, list):
        return [_replace_identity(item, owner, replacement) for item in value]
    return replacement if value == owner else value


def invalid_documents(before: dict, after: dict, owner: str) -> dict[str, dict]:
    """Return standalone-invalid copies, including freshly sealed oracle edits."""
    original = state._blocks(before)[owner]
    current = state._blocks(after)[owner]
    if original == current or original.get("type") != "cpp_compiler":
        raise ValueError("Special-source QA requires a real formatted compiler pair")
    result = {}

    missing_archive = deepcopy(after)
    missing_archive["stateLayout"]["sourceFormatRevisions"] = [
        entry for entry in missing_archive["stateLayout"]["sourceFormatRevisions"]
        if entry["id"] != owner]
    result["missing_archive"] = missing_archive

    missing_editorial = deepcopy(after)
    edge = missing_editorial["stateLayout"]["editorial"].pop()
    if edge["id"] != owner:
        raise ValueError("Special-source QA requires its format edge to be last")
    result["missing_editorial"] = missing_editorial

    def changed(name: str, mutation) -> None:
        document = deepcopy(after)
        mutation(state._blocks(document)[owner])
        _seal_current(document, owner)
        result[name] = document

    def source_token(block: dict) -> None:
        if block["initialCode"].count("int main()") != 1:
            raise ValueError("Special-source QA lost its unique main declaration")
        block["initialCode"] = block["initialCode"].replace("int main()", "long main()", 1)

    changed("source_token", source_token)
    changed("removed_output_check", lambda block: block.pop("outputCheck"))
    changed("changed_stdout", lambda block: block["outputCheck"]["cases"][0]["outputs"].__setitem__(
        0, block["outputCheck"]["cases"][0]["outputs"][0] + "QA"))
    changed("changed_exit_code", lambda block: block["outputCheck"]["cases"][0].__setitem__("exitCode", 2))
    changed("changed_stdin", lambda block: block["outputCheck"]["cases"][0].__setitem__("standardInput", "0\n"))

    # Rename consistently throughout the map, family, relations and proofs.
    # No duplicate/missing identity is needed to test the exact special owner.
    result["foreign_owner"] = _replace_identity(after, owner, "qa_foreign_special_owner")

    if state.reviewed_crt_output_variant(original):
        changed("removed_crlf_variant", lambda block: block["outputCheck"]["cases"][0]["outputs"].pop())
        changed("changed_crlf_variant", lambda block: block["outputCheck"]["cases"][0]["outputs"].__setitem__(
            1, block["outputCheck"]["cases"][0]["outputs"][1].replace("\r\n", "\n", 1)))
    return result


def _format_copy(document: dict, owner: str, source: str) -> dict:
    """Author the explicit isolated format edge and verify its full transition."""
    result = deepcopy(document)
    original, current = state._blocks(document)[owner], state._blocks(result)[owner]
    current["initialCode"] = source
    if not state.code_format.require_source_format(original, current):
        raise ValueError("Historical special-source QA requires a real format edit")
    archive = result["stateLayout"].setdefault("sourceFormatRevisions", [])
    if not any(entry["id"] == owner for entry in archive):
        archive.append({"id": owner, "block": deepcopy(original)})
    result["stateLayout"]["editorial"].append({
        "id": owner, "beforeSha256": state.fingerprint(original),
        "afterSha256": state.fingerprint(current)})
    state.validate_transition(document, result)
    return result


def historical_documents(before: dict, after: dict, owner: str) -> dict[str, dict]:
    """Retain authentic snapshots before and after the single format revision."""
    retiring = "block_0000_0000"
    if state._blocks(before).get(retiring, {}).get("type") != "heading" or retiring == owner:
        raise ValueError("Special-source QA lost its independent published heading")
    # The real retirement API freezes pages, owners, relationships, semantics
    # and the editorial prefix. The tested compiler stays active throughout.
    earlier = state.retire(before, [retiring])
    format_after_retirement = _format_copy(
        earlier, owner, state._blocks(after)[owner]["initialCode"])
    retirement_after_format = state.retire(after, [retiring])
    source = state._blocks(after)[owner]["initialCode"]
    marker = "\nint main() {"
    if source.count(marker) != 1:
        raise ValueError("Special-source QA lost its unique published main declaration")
    reformat_after_retirement = _format_copy(
        retirement_after_format, owner, source.replace(marker, "\n\nint main() {", 1))
    return {
        "format_after_retirement": format_after_retirement,
        "retirement_after_format": retirement_after_format,
        "reformat_after_retirement": reformat_after_retirement,
    }

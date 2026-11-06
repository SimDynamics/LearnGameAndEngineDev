"""Exact presentation-only annotations. No response payload or source is rewritten."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import re

ROLES = frozenset({"prose", "cpp", "string", "data", "math", "output", "citation"})
READINGS = frozenset({"auto", "natural", "technical"})
ROOT_FIELD = "textSemantics"
TRIM_CHARS = ''.join(chr(n) for n in (*range(9, 14), *range(0x1c, 0x21), 0xa0, 0x1680,
                                    *range(0x2000, 0x200b), 0x2028, 0x2029, 0x202f, 0x205f, 0x3000))


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def public_text_fields(block):
    """Concrete supported presentation/feedback/voice fields, never solutions.

    Paths are canonical JSON Pointers relative to the unchanged raw block.
    Canvas/GDI labels, dropdown choices and editable source are not supported
    by this first presentation contract and cannot silently accept annotations.
    """
    result = {}

    def text(obj, key, path):
        if isinstance(obj, dict) and isinstance(obj.get(key), str):
            result[path + "/" + key] = obj[key]

    def strings(values, path):
        if isinstance(values, list):
            for index, value in enumerate(values):
                if isinstance(value, str):
                    result[f"{path}/{index}"] = value

    def table(obj, path):
        strings(obj.get("headers"), path + "/headers")
        rows = obj.get("rows", [])
        if isinstance(rows, list):
            for index, row in enumerate(rows):
                strings(row, f"{path}/rows/{index}")

    def content(values, path, depth):
        _require(depth <= 32, "Semantic rich content is too deeply nested")
        if isinstance(values, list):
            for index, value in enumerate(values):
                location = f"{path}/{index}"
                if isinstance(value, str):
                    result[location] = value
                elif isinstance(value, dict):
                    representation(value, location, depth + 1)

    def representation(spec, path, depth=0):
        if not isinstance(spec, dict):
            return
        config = spec.get("config", spec)
        if not isinstance(config, dict):
            return
        location = path + ("/config" if "config" in spec else "")
        kind = spec.get("type", "text")
        if kind in {"text", "math_formula"}:
            for field in ("latex", "text", "formula", "value"):
                if field in config:
                    text(config, field, location)
                    break
        elif kind == "math_table":
            table(config, location)
        elif kind == "rich_answer_option":
            content(config.get("content"), location + "/content", depth)

    def card(item, path, reviewed=False):
        if not isinstance(item, dict):
            return
        values = item.get("content")
        if isinstance(values, list) and values:
            content(values, path + "/content", 0)
        else:
            for name in ("label", "text", "value"):
                if isinstance(item.get(name), str):
                    if item[name]:  # The UI substitutes id for an empty winner.
                        text(item, name, path)
                    break
        text(item, "explanation", path)
        if reviewed:
            text(item, "reviewFeedback", path)

    kind = block.get("type")
    if kind in {"heading", "paragraph", "note"}:
        text(block, "text", "")
    elif kind in {"multiple_choice", "fill_code", "output_prediction", "order_code", "drag_fill_code", "inline_fill_code"}:
        for field in ("question", "explanation"):
            text(block, field, "")
        if kind == "multiple_choice":
            strings(block.get("options"), "/options")
        if isinstance(block.get("reviewFeedback"), str):
            text(block, "reviewFeedback", "")
        else:
            strings(block.get("reviewFeedback"), "/reviewFeedback")
    elif kind == "cpp_compiler":
        config = block.get("compileCheck", {})
        text(config, "explanation", "/compileCheck")
        output = block.get("outputCheck", {})
        for index, case in enumerate(output.get("cases", []) if isinstance(output, dict) else []):
            if not isinstance(case, dict):
                continue
            path = f"/outputCheck/cases/{index}"
            text(case, "explanation", path)
            for review_index, review in enumerate(case.get("reviewFeedback", [])):
                text(review, "text", f"{path}/reviewFeedback/{review_index}")
    elif isinstance(kind, str) and kind.startswith("math_"):
        text(block, "prompt" if "prompt" in block else "question", "")
        feedback = block.get("feedback", {})
        for field in ("correct", "incorrect"):
            text(feedback, field, "/feedback")
        for group in ("representations", "tools"):
            for index, spec in enumerate(block.get(group, [])):
                representation(spec, f"/{group}/{index}")
        responses = block.get("responses", []) if kind == "math_puzzle" else [block]
        for index, response in enumerate(responses):
            if not isinstance(response, dict):
                continue
            path = f"/responses/{index}" if kind == "math_puzzle" else ""
            for field in ("label", "instruction"):
                text(response, field, path)
            response_kind = response.get("type")
            if response_kind in {"math_numeric_input", "math_expression_input"} and not response.get("fields"):
                text(response, "unit", path)
            groups = ("options",) if response_kind in {"math_multiple_choice_single", "math_multiple_choice_multi"} else (
                ("sources", "targets") if response_kind == "math_matching_drag" else ("items",) if response_kind == "math_ordering_drag" else ())
            for group in groups:
                for item_index, item in enumerate(response.get(group, [])):
                    card(item, f"{path}/{group}/{item_index}", group == "options")
            has_fields = response_kind in {"math_multi_input", "math_structured_answer", "math_equation_fill_blanks"} or (
                response_kind in {"math_numeric_input", "math_expression_input"} and bool(response.get("fields")))
            if not has_fields:
                continue
            for field_index, field in enumerate(response.get("fields", [])):
                if not isinstance(field, dict):
                    continue
                location = f"{path}/fields/{field_index}"
                for name in ("label", "unit"):
                    text(field, name, location)
                if field.get("type", "numeric") == "fixed":
                    text(field, "text", location)
                if field.get("type") == "dropdown":
                    for option_index, option in enumerate(field.get("options", [])):
                        text(option, "reviewFeedback", f"{location}/options/{option_index}")
            for segment_index, segment in enumerate(response.get("segments", [])):
                if isinstance(segment, dict) and "field" not in segment:
                    text(segment, "text", f"{path}/segments/{segment_index}")
            if "segments" not in response and response.get("layout") == "table" and response.get("rows"):
                table(response, path)
    return result


def feedback_origins(block):
    """Exact possible authored pieces of the shared math feedback compositor."""
    kind = block.get("type", "")
    if not isinstance(kind, str) or not kind.startswith("math_"):
        return set()
    public = public_text_fields(block)
    trim_chars = TRIM_CHARS
    result = {path for path, value in public.items() if
              (path in {"/feedback/correct", "/feedback/incorrect"} or path.endswith("/reviewFeedback")) and value.strip(trim_chars)}
    responses = block.get("responses", []) if kind == "math_puzzle" else [block]
    for index, response in enumerate(responses):
        if not isinstance(response, dict):
            continue
        path = f"/responses/{index}" if kind == "math_puzzle" else ""
        for obj, location, identity in [(response, path, response.get("id") if kind == "math_puzzle" else "response")] + [
                (field, f"{path}/fields/{n}", field.get("id")) for n, field in enumerate(response.get("fields", [])) if isinstance(field, dict)]:
            label = obj.get("label")
            if location + "/label" in public and isinstance(label, str) and label.strip(trim_chars) and label.strip(trim_chars) != identity:
                result.add(location + "/label")
    return result


def presentation_groups(block):
    """Fields joined by the common extractor must switch policy together."""
    public, groups = public_text_fields(block), []
    nonblank = lambda path: path in public and bool(public[path].strip(TRIM_CHARS))

    def add(paths):
        group = {path for path in paths if nonblank(path)}
        if len(group) > 1:
            groups.append(group)

    def strings(values, path):
        if isinstance(values, list):
            add(f"{path}/{n}" for n in range(len(values)))

    def content(values, path, depth):
        _require(depth <= 32, "Semantic rich content is too deeply nested")
        if isinstance(values, list):
            for n, value in enumerate(values):
                if isinstance(value, dict):
                    representation(value, f"{path}/{n}", depth + 1)

    def representation(spec, path, depth=0):
        if not isinstance(spec, dict):
            return
        config = spec.get("config", spec)
        if not isinstance(config, dict):
            return
        location = path + ("/config" if "config" in spec else "")
        if spec.get("type") == "math_table":
            strings(config.get("headers"), location + "/headers")
            for n, row in enumerate(config.get("rows", [])):
                strings(row, f"{location}/rows/{n}")
        elif spec.get("type") == "rich_answer_option":
            content(config.get("content"), location + "/content", depth)

    kind = block.get("type", "")
    if not isinstance(kind, str) or not kind.startswith("math_"):
        return groups
    for name in ("representations", "tools"):
        for n, spec in enumerate(block.get(name, [])):
            representation(spec, f"/{name}/{n}")
    responses = block.get("responses", []) if kind == "math_puzzle" else [block]
    for n, response in enumerate(responses):
        if not isinstance(response, dict):
            continue
        path = f"/responses/{n}" if kind == "math_puzzle" else ""
        fields = response.get("fields", [])
        definitions = {field.get("id"): (index, field) for index, field in enumerate(fields) if isinstance(field, dict)}
        inline = "segments" in response

        def field_paths(identifier):
            entry = definitions.get(identifier)
            if entry is None:
                return set()
            index, field = entry
            location = f"{path}/fields/{index}"
            names = (["label"] if field.get("type", "numeric") != "fixed" or not inline else [])
            if field.get("type") == "fixed":
                names.append("text")
            names.append("unit")
            selected = {location + "/" + name for name in names if nonblank(location + "/" + name)}
            add(selected)
            return selected

        for identifier in definitions:
            field_paths(identifier)
        if inline:
            joined = set()
            for index, segment in enumerate(response.get("segments", [])):
                if isinstance(segment, dict):
                    joined.update(field_paths(segment["field"]) if "field" in segment else {f"{path}/segments/{index}/text"})
            add(joined)
        elif response.get("layout") == "table":
            strings(response.get("headers"), path + "/headers")
            # Current separateTableCells policy keeps each response-row cell
            # independent; representation-table rows still use strings().
        for name in ("options", "items", "sources", "targets"):
            for index, item in enumerate(response.get(name, [])):
                if isinstance(item, dict):
                    content(item.get("content"), f"{path}/{name}/{index}/content", 0)
    return groups


def _blocks(document):
    layout = document.get("stateLayout")
    _require(isinstance(layout, dict) and type(layout.get("version")) is int and layout["version"] == 2,
             "Text semantics requires the existing stable stateLayout v2")
    current, pages = layout.get("current"), document.get("pages")
    _require(isinstance(current, list) and isinstance(pages, list) and len(current) == len(pages),
             "Text semantics requires the complete visible identity map")
    result = {}
    for page, record in zip(pages, current):
        _require(isinstance(page, dict) and isinstance(record, dict), "Malformed text semantic page owner")
        blocks, ids = page.get("blocks"), record.get("blocks")
        _require(isinstance(blocks, list) and isinstance(ids, list) and len(blocks) == len(ids),
                 "Text semantic block identities differ from their visible owners")
        for identity, block in zip(ids, blocks):
            _require(isinstance(identity, str) and identity not in result and isinstance(block, dict),
                     "Duplicate or invalid text semantic owner")
            result[identity] = block
    return result


def validate(document):
    """Return resolved descriptors, without mutating any authored object."""
    if ROOT_FIELD not in document:
        return {}
    metadata = document[ROOT_FIELD]
    _require(isinstance(metadata, dict) and set(metadata) == {"version", "fields"} and
             type(metadata["version"]) is int and metadata["version"] == 1,
             "Unknown textSemantics structure/version")
    fields = metadata["fields"]
    _require(isinstance(fields, list) and len(fields) <= 100000, "Invalid text semantic field list")
    if not fields:
        return {}
    blocks, result, inventories = _blocks(document), {}, {}
    for field in fields:
        _require(isinstance(field, dict) and set(field) == {"block", "path", "sourceSha256", "spans"},
                 "Invalid text semantic field structure")
        owner, path = field["block"], field["path"]
        _require(isinstance(owner, str) and owner in blocks and isinstance(path, str), "Unknown text semantic owner/path")
        if owner not in inventories:
            inventories[owner] = public_text_fields(blocks[owner])
        public = inventories[owner]
        _require(path in public, "Text semantic path is not a supported public string")
        source = public[path]
        try:
            encoded, utf16 = source.encode("utf-8"), source.encode("utf-16-le")
        except UnicodeError as error:
            raise ValueError("Text semantic source contains an invalid Unicode scalar") from error
        digest = field["sourceSha256"]
        _require(isinstance(digest, str) and re.fullmatch(r"[0-9a-f]{64}", digest) is not None and
                 hashlib.sha256(encoded).hexdigest() == digest, "Text semantic source fingerprint is stale")
        _require(len(encoded) <= 8 * 1024 * 1024, "Text semantic source is too large")
        spans, previous = field["spans"], 0
        _require(isinstance(spans, list) and len(spans) <= 100000, "Invalid text semantic span list")
        for span in spans:
            _require(isinstance(span, dict) and {"start", "end", "text", "role"} <= set(span) <=
                     {"start", "end", "text", "role", "reading"}, "Invalid text semantic span structure")
            start, end = span["start"], span["end"]
            _require(type(start) is int and type(end) is int and 0 <= previous <= start < end <= len(utf16) // 2,
                     "Text semantic spans must be ordered disjoint UTF-16 ranges")
            _require(isinstance(span["text"], str) and isinstance(span["role"], str) and span["role"] in ROLES and
                     isinstance(span.get("reading", "auto"), str) and span.get("reading", "auto") in READINGS,
                     "Unknown text semantic role/reading intent")
            try:
                selected = utf16[start * 2:end * 2].decode("utf-16-le")
            except UnicodeError as error:
                raise ValueError("Text semantic range splits a Unicode scalar") from error
            _require(selected == span["text"], "Text semantic selection differs from its exact source")
            previous = end
        owned = result.setdefault(owner, {})
        _require(path not in owned, "Duplicate text semantic field")
        owned[path] = {"source": source, "spans": deepcopy(spans)}
    for owner, fields in result.items():
        group = feedback_origins(blocks[owner])
        _require(not group.intersection(fields) or group <= fields.keys(),
                 "Annotating mathematical feedback requires every possible authored feedback origin in that block")
        for group in presentation_groups(blocks[owner]):
            _require(not group.intersection(fields) or group <= fields.keys(),
                     "Annotating a joined mathematical passage requires all its authored source fields")
    return result


def select(source, text, role, *, occurrence=None, reading="auto"):
    """Authoring helper; JSON still has the single start/end/text form."""
    _require(isinstance(source, str) and isinstance(text, str) and bool(text), "Select an exact nonempty fragment")
    _require(role in ROLES and reading in READINGS, "Unknown semantic role/reading")
    starts, position = [], 0
    while (position := source.find(text, position)) >= 0:
        starts.append(position)
        position += 1
    if occurrence is None:
        _require(len(starts) == 1, "A repeated fragment requires an explicit occurrence")
        position = starts[0]
    else:
        _require(type(occurrence) is int and 1 <= occurrence <= len(starts), "Occurrence must select an existing positive match")
        position = starts[occurrence - 1]
    start = len(source[:position].encode("utf-16-le")) // 2
    result = {"start": start, "end": start + len(text.encode("utf-16-le")) // 2, "text": text, "role": role}
    if reading != "auto":
        result["reading"] = reading
    return result


def make_field(document, block, path, spans):
    """Prepare an exact field descriptor; does not edit the document/layout."""
    blocks = _blocks(document)
    _require(block in blocks, "Unknown stable semantic owner")
    fields = public_text_fields(blocks[block])
    _require(path in fields, "Not a supported public field")
    return {"block": block, "path": path, "sourceSha256": hashlib.sha256(fields[path].encode("utf-8")).hexdigest(),
            "spans": deepcopy(spans)}

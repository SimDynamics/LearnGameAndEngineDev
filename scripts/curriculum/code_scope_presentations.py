"""Explicit, source-bound reading containers; no answer or geometry migration."""
from __future__ import annotations

import hashlib
import re

import text_semantics

ROOT_FIELD = "codeScopePresentations"
_ID = re.compile(r"[A-Za-z0-9_-]{1,128}\Z")
_HASH = re.compile(r"[0-9a-f]{64}\Z")
_INDEX = re.compile(r"(?:0|[1-9][0-9]*)\Z")
_BREAKS = frozenset("\0\r\n\u2028\u2029")


def _require(condition, message):
    if not condition:
        raise ValueError("Code scope presentation: " + message)


def _keys(value, required, optional=()):
    _require(type(value) is dict, "expected an object")
    _require(set(required) <= set(value) <= set(required) | set(optional),
             "missing or unknown property")


def _line(value):
    _require(type(value) is str and value.strip(), "expected nonblank text")
    _require(len(value.encode("utf-8")) <= 8192 and not any(c in _BREAKS for c in value),
             "each title or instruction must occupy one physical source line")
    return value


def _source(representation, pointer):
    _require(type(pointer) is str and pointer.startswith("/") and len(pointer.encode("utf-8")) <= 512,
             "source must be a bounded JSON pointer relative to the representation")
    value = representation
    for raw in pointer[1:].split("/"):
        _require(raw and not re.search(r"~(?![01])", raw), "noncanonical source pointer")
        token = raw.replace("~1", "/").replace("~0", "~")
        if type(value) is dict:
            _require(token in value, "source field does not exist")
            value = value[token]
        elif type(value) is list:
            _require(_INDEX.fullmatch(token) and len(token) <= 7,
                     "source array index must be canonical")
            index = int(token)
            _require(index < len(value), "source array index is outside the representation")
            value = value[index]
        else:
            _require(False, "source pointer traverses a scalar")
    return _line(value)


def _reading_pages(document, blocks):
    layout = document["stateLayout"]
    pages, current = document.get("pages"), layout.get("current")
    _require(type(layout.get("version")) is int and layout["version"] == 2 and type(pages) is list and
             type(current) is list and len(pages) == len(current),
             "reading scopes require the complete stable v2 page map")
    owners = {}
    for index, (page, record) in enumerate(zip(pages, current)):
        _require(type(page) is dict and type(record) is dict and
                 type(page.get("blocks")) is list and type(record.get("blocks")) is list and
                 len(page["blocks"]) == len(record["blocks"]),
                 "reading scope page owners do not match their visible blocks")
        for identity, block in zip(record["blocks"], page["blocks"]):
            _require(type(identity) is str and identity not in owners and
                     identity in blocks and blocks[identity] == block,
                     "reading scope has an invalid or repeated stable owner")
            owners[identity] = index
    return owners


def _reading_source(value):
    _require(type(value) is str and value.strip() and "\0" not in value,
             "reading source must be nonblank public text")
    try:
        encoded, units = value.encode("utf-8"), value.encode("utf-16-le")
    except UnicodeError as error:
        raise ValueError("Code scope reading source contains an invalid Unicode scalar") from error
    _require(len(encoded) <= 256 * 1024, "reading source exceeds supported bounds")
    return units


def _reading_slice(source, authored):
    units = _reading_source(source)
    interval = authored.get("range", [0, len(units) // 2])
    _require(type(interval) is list and len(interval) == 2 and
             all(type(n) is int for n in interval) and
             0 <= interval[0] < interval[1] <= len(units) // 2,
             "reading range must be a nonempty ordered UTF-16 selection")
    start, end = interval
    try:
        selected = units[start * 2:end * 2].decode("utf-16-le")
    except UnicodeError as error:
        raise ValueError("Code scope reading range splits a Unicode scalar") from error
    return start, end, selected


def _reading_semantics(document, owner, path, source, start, end, selected):
    fields = document.get("textSemantics", {}).get("fields", [])
    candidates = [field for field in fields
                  if field.get("block") == owner and field.get("path") == path]
    _require(len(candidates) <= 1, "duplicate semantic source for a reading note")
    if not candidates:
        return None
    # Use the same strict descriptor as the root authoring contract. A scope
    # relocates authored meaning; it never infers code from prose or an icon.
    descriptor = text_semantics._descriptor(candidates[0], source)
    spans = []
    for span in descriptor["spans"]:
        if span["end"] <= start or span["start"] >= end:
            continue
        _require(start <= span["start"] < span["end"] <= end,
                 "reading range splits an authored semantic span")
        spans.append(dict(span, start=span["start"] - start, end=span["end"] - start))
    return {"source": selected, "spans": spans}


def _reading_projection(document, blocks, entry, consumers):
    _keys(entry, {"block", "language", "root"})
    owner = entry["block"]
    _require(entry["language"] == "cpp", "only explicitly declared C++ is supported")
    pages = _reading_pages(document, blocks)
    _require(owner in pages and blocks[owner].get("type") == "code",
             "reading scope owner must be active code without an answer")
    language = blocks[owner].get("language", "")
    _require(type(language) is str and language in {"", "cpp", "c++"},
             "reading scope cannot reinterpret another declared language as C++")
    ids, ranges, sources = set(), {}, {}
    notes = []

    def node(authored, depth=0):
        _require(depth <= 12 and len(ids) < 256, "tree exceeds supported bounds")
        _keys(authored, {"id"},
              {"title", "source", "sourceBlock", "sourceSha256", "range", "role", "icon", "children"})
        key = authored["id"]
        _require(type(key) is str and _ID.fullmatch(key) and key not in ids,
                 "node IDs must be unique stable identifiers")
        ids.add(key)
        _require(("title" in authored) != ("source" in authored),
                 "declare exactly one prose title or public source reference")
        result = {"key": key, "text": "", "code": False, "children": [],
                  "decorative_icon": "", "source_block": "", "source_path": "",
                  "text_semantics": None}
        if "title" in authored:
            _require(not set(authored).intersection(
                {"sourceBlock", "sourceSha256", "range", "role", "icon"}),
                "a prose container title cannot borrow source metadata")
            result["text"] = _line(authored["title"])
        else:
            source_owner = authored.get("sourceBlock", owner)
            _require(type(source_owner) is str and source_owner in blocks and
                     source_owner in pages and pages[source_owner] == pages[owner],
                     "reading note must belong to this same page")
            code = source_owner == owner
            path = authored["source"]
            if code:
                _require(path == "/code" and authored.get("role", "code") == "code" and
                         "icon" not in authored, "code nodes require only the owner's complete /code source")
            else:
                _require(blocks[source_owner].get("type") == "paragraph" and path == "/text" and
                         authored.get("role") == "text",
                         "a reading note must reference paragraph /text, never another activity or answer")
                _require(source_owner not in consumers, "a note already belongs to another reading scope")
                _require("children" not in authored, "reading notes are source-bound text leaves")
                if "icon" in authored:
                    _require(authored["icon"] == "check", "unknown decorative note icon")
                    result["decorative_icon"] = "check"
                if source_owner not in notes:
                    notes.append(source_owner)
            source = blocks[source_owner].get(path[1:])
            start, end, selected = _reading_slice(source, authored)
            digest = authored.get("sourceSha256")
            _require(type(digest) is str and _HASH.fullmatch(digest) and
                     digest == hashlib.sha256(source.encode("utf-8")).hexdigest(),
                     "source text fingerprint is stale")
            if not code:
                _require(selected.strip(), "a reading note must contain authored text")
            ranges.setdefault(source_owner, []).append((start, end))
            sources[source_owner] = source
            result.update(text=selected, code=code, source_block=source_owner, source_path=path,
                          text_semantics=_reading_semantics(
                              document, source_owner, path, source, start, end, selected))
        if "children" in authored:
            children = authored["children"]
            _require(type(children) is list and 0 < len(children) <= 128,
                     "a container needs a nonempty ordered child list")
            result["children"] = [node(child, depth + 1) for child in children]
        else:
            _require("source" in authored, "a leaf must reference a complete source selection")
        return result

    root = node(entry["root"])
    _require(root["children"], "the root must be a reading container")
    _require(owner in ranges, "reading presentation must retain the complete program")
    for identity, selected_ranges in ranges.items():
        units = _reading_source(sources[identity])
        previous = 0
        for start, end in selected_ranges:
            _require(start >= previous, "reading source selections overlap or change source order")
            gap = units[previous * 2:start * 2].decode("utf-16-le")
            _require(not gap if identity == owner else not gap.strip(),
                     "reading projection omits source content")
            previous = end
        tail = units[previous * 2:].decode("utf-16-le")
        _require(not tail if identity == owner else not tail.strip(),
                 "reading projection omits source content")
    consumers.update(notes)
    root.update(read_only=True, consumed_blocks=notes)
    return root


def validate(document, *, blocks):
    """Resolve an authenticated overlay without changing any curricular block.

    The caller supplies already validated stable owners. Each source reference
    pins its complete UTF-8 text, independently of geometric coordinates.
    Returned rows are only presentation data, never evaluation evidence.
    """
    if ROOT_FIELD not in document:
        return {}
    _require(type(document.get("stateLayout")) is dict,
             "stable block ownership is required")
    metadata = document[ROOT_FIELD]
    _keys(metadata, {"version", "entries"})
    _require(type(metadata["version"]) is int and metadata["version"] in (1, 2),
             "unsupported version")
    entries = metadata["entries"]
    _require(type(entries) is list and 0 < len(entries) <= 64, "invalid entry count")
    resolved, consumers = {}, set()
    for entry in entries:
        _require(type(entry) is dict, "expected an entry object")
        if (metadata["version"] == 2 and type(entry.get("block")) is str and
                entry["block"] in blocks and blocks[entry["block"]].get("type") == "code"):
            identity = (entry["block"], None)
            _require(identity not in resolved, "duplicate reading projection")
            resolved[identity] = _reading_projection(document, blocks, entry, consumers)
            continue
        _keys(entry, {"block", "representation", "language", "root"})
        owner = entry["block"]
        _require(type(owner) is str and owner in blocks, "unknown active block owner")
        block = blocks[owner]
        _require(block.get("type") == "math_puzzle", "owner must be a composed puzzle")
        index = entry["representation"]
        representations = block.get("representations")
        _require(type(index) is int and type(representations) is list and
                 0 <= index <= 1000000 and index < len(representations), "invalid representation index")
        identity = (owner, index)
        _require(identity not in resolved, "duplicate representation projection")
        _require(entry["language"] == "cpp", "only explicitly declared C++ is supported")
        representation = representations[index]
        _require(type(representation) is dict, "representation must be an object")
        ids, references = set(), set()

        def node(authored, depth=0):
            _require(depth <= 12 and len(ids) < 256, "tree exceeds supported bounds")
            _keys(authored, {"id"}, {"title", "source", "sourceSha256", "children"})
            key = authored["id"]
            _require(type(key) is str and _ID.fullmatch(key) and key not in ids,
                     "node IDs must be unique stable identifiers")
            ids.add(key)
            _require(("title" in authored) != ("source" in authored),
                     "declare exactly one prose title or C++ source reference")
            code = "source" in authored
            if code:
                pointer = authored["source"]
                text = _source(representation, pointer)
                digest = authored.get("sourceSha256")
                _require(type(digest) is str and _HASH.fullmatch(digest) and
                         digest == hashlib.sha256(text.encode("utf-8")).hexdigest(),
                         "source text fingerprint is stale")
                _require(pointer not in references, "a source field is projected more than once")
                references.add(pointer)
            else:
                _require("sourceSha256" not in authored, "a prose title has no source reference")
                text = _line(authored["title"])
            children = authored.get("children")
            if "children" in authored:
                _require(type(children) is list and 0 < len(children) <= 128,
                         "a container needs a nonempty ordered child list")
                children = [node(child, depth + 1) for child in children]
            else:
                _require(code, "a leaf must reference a complete C++ instruction")
                children = []
            return {"key": key, "text": text, "code": code, "children": children}

        root = node(entry["root"])
        _require(root["children"], "the root must be a reading container")
        resolved[identity] = root
    return resolved

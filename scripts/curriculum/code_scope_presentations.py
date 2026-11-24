"""Explicit, source-bound reading containers; no answer or geometry migration."""
from __future__ import annotations

import hashlib
import re

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
    _require(type(metadata["version"]) is int and metadata["version"] == 1,
             "unsupported version")
    entries = metadata["entries"]
    _require(type(entries) is list and 0 < len(entries) <= 64, "invalid entry count")
    resolved = {}
    for entry in entries:
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

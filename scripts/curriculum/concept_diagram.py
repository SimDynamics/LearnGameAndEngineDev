"""Strict authoring contract for native reading-only conceptual flow diagrams."""
import re

FIELDS = frozenset({"type", "id", "schemaVersion", "title", "nodes", "links"})
NODE_FIELDS = frozenset({"id", "kind", "label", "value", "caption"})
LINK_FIELDS = frozenset({"from", "to", "label"})
IDENTIFIER = re.compile(r"[a-z][a-z0-9_]{0,63}\Z")


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _object(value, required, allowed):
    _require(type(value) is dict and required <= value.keys() <= allowed,
             "Campos desconocidos o ausentes en el diagrama conceptual")


def _identifier(value):
    return type(value) is str and IDENTIFIER.fullmatch(value) is not None


def _visible(value, limit, multiline=False):
    _require(type(value) is str and bool(value.strip()), "Texto del diagrama vacío o no textual")
    _require(len(value.encode("utf-8")) <= limit, "Texto del diagrama demasiado largo")
    _require(all((ord(c) >= 32 and not 127 <= ord(c) <= 159 and ord(c) not in (0x2028, 0x2029))
                 or (multiline and c in "\n\t") for c in value),
             "Carácter de control no admitido en el diagrama")


def validate_contract(block):
    _object(block, FIELDS - {"id"}, FIELDS)
    _require(block["type"] == "concept_diagram" and type(block["schemaVersion"]) is int
             and block["schemaVersion"] == 1, "El diagrama requiere schemaVersion entero 1")
    if "id" in block:
        _require(_identifier(block["id"]), "ID de diagrama inválido")
    _visible(block["title"], 512)
    nodes, links = block["nodes"], block["links"]
    _require(type(nodes) is list and 1 <= len(nodes) <= 8 and type(links) is list
             and len(links) == len(nodes) - 1, "Se requieren de uno a ocho nodos consecutivos")
    ids = set()
    for node in nodes:
        _object(node, NODE_FIELDS - {"caption"}, NODE_FIELDS)
        _require(_identifier(node["id"]) and node["id"] not in ids, "ID de nodo inválido o duplicado")
        ids.add(node["id"])
        _require(node["kind"] in ("generic", "memory", "console"), "Tipo de nodo no admitido")
        _visible(node["label"], 512)
        _visible(node["value"], 2048, multiline=True)
        if "caption" in node:
            _visible(node["caption"], 512)
    for index, link in enumerate(links):
        _object(link, LINK_FIELDS - {"label"}, LINK_FIELDS)
        _require(link["from"] == nodes[index]["id"] and link["to"] == nodes[index + 1]["id"],
                 "Una flecha debe unir nodos consecutivos en su dirección declarada")
        if "label" in link:
            _visible(link["label"], 512)
    return block


def validate_document(document):
    def check(block):
        if type(block) is dict and block.get("type") == "concept_diagram":
            validate_contract(block)

    def pages(values):
        for page in values:
            for block in page.get("blocks", []):
                check(block)

    pages(document.get("pages", []))
    metadata = document.get("stateLayout", {})
    for item in metadata.get("retired", []):
        check(item["block"])
    for snapshot in metadata.get("retirementHistory", []):
        pages(snapshot["pages"])

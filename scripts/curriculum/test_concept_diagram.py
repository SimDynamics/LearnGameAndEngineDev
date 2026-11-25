"""Conceptual reading diagrams: closed authoring, semantic ownership and stable-state QA."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import unittest

import concept_diagram
import stable_state_layout as state
import text_semantics

EXAMPLE = Path(__file__).resolve().parents[2] / "docs/examples/concept_diagram_memory_console.json"


def example():
    return json.loads(EXAMPLE.read_text(encoding="utf-8"))


def alternate():
    return {"type": "concept_diagram", "schemaVersion": 1, "id": "recorrido",
            "title": "Flujo de datos",
            "nodes": [
                {"id": "entrada", "kind": "generic", "label": "Entrada", "value": "12"},
                {"id": "doble", "kind": "memory", "label": "doble", "value": "24", "caption": "Resultado"},
                {"id": "salida", "kind": "console", "label": "Salida", "value": "24\nListo"}],
            "links": [{"from": "entrada", "to": "doble", "label": "Multiplicar por dos"},
                      {"from": "doble", "to": "salida", "label": "Mostrar"}]}


def raw_lesson():
    return {"id": "cpp_fundamentals__qa__concept_diagram", "title": "Diagramas conceptuales",
            "pages": [
                {"title": "Memoria", "blocks": [example()]},
                {"title": "Flujo", "blocks": [alternate(),
                    {"type": "multiple_choice", "id": "pregunta", "question": "¿Cuál es el doble de doce?",
                     "options": ["24", "12"], "correctIndex": 0, "explanation": "Veinticuatro."}]},
                {"title": "Continuación", "blocks": [{"type": "paragraph", "text": "Continúa."}]}]}


def fixture():
    raw = raw_lesson()
    document = state.prepare_v2(raw, "es", {"es": raw})
    owner = document["stateLayout"]["current"][0]["blocks"][0]
    fields = [
        text_semantics.make_field(document, owner, "/nodes/0/label",
                                  [text_semantics.select("tarifa", "tarifa", "cpp")]),
        text_semantics.make_field(document, owner, "/nodes/1/value",
                                  [text_semantics.select("8", "8", "output")]),
    ]
    document["textSemantics"] = {"version": 1, "fields": fields}
    state.validate(document)
    return document


class ConceptDiagramTests(unittest.TestCase):
    def test_examples_and_single_node(self):
        for block in (example(), alternate()):
            self.assertIs(concept_diagram.validate_contract(block), block)
        block = example()
        block["nodes"] = block["nodes"][:1]
        block["links"] = []
        concept_diagram.validate_contract(block)

    def test_closed_schema(self):
        for key, value in (("schemaVersion", True), ("schemaVersion", 1.0), ("schemaVersion", 2),
                           ("title", ""), ("title", "x"*513), ("title", "a\nb"),
                           ("id", "bad-ID"), ("responses", []), ("reviewFeedback", []),
                           ("explanation", "hidden"), ("nodes", []), ("links", [])):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                concept_diagram.validate_contract(dict(example(), **{key: value}))

    def test_invalid_nodes_and_links(self):
        for path, key, value in (("nodes", "kind", "other"), ("nodes", "id", "consola"),
                                 ("nodes", "value", "\x00"), ("nodes", "value", " "),
                                 ("nodes", "value", 8), ("nodes", "value", "x"*2049),
                                 ("nodes", "caption", "a\r\nb"), ("nodes", "extra", True),
                                 ("links", "from", "unknown"), ("links", "to", "tarifa"),
                                 ("links", "label", "  "), ("links", "extra", True)):
            block = example()
            block[path][0][key] = value
            with self.subTest(path=path,key=key), self.assertRaises(ValueError):
                concept_diagram.validate_contract(block)
        block = alternate()
        block["links"].reverse()
        with self.assertRaises(ValueError):
            concept_diagram.validate_contract(block)

    def test_only_visible_fields_own_semantics(self):
        fields = text_semantics.public_text_fields(example())
        self.assertEqual(fields, {"/title": example()["title"], "/nodes/0/label": "tarifa",
                                  "/nodes/0/value": "8", "/nodes/0/caption": "Memoria",
                                  "/nodes/1/label": "Consola", "/nodes/1/value": "8"})
        self.assertEqual(text_semantics.public_text_fields(alternate())["/links/0/label"],
                         "Multiplicar por dos")
        document = fixture()
        owner = document["stateLayout"]["current"][0]["blocks"][0]
        for path in ("/nodes/0/id", "/nodes/0/kind", "/links/0/from", "/links/0/to"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                text_semantics.make_field(document, owner, path, [])

    def test_state_layout_and_neighbor_identity(self):
        document = fixture()
        original = deepcopy(document)
        plan = deepcopy(document["stateLayout"]["current"])
        plan[1]["blocks"].reverse()
        updated = state.reorder(document, plan)
        state.validate_transition(document, updated)
        self.assertEqual(state.validate(updated)[1, 0], (1, 1))
        self.assertEqual(state.validate(updated)[1, 1], (1, 0))
        self.assertEqual(document, original)
        # This is not a mutable answer or a free editorial text override.
        changed = deepcopy(document)
        changed["pages"][0]["blocks"][0]["nodes"][0]["value"] = "7"
        with self.assertRaises(ValueError):
            state.validate_transition(document, changed)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--fixture":
        destination = Path(sys.argv[2])
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(fixture(), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    else:
        unittest.main()

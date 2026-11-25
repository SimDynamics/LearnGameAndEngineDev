"""Reading overlays preserve every code byte and relocate authenticated prose once."""
from copy import deepcopy
import hashlib
import unittest

import code_scope_presentations as scopes


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def units(text):
    return len(text.encode("utf-16-le")) // 2


def flatten(node):
    yield node
    for child in node["children"]:
        yield from flatten(child)


def fixture():
    code = ('#include <iostream>\n\nint main() {\n'
            '    const int fichas = 8;\n    if (fichas >= 4) {\n'
            '        const int bolsas = fichas / 4;\n        std::cout << bolsas;\n'
            '    }\n    return 0;\n}\n')
    first = "Dentro se pueden usar bolsas y fichas."
    second = "Fuera del if, solo fichas sigue al alcance."
    notes = first + "\n\n" + second
    pieces = code.splitlines(keepends=True)
    offsets, at = [], 0
    for piece in pieces:
        offsets.append((at, at + units(piece)))
        at += units(piece)

    def segment(index, key):
        return {"id": key, "source": "/code", "sourceSha256": digest(code),
                "range": list(offsets[index])}

    inner = segment(4, "if")
    inner["children"] = [segment(5, "bolsas"), segment(6, "salida"),
        {"id": "nota_dentro", "sourceBlock": "notes", "source": "/text",
         "sourceSha256": digest(notes), "range": [0, units(first)],
         "role": "text", "icon": "check"}, segment(7, "cierre_if")]
    main = segment(2, "main")
    main["children"] = [segment(3, "fichas"), inner,
        {"id": "nota_fuera", "sourceBlock": "notes", "source": "/text",
         "sourceSha256": digest(notes), "range": [units(first + "\n\n"), units(notes)],
         "role": "text"}, segment(8, "return"), segment(9, "cierre_main")]
    document = {
        "pages": [{"blocks": [{"type": "code", "code": code, "language": "cpp"},
                             {"type": "paragraph", "text": notes}]}],
        "stateLayout": {"version": 2, "current": [{"blocks": ["program", "notes"]}]},
        "codeScopePresentations": {"version": 2, "entries": [{
            "block": "program", "language": "cpp",
            "root": {"id": "programa", "title": "Programa completo",
                     "children": [segment(0, "header"), segment(1, "blank"), main]}}]},
    }
    return document


def blocks(document):
    return {identity: block
            for page, record in zip(document["pages"], document["stateLayout"]["current"])
            for identity, block in zip(record["blocks"], page["blocks"])}


def authored_nodes(document):
    def walk(node):
        yield node
        for child in node.get("children", []):
            yield from walk(child)
    return {node["id"]: node for node in walk(document["codeScopePresentations"]["entries"][0]["root"])}


class CodeScopeReadingTest(unittest.TestCase):
    def resolve(self, document):
        return scopes.validate(document, blocks=blocks(document))[("program", None)]

    def rejects(self, mutation):
        document = fixture()
        mutation(document)
        with self.assertRaises(ValueError):
            self.resolve(document)

    def test_complete_program_and_notes_keep_their_exact_order_without_mutating_sources(self):
        document = fixture()
        original = deepcopy(document)
        tree = self.resolve(document)
        nodes = list(flatten(tree))
        self.assertEqual("".join(node["text"] for node in nodes if node["code"]),
                         blocks(document)["program"]["code"])
        self.assertEqual([node["key"] for node in nodes], [
            "programa", "header", "blank", "main", "fichas", "if", "bolsas", "salida",
            "nota_dentro", "cierre_if", "nota_fuera", "return", "cierre_main"])
        self.assertEqual(tree["consumed_blocks"], ["notes"])
        self.assertTrue(tree["read_only"])
        self.assertEqual(document, original)
        note_nodes = [node for node in nodes if node["source_block"] == "notes"]
        self.assertEqual([node["source_path"] for node in note_nodes], ["/text", "/text"])
        self.assertEqual([node["text"] for node in note_nodes],
                         blocks(document)["notes"]["text"].split("\n\n"))
        self.assertTrue(all(node["text_semantics"] is None for node in note_nodes))

    def test_icon_is_decorative_and_optional_without_entering_the_source_text(self):
        document = fixture()
        first = self.resolve(document)
        authored_nodes(document)["nota_dentro"].pop("icon")
        second = self.resolve(document)
        self.assertEqual([node["text"] for node in flatten(first)],
                         [node["text"] for node in flatten(second)])
        self.assertEqual([node["decorative_icon"] for node in flatten(first)].count("check"), 1)
        self.assertFalse(any(node["decorative_icon"] for node in flatten(second)))
        self.assertFalse(any("✓" in node["text"] for node in flatten(first)))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("icon", "answer"))
        self.rejects(lambda d: authored_nodes(d)["fichas"].__setitem__("icon", "check"))

    def test_header_braces_whitespace_and_code_order_cannot_be_dropped_or_duplicated(self):
        for key in ("header", "blank", "fichas", "cierre_if", "cierre_main"):
            def mutate(document, key=key):
                root = document["codeScopePresentations"]["entries"][0]["root"]
                def remove(node):
                    node["children"] = [child for child in node.get("children", [])
                                        if child["id"] != key]
                    for child in node["children"]:
                        if "children" in child:
                            remove(child)
                remove(root)
            with self.subTest(key=key):
                self.rejects(mutate)
        self.rejects(lambda d: authored_nodes(d)["fichas"].__setitem__(
            "range", authored_nodes(d)["return"]["range"]))
        self.rejects(lambda d: authored_nodes(d)["cierre_if"].__setitem__(
            "range", authored_nodes(d)["salida"]["range"]))
        self.rejects(lambda d: authored_nodes(d)["fichas"].__setitem__(
            "range", [authored_nodes(d)["fichas"]["range"][0] + 1,
                      authored_nodes(d)["fichas"]["range"][1]]))

    def test_notes_must_consume_all_nonblank_source_in_order_exactly_once(self):
        self.rejects(lambda d: authored_nodes(d)["main"]["children"].pop(2))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__(
            "range", [1, authored_nodes(d)["nota_dentro"]["range"][1]]))
        self.rejects(lambda d: authored_nodes(d)["nota_fuera"].__setitem__(
            "range", authored_nodes(d)["nota_dentro"]["range"]))
        document = fixture()
        # The complete two-line paragraph is also a valid single public note.
        authored_nodes(document)["nota_dentro"].pop("range")
        authored_nodes(document)["main"]["children"].pop(2)
        self.assertEqual(self.resolve(document)["consumed_blocks"], ["notes"])

    def test_foreign_pages_activities_answers_and_field_pointers_are_rejected(self):
        for pointer in ("/code", "/responses/0/validator/expected", "/question", "/../text"):
            with self.subTest(pointer=pointer):
                self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("source", pointer))
        for kind in ("math_puzzle", "cpp_compiler", "code", "multiple_choice", "note"):
            with self.subTest(kind=kind):
                self.rejects(lambda d: blocks(d)["notes"].__setitem__("type", kind))
        def another_page(document):
            note = document["pages"][0]["blocks"].pop()
            document["stateLayout"]["current"][0]["blocks"].pop()
            document["pages"].append({"blocks": [note]})
            document["stateLayout"]["current"].append({"blocks": ["notes"]})
        self.rejects(another_page)
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("sourceBlock", "missing"))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("role", "code"))

    def test_a_paragraph_cannot_be_hidden_by_two_reading_owners(self):
        document = fixture()
        document["pages"][0]["blocks"].append(deepcopy(blocks(document)["program"]))
        document["stateLayout"]["current"][0]["blocks"].append("other_program")
        second = deepcopy(document["codeScopePresentations"]["entries"][0])
        second["block"] = "other_program"
        document["codeScopePresentations"]["entries"].append(second)
        with self.assertRaisesRegex(ValueError, "another reading scope"):
            self.resolve(document)

    def test_declared_language_cannot_be_reinterpreted_as_cpp(self):
        for language in ("python", "cmake"):
            with self.subTest(language=language):
                self.rejects(lambda d: blocks(d)["program"].__setitem__("language", language))
        for language in ("", "cpp", "c++"):
            document = fixture()
            blocks(document)["program"]["language"] = language
            self.assertTrue(self.resolve(document)["read_only"])
        document = fixture()
        blocks(document)["program"].pop("language")
        self.assertTrue(self.resolve(document)["read_only"])

    def test_every_slice_authenticates_the_complete_original_source(self):
        self.rejects(lambda d: blocks(d)["program"].__setitem__(
            "code", blocks(d)["program"]["code"] + "// later source changed\n"))
        self.rejects(lambda d: blocks(d)["notes"].__setitem__(
            "text", blocks(d)["notes"]["text"] + " Another explanation."))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("sourceSha256", "0" * 64))
        self.rejects(lambda d: authored_nodes(d)["header"].__setitem__("sourceSha256", "0" * 64))

    def test_utf16_ranges_accept_whole_non_bmp_characters_and_reject_half_surrogates(self):
        document = fixture()
        note = blocks(document)["notes"]
        note["text"] = "🧺 " + note["text"]
        for key in ("nota_dentro", "nota_fuera"):
            node = authored_nodes(document)[key]
            node["sourceSha256"] = digest(note["text"])
            node["range"][1] += 3
            if key == "nota_fuera":
                node["range"][0] += 3
        tree = self.resolve(document)
        self.assertTrue(next(n for n in flatten(tree) if n["key"] == "nota_dentro")["text"].startswith("🧺 "))
        for boundary in ("start", "end"):
            candidate = deepcopy(document)
            authored_nodes(candidate)["nota_dentro"]["range"][0 if boundary == "start" else 1] = 1
            with self.subTest(boundary=boundary), self.assertRaisesRegex(ValueError, "Unicode scalar"):
                self.resolve(candidate)
        invalid = fixture()
        blocks(invalid)["notes"]["text"] = "\ud800 invalid"
        with self.assertRaises(ValueError):
            self.resolve(invalid)

    def test_semantic_spans_are_relocated_without_inference_or_partial_cuts(self):
        document = fixture()
        source = blocks(document)["notes"]["text"]
        occurrence = source.rfind("fichas")
        start = units(source[:occurrence])
        document["textSemantics"] = {"version": 1, "fields": [{
            "block": "notes", "path": "/text", "sourceSha256": digest(source),
            "spans": [{"start": start, "end": start + 6, "text": "fichas", "role": "cpp",
                       "reading": "technical"}]}]}
        tree = self.resolve(document)
        inner = next(n for n in flatten(tree) if n["key"] == "nota_dentro")
        outer = next(n for n in flatten(tree) if n["key"] == "nota_fuera")
        self.assertEqual(inner["text_semantics"], {"source": inner["text"], "spans": []})
        span = outer["text_semantics"]["spans"][0]
        offset = authored_nodes(document)["nota_fuera"]["range"][0]
        self.assertEqual(span, {"start": start - offset, "end": start + 6 - offset,
                               "text": "fichas", "role": "cpp", "reading": "technical"})
        first_span_start = units("Dentro se pueden usar ")
        document["textSemantics"]["fields"][0]["spans"] = [{
            "start": first_span_start, "end": first_span_start + 6, "text": "bolsas", "role": "cpp"}]
        authored_nodes(document)["nota_dentro"]["range"][1] = first_span_start + 3
        with self.assertRaisesRegex(ValueError, "semantic span"):
            self.resolve(document)

    def test_duplicate_nodes_invalid_ranges_titles_and_versions_remain_invalid(self):
        self.rejects(lambda d: authored_nodes(d)["return"].__setitem__("id", "header"))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("range", [True, 5]))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("range", [1, 1]))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("range", [-1, 5]))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("range", [0, 999999]))
        self.rejects(lambda d: authored_nodes(d)["nota_dentro"].__setitem__("children", []))
        self.rejects(lambda d: authored_nodes(d)["programa"].__setitem__("icon", "check"))
        self.rejects(lambda d: d["codeScopePresentations"].__setitem__("version", 1))
        self.rejects(lambda d: d["codeScopePresentations"].__setitem__("version", True))
        self.rejects(lambda d: d["codeScopePresentations"]["entries"][0].__setitem__("representation", 0))

    def test_math_version_one_tree_remains_identical_under_version_two(self):
        value = "return 0;"
        document = {
            "stateLayout": {},
            "codeScopePresentations": {"version": 1, "entries": [{
                "block": "math", "representation": 0, "language": "cpp",
                "root": {"id": "main", "title": "Cuerpo de main", "children": [{
                    "id": "return", "source": "/lines/0", "sourceSha256": digest(value)}]}}]},
        }
        owners = {"math": {"type": "math_puzzle", "representations": [{"lines": [value]}]}}
        expected = {("math", 0): {"key": "main", "text": "Cuerpo de main", "code": False,
                               "children": [{"key": "return", "text": value, "code": True, "children": []}]}}
        self.assertEqual(scopes.validate(document, blocks=owners), expected)
        document["codeScopePresentations"]["version"] = 2
        self.assertEqual(scopes.validate(document, blocks=owners), expected)


if __name__ == "__main__":
    unittest.main()

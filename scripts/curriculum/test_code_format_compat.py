"""Source-only formatting and historical state compatibility contracts."""
from __future__ import annotations

from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import unittest

from format_batch_history import (
    Publication, active_owners, publication_bytes, verify_publication_bytes,
)

ROOT = Path(__file__).parent
fmt_spec = importlib.util.spec_from_file_location("code_format_compat", ROOT / "code_format_compat.py")
fmt = importlib.util.module_from_spec(fmt_spec)
fmt_spec.loader.exec_module(fmt)
layout_spec = importlib.util.spec_from_file_location("stable_state_layout", ROOT / "stable_state_layout.py")
layout = importlib.util.module_from_spec(layout_spec)
layout_spec.loader.exec_module(layout)

# Both inputs are complete Git publications.  The first has no format archive
# for the owners below; the second is the publication that exposed the fixture
# assumptions.  English/French bytes are identical in these two real releases.
REAL_FORMAT_COMMITS = {
    "first-format": "921e1df9171992ab7a9f1b3803b963b02fd45bbc",
    "coexisting-format": "50b479912dd0e598ad0b717332a15bf7d8606e69",
}
REAL_FORMAT_PINS = {
    "programming/cpp_fundamentals/aleatoriedad_y_reproducibilidad/lesson_combinar_ensayos_generados_y_casos_elegidos.json": {
        "es": [
            "26da228b3df2d13088897768a4823ff641fe7b0babe1788093c6c60216a86e6a",
            "b5319bcf554f6f5347375feee6c2ab778f50505cc6c013e2b101bffc74fbaf7d"
        ],
        "en": "de1956f975fb4c876d95acd9502d73eb71e2b796aa34820e2fdb668e4a416194",
        "fr": "2a76dfb5c62cef396dd283b16d5c1a3a0690665406d6cbc1ae0a9b031497ec43"
    },
    "programming/cpp_fundamentals/archivos_y_programa_completo/lesson_cerrar_reemplazar_y_anadir_datos.json": {
        "es": [
            "a4c53aaac795de6410e137d0db89e0f607f5aad8d1779bd8a64111e8a993ebea",
            "1c906e101da5593c3686184b28ce1ae4c7d33f80a3304f0212c518de95a52f7d"
        ],
        "en": "e1ee5510ea9a7e2451103b298f29c695a23cf6d76cdd95cef245b376988501cd",
        "fr": "d1c22d2c6da13a48bd2310fd72235319d6a3de899bd129444e4853e2e0c426f8"
    },
    "programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_nombrar_alternativas_con_enum_class.json": {
        "es": [
            "e8e154b4e95f36f477a1585377ea36090946d43a252617fe5017dc5646647fa4",
            "cfef31a0b8261b74ed64c55ed19604712dfb16be8e072d5d548e3a17925eaef3"
        ],
        "en": "22bc8b1b2d4ce6410e8d9d3d265ae573f261bdbb09e3b86adf99fb1bb5bb58ec",
        "fr": "e972b4f72c9899f828405d981d0d4f9796911d8b3300feb2cd672dfc06698b76"
    },
    "programming/cpp_fundamentals/errores_y_construccion_gradual/lesson_validar_antes_y_elegir_reporte.json": {
        "es": [
            "d65de29e472e1f1518559c7e6e785d68943bf1378ff80f0b55278786bbd9f788",
            "84ae6c2c5f4bd7da82782def62d1b6adf9f6a78e4fad440c6e5a6c1206273c80"
        ],
        "en": "0e0eab9f7fda4ea638596f1f81e5e523ef0fbd9fc59ef42646ed1e765168c4d0",
        "fr": "9ae89d6b6d74e9b65fc5b70efec82d3b80f9fd84bc384d07b97f222844a25288"
    },
    "programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json": {
        "es": [
            "b1d0206bc4ea3e3fcb960cb0b91322a850be3ddb2ef3a34221334215e24abfe2",
            "7b8704f4d738e56cb873358929b36b49ca9f159f2652fa414470395c1408f1a9"
        ],
        "en": "b821159b28e8165c10e4bf6b23361b59d8eba0e1213dde4c3f617952616d86b6",
        "fr": "853f786bf08cb1ea0434df6ac45026b5c95de6cee09c26097feda5dae92de83b"
    },
    "programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json": {
        "es": [
            "6ce999bc1d2ed19ce71c5f995380c51f4ea734fc2ae04ed9915fa7bdf0a4cbb8",
            "65b762c8cff56c1fbf5c33094be61dc3d0e989ad1798d110cf80344b21e91393"
        ],
        "en": "4225fef70fc80bdd8323a1e4b6f9284d0c0f6d6e6bb3366a039edefbcfa4078b",
        "fr": "1ce3751a6d16370ac784eb783df3bf74dce26fdbb6c90b38b3bbd6cf2f7e34f7"
    },
    "programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_numeros_de_varios_digitos_y_limites.json": {
        "es": [
            "6b4187b4411467892240ae00b0311d325b48150f90fd547c706bb5e93cfc0f0d",
            "605e256e4bb03ce748160bac51c7d3af95191d68fff0ffe661bed34d0ffd64b2"
        ],
        "en": "f52fede751dcdcbf93465cf67e84704dcb871fc9197b56e552021255ffe0b4a5",
        "fr": "66d3d0f63299004ded9d190af1d3cf04c908246d18625049fa7f1294d85bc714"
    },
    "programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_resto_y_asociacion_en_el_interprete.json": {
        "es": [
            "6ca35f0d3cb348e453009a3fa8d98e8e45efd927d211598265828a3f3320895d",
            "b7179c7e6a1e5e0a51106878a556a08b7a7606b0552910b97c1d16651db6b205"
        ],
        "en": "dfe05ab3d615f38091aecbfd3bd8ef49cec0b489cdd55a31c21aa729ad2ec8cf",
        "fr": "fa5eae36ab2bc32544d37839476e63ae7f687228d80ffcfbbaa7870552a93f87"
    },
    "programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_signos_unarios_parentesis_y_progreso_recursivo.json": {
        "es": [
            "222111911e25b890ea9a8582b7d66d3394f69178b219c9c5282bd61c8fd69a49",
            "88534164406bfcf3ed951e029eef924e9d1de1ff4488d3dd26b8ba791c4462b2"
        ],
        "en": "22a26b0108a011becc8d718694b622c467c50693cb3f4975c4ee9a5eccd5b1ea",
        "fr": "a394dc2158317e2c7b3ee5298a970d79b449269b0432dd452513d91dab21dd5c"
    },
    "programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_terminos_prioridad_y_division_segura.json": {
        "es": [
            "c5f88dcd96187ebd74e3a40221d79893a979991d073a1e9aaad0309a07d920ac",
            "4f672a4b47a8a641678219709d0d28cd9daf602c2992fce8d81fe48df5c283b6"
        ],
        "en": "d650e04c0b2d5ab59ec459e21c65964e5e29a9989fc99a14d94ca770f3eec631",
        "fr": "5a3166666b5a0f818e88dd465c9804567dff6cca8097f49add5c2a248b2a2471"
    },
    "programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_tokens_con_categoria_y_valor.json": {
        "es": [
            "82bb7c21d96e30674b470beeed3cc19a4c4d2b0fd21409163579678ae45173db",
            "5cf2807129ac1b3fbb42fe0dea9576db74a7d5a47db32e89dea386f24ccbf870"
        ],
        "en": "067b6be16cd36e5606d0c45a99ef50fa573c7f42812617df34243b7265e3f603",
        "fr": "21d0dd8dd3394b10677a08994e19f2fd18a98d19ed64b29b232c81945412a6b2"
    },
    "programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json": {
        "es": [
            "70772736b2b4ba184a33afbd1348fbbe5d6ff342cd96cb6f386b1b33d1d0d331",
            "ae347151fd875d7dc0f1b5449a6087050e13cbd7c33597641c5fa7671ac244ac"
        ],
        "en": "482d555a8f8abc517c362acc608dac5e5a3110b058a7b72035dab940a245400b",
        "fr": "f2155f5046c14bbb0827057569bb92a160c8347f48d969221227c6cac40f5bcf"
    }
}


def real_format_path(locale: str, relative: str) -> Path:
    return Path(__file__).resolve().parents[2] / "app/src/main/assets/content" / locale / relative


def real_format_family(relative: str, publication: str) -> dict:
    if publication == "working-tree":
        return {locale: json.loads(real_format_path(locale, relative).read_bytes())
                for locale in ("es", "en", "fr")}
    commit = REAL_FORMAT_COMMITS[publication]
    edition = tuple(REAL_FORMAT_COMMITS).index(publication)
    family = {}
    for locale, digests in REAL_FORMAT_PINS[relative].items():
        digest = digests[edition] if locale == "es" else digests
        expected = Publication(commit, digest)
        path = f"app/src/main/assets/content/{locale}/{relative}"
        payload = publication_bytes(Path(__file__).resolve().parents[2], path, expected)
        family[locale] = json.loads(verify_publication_bytes(payload, expected))
    return family


def fixture():
    return {
        "id": "cpp_fundamentals__source_format_qa__source_format_qa",
        "title": "Contrato de formato",
        "pages": [{"blocks": [
            {"type": "code", "code": "#include <iostream>\nint main(){int number=2;return number;}\n"},
            {"type": "output_prediction", "question": "¿Qué imprime?",
             "code": "int number=2;\nstd::cout << number;", "answers": ["2"]},
            {"type": "order_code", "question": "Ordena",
             "fragments": ["#include <iostream>\n", "int main(){", "return 0;", "}"],
             "correctOrder": [0, 1, 2, 3]},
            {"type": "drag_fill_code", "question": "Completa",
             "template": "int main(){\n[]\nreturn 0;\n}",
             "choices": ["int n=2;"], "correctOrder": [0]},
            {"type": "cpp_compiler", "initialCode": "int main(){return 0;}",
             "projectFiles": [
                 {"path": "CMakeLists.txt", "initialCode":
                  "cmake_minimum_required(VERSION 3.20)\nproject(QA LANGUAGES CXX)\nadd_executable(example main.cpp)\n"},
                 {"path": "main.cpp", "initialCode": "int main(){return 0;}\n"}],
             "cmake": {"target": "example", "buildType": "Debug"},
             "outputCheck": {"cases": [{"standardInput": "", "outputs": [""]}]}},
        ]}]
    }

def edited(source):
    out = deepcopy(source)
    blocks = out["pages"][0]["blocks"]
    blocks[0]["code"] = "#include <iostream>\n\nint main() {\n    int number = 2;\n\n    return number;\n}\n"
    blocks[1]["code"] = "int number = 2;\n\nstd::cout << number;"
    blocks[2]["fragments"][1] = "int main() {\n"
    blocks[2]["fragments"][2] = "    return 0;"
    blocks[3]["template"] = "int main() {\n\n[]\n\nreturn 0;\n}"
    blocks[3]["choices"][0] = "int n = 2;"
    blocks[4]["initialCode"] = "int main() {\n  return 0;\n}"
    blocks[4]["projectFiles"][0]["initialCode"] = (
        "cmake_minimum_required(VERSION 3.20)\n\n"
        "project(QA LANGUAGES CXX)\n\nadd_executable(\n  example\n  main.cpp\n)\n")
    blocks[4]["projectFiles"][1]["initialCode"] = "int main() {\n    return 0;\n}\n"
    return out

def mark_authored_explanation(document):
    """The QA author must mark revised public prose, including empty spans.

    A format-only transition must never synthesize this provenance on
    behalf of a lesson. Tests that change /explanation supply its marker.
    """
    identity = "block_0000_0001"
    overlay = document.setdefault("textSemantics", {"version": 1, "fields": []})
    assert not any(item["block"] == identity and item["path"] == "/explanation"
                   for item in overlay["fields"])
    overlay["fields"].append(layout.text_semantics.make_field(
        document, identity, "/explanation", []))


def ucn_boundary_sources():
    # The single removed space changes a local declaration (return 7) into
    # assignment to the global a-alpha (return 2).
    before = r"""using a = int;
int a\u03B1 = 7;
int main() {
    a \u03B1 = 2;
    return a\u03B1;
}
"""
    return before, before.replace(r"a \u03B1 = 2;", r"a\u03B1 = 2;")

class SourceFormatTest(unittest.TestCase):
    def test_formatting_preserves_every_code_location(self):
        before = fixture()
        after = edited(before)
        for a, b in zip(before["pages"][0]["blocks"], after["pages"][0]["blocks"]):
            self.assertTrue(fmt.require_source_format(a, b))
            self.assertEqual(fmt.source_projection(b, a), a)

    def test_unsafe_semantic_changes_are_rejected(self):
        before = fixture()
        examples = [
            ("code", "int n=1; // reason\nreturn n;", "int n=1; // reason return n;"),
            ("code", 'const char* s="one\\ntwo";', 'const char* s="one\ntwo";'),
            ("code", 'const char* s=R"raw(one\ntwo)raw";', 'const char* s=R"raw(one\n\ntwo)raw";'),
            ("code", "int n=1;return n;", "int n=2;return n;"),
            ("code", "int n=x+ +y;", "int n=x++y;"),
            ("code", "int n=0;/* note */return n;", "int n=0;/* changed */return n;"),
        ]
        for kind, source, target in examples:
            with self.subTest(source=source, target=target), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format({"type": kind, "code": source},
                                          {"type": kind, "code": target})
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format({"type": "code", "code": "#define X \\\n  1\nint x=X;\n"},
                                      {"type": "code", "code": "#define X \\\n  1\n\nint x=X;\n"})
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format({"type": "cpp_compiler", "projectFiles": [
                {"path": "CMakeLists.txt", "initialCode": "message(\"a\\nb\")"}]},
                {"type": "cpp_compiler", "projectFiles": [
                    {"path": "CMakeLists.txt", "initialCode": "message(\"a\nb\")"}]})

    def test_source_line_numbers_and_udl_token_boundaries_are_protected(self):
        code_cases = [
            ('int main(){return __LINE__;}',
             'int main(){\nreturn __LINE__;\n}'),
            ('auto line = std::source_location::current().line();',
             'auto line =\nstd::source_location::current().line();'),
            ('auto size = "abc"_length;',
             'auto size = "abc" _length;'),
            ('auto size = R"tag(abc)tag"_length;',
             'auto size = R"tag(abc)tag" _length;'),
        ]
        for before, after in code_cases:
            with self.subTest(before=before, after=after), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format({"type": "code", "code": before},
                                          {"type": "code", "code": after})
        cmake_before = 'message("${CMAKE_CURRENT_LIST_LINE}")\nmessage(STATUS "ok")'
        cmake_after = '\n' + cmake_before
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format({"type": "code", "language": "cmake", "code": cmake_before},
                                      {"type": "code", "language": "cmake", "code": cmake_after})

    def test_cpp_live_ucn_boundaries_cannot_receive_format_equivalence(self):
        before, after = ucn_boundary_sources()
        for validate in (fmt.require_source_lexical, fmt.require_source_format):
            for source, target in ((before, after), (after, before),
                                   (before, "\n" + before)):
                with self.subTest(validator=validate.__name__, target=target):
                    with self.assertRaisesRegex(
                            fmt.SourceFormatError, r"C\+\+ escape token boundaries"):
                        validate({"type": "code", "code": source},
                                 {"type": "code", "code": target})
            for source in (before, after):
                with self.subTest(validator=validate.__name__, identical=source):
                    original = {"type": "code", "code": source}
                    self.assertFalse(validate(original, deepcopy(original)))

    def test_cpp_opaque_ucn_text_keeps_safe_formatting_available(self):
        sources = [
            r"// \u03B1 is explanatory text." + "\nint main(){return 0;}",
            r"/* \u03B1 is explanatory text. */ int main(){return 0;}",
            r'const char* text = "\\u03B1"; int main(){return 0;}',
            r'const char* raw = R"note(\u03B1)note"; int main(){return 0;}',
            r"#define a\u03B1 7" + "\nint main(){return 0;}",
        ]
        for validate in (fmt.require_source_lexical, fmt.require_source_format):
            for source in sources:
                with self.subTest(validator=validate.__name__, opaque=source):
                    self.assertTrue(validate(
                        {"type": "code", "code": source},
                        {"type": "code", "code": "\n" + source}))

    def test_ucn_boundary_change_cannot_create_a_state_format_archive(self):
        before, after = ucn_boundary_sources()
        source = fixture()
        source["pages"][0]["blocks"][0]["code"] = before
        family = {"es": deepcopy(source)}
        previous = layout.prepare_v2(source, "es", family)
        unchanged = deepcopy(previous)
        updated = deepcopy(previous)
        updated["pages"][0]["blocks"][0]["code"] = after
        with self.assertRaisesRegex(layout.LayoutError, r"C\+\+ escape token boundaries"):
            layout.prepare_v2(updated, "es", family, previous)
        self.assertEqual(previous, unchanged)
        self.assertNotIn("sourceFormatRevisions", previous["stateLayout"])

    def test_cpp_header_unit_import_keeps_exact_header_name(self):
        # C++23 header imports preserve the spelling inside <...>, including
        # spaces. Ordinary indentation outside that token is still editable.
        allowed = [
            ("import <vector>;\nint main(){return 0;}\n",
             "import <vector>;\n\nint main() {\n    return 0;\n}\n"),
            ("export import <local/header.hpp>;\nint main(){return 0;}",
             "export import <local/header.hpp>;\n\nint main() { return 0; }"),
            ('const char* example = "import <vector>";\nint main(){}',
             'const char* example = "import <vector>";\n\nint main() {}'),
        ]
        for before, after in allowed:
            with self.subTest(before=before):
                self.assertTrue(fmt.require_source_format(
                    {"type": "code", "code": before},
                    {"type": "code", "code": after}))
        unsafe = [
            ("import <vector>;", "import < vector >;"),
            ("export import <local/header.hpp>;", "export import <local/ header.hpp>;"),
            ("import/* note */<vector>;", "import/* note */<vector >;"),
            ("import <vector>;", "import\n<vector>;"),
            ("import <vector>;", "import <vector\n>;"),
        ]
        for before, after in unsafe:
            with self.subTest(before=before, after=after):
                with self.assertRaises(fmt.SourceFormatError):
                    fmt.require_source_format(
                        {"type": "code", "code": before},
                        {"type": "code", "code": after})
        # The physical #include directive stays unchanged, as before.
        self.assertTrue(fmt.require_source_format(
            {"type": "code", "code": "#include <vector>\nint main() {}"},
            {"type": "code", "code": "#include <vector>\n\nint main() {}"}))

    def test_contextual_import_identifier_remains_a_comparison(self):
        # C++23 permits 'import' as an identifier outside the line-initial
        # preprocessing import context. Header-name tokens must never swallow
        # a valid pair of comparison operators.
        before = "int main(){int import=3;return import<4>1;}"
        after = "int main() {\n    int import = 3;\n    return import < 4 > 1;\n}"
        self.assertTrue(fmt.require_source_format(
            {"type": "code", "code": before},
            {"type": "code", "code": after}))
        other_before = "int main(){int import=3;return (import<4>1);}"
        other_after = "int main() {int import = 3; return (import < 4 > 1);}"
        self.assertTrue(fmt.require_source_format(
            {"type": "code", "code": other_before},
            {"type": "code", "code": other_after}))


        # Both forms are valid C++23 when 'import' is a contextual identifier.
        # A physical line break before that word does not turn comparisons
        # into a header-unit declaration.
        line_initial_before = (
            "int main(){int import=3;return (\n"
            "import<4>1\n"
            ");}")
        line_initial_after = (
            "int main() {\n    int import = 3;\n    return (\n"
            "        import < 4 > 1\n"
            "    );\n}")
        self.assertTrue(fmt.require_source_format(
            {"type": "code", "code": line_initial_before},
            {"type": "code", "code": line_initial_after}))
        # Unfinished imports must stay protected, even without ';'.
        # Comments after a real header name must not bypass protection.
        for original, altered in (
            ("import <vector> /* note */ ;",
             "import < vector > /* note */ ;"),
            ("export import <project/local.hpp> // note\n;",
             "export import <project/ local.hpp> // note\n;"),
            ("import <vector>", "import < vector >"),
        ):
            with self.subTest(import_source=original), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format(
                    {"type": "code", "code": original},
                    {"type": "code", "code": altered})

        source = fixture()
        source["pages"][0]["blocks"][0]["code"] = before
        family = {"es": deepcopy(source)}
        original = layout.prepare_v2(source, "es", family)
        proposed = deepcopy(original)
        proposed["pages"][0]["blocks"][0]["code"] = after
        revised = layout.prepare_v2(proposed, "es", family, original)
        layout.validate_transition(original, revised)
        self.assertEqual(original["stateLayout"]["origin"],
                         revised["stateLayout"]["origin"])
        self.assertEqual(original["stateLayout"]["family"],
                         revised["stateLayout"]["family"])
        self.assertEqual(1, len(revised["stateLayout"]["sourceFormatRevisions"]))
        self.assertEqual(before, revised["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"])

    def test_header_import_comment_prefix_is_still_protected(self):
        for before, after in (
            ("/* comentario */ import <vector>;",
             "/* comentario */ import < vector >;"),
            ("export/* comentario */import <vector>;",
             "export/* comentario */import < vector >;"),
            ("/* ejemplo\n comentario */ import <vector>;",
             "/* ejemplo\n comentario */ import < vector >;"),
        ):
            with self.subTest(before=before), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format(
                    {"type": "code", "code": before},
                    {"type": "code", "code": after})

    def test_header_import_spelling_cannot_enter_a_format_archive(self):
        published_source = fixture()
        published_source["pages"][0]["blocks"][0]["code"] = (
            "import <vector>;\nint main(){return 0;}\n")
        family = {"es": deepcopy(published_source)}
        original = layout.prepare_v2(published_source, "es", family)
        current = deepcopy(original)
        current["pages"][0]["blocks"][0]["code"] = (
            "import < vector >;\nint main(){return 0;}\n")
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(current, "es", family, original)
        self.assertNotIn("sourceFormatRevisions", original["stateLayout"])
        self.assertEqual(original["stateLayout"]["origin"],
                         layout.prepare_v2(published_source, "es", family)["stateLayout"]["origin"])

    def test_source_location_builtin_does_not_enter_a_format_archive(self):
        original_source = fixture()
        original_source["pages"][0]["blocks"][0]["code"] = (
            "int main(){return __builtin_source_location() ? 1 : 0;}")
        family = {"es": deepcopy(original_source)}
        published = layout.prepare_v2(original_source, "es", family)
        proposed = deepcopy(published)
        proposed["pages"][0]["blocks"][0]["code"] = (
            "int main(){\nreturn __builtin_source_location() ? 1 : 0;\n}")
        with self.assertRaisesRegex(layout.LayoutError,
                                    "Source location may depend on physical line numbers"):
            layout.prepare_v2(proposed, "es", family, published)
        self.assertNotIn("sourceFormatRevisions", published["stateLayout"])

    def test_cpp_preprocessor_digraph_and_comment_preamble_safety(self):
        # %: is the standard alternative token for # in preprocessing directives.
        allowed = [
            ("%:include <iostream>\nint main(){return 0;}\n",
             "%:include <iostream>\n\nint main() {\n    return 0;\n}\n"),
            ('const char* note="%:define X";\nint main(){}',
             'const char* note="%:define X";\n\nint main() {}'),
        ]
        for before, after in allowed:
            with self.subTest(safe=before):
                self.assertTrue(fmt.require_source_format(
                    {"type": "code", "code": before},
                    {"type": "code", "code": after}))
        blocked = [
            ("%:define X 3\nint n=X;", "%:\ndefine X 3\nint n=X;"),
            ("/* note */ %:define X 3\nint n=X;",
             "/* note */ %:\ndefine X 3\nint n=X;"),
            ("/* note */ #define X 3\nint n=X;",
             "/* note */ #\ndefine X 3\nint n=X;"),
        ]
        for before, after in blocked:
            with self.subTest(blocked=before), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format(
                    {"type": "code", "code": before},
                    {"type": "code", "code": after})

    def test_cpp_location_tokens_match_native_and_ignore_inert_mentions(self):
        allowed = [
            '// __LINE__ is documentation\nint main() {}',
            'const char* name = "__builtin_COLUMN";\nint main() {}',
            'auto literal = R"tag(__builtin_LINE and source_location)tag";',
            '/* __LINE__ does not expand here */ int main() {}',
            '// __builtin_source_location() is documentation\nint main() {}',
            'const char* diagnostic = "__builtin_source_location()";\nint main() {}',
            '#include <source_location>\nint main(){return 0;}',
            '#include "__LINE__.h" // source_location\nint main(){return 0;}',
            '%:include <source_location>\nint main(){return 0;}',
        ]
        for before in allowed:
            with self.subTest(allowed=before):
                self.assertTrue(fmt.require_source_format(
                    {"type": "code", "code": before},
                    {"type": "code", "code": "\n" + before}))
        blocked = [
            'int current = __LINE__;',
            'int current = __builtin_LINE();',
            'int current = __builtin_COLUMN();',
            'auto current = std::source_location::current();',
            'auto current = __builtin_source_location();',
            '#define CURRENT __LINE__\nint main() {}',
            '#define CAPTURE_LOCATION __builtin_source_location()\nint main() {}',
            '#include <source_location>\nauto current = std::source_location::current();',
        ]
        for before in blocked:
            with self.subTest(blocked=before), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format(
                    {"type": "code", "code": before},
                    {"type": "code", "code": "\n" + before})

    def test_cmake_legacy_arguments_not_spaced_into_different_calls(self):
        # CMake treats these legacy unquoted forms as one argument.
        allowed = [
            ('show(-Da="b c")\nmessage(ok)',
             'show(-Da="b c")\n\nmessage(ok)'),
            ('show(-Dfoo=$(BAR))\nmessage(ok)',
             'show(-Dfoo=$(BAR))\n\nmessage(ok)'),
            ('message("a b")', 'message(\n  "a b"\n)'),
        ]
        for before, after in allowed:
            with self.subTest(allowed=before):
                self.assertTrue(fmt.require_source_format(
                    {"type": "code", "language": "cmake", "code": before},
                    {"type": "code", "language": "cmake", "code": after}))
        blocked = [
            ('show(-Da="b c")', 'show(-Da= "b c")'),
            ('show(a" "b)', 'show(a " " b)'),
            ('show(-Dfoo=$(BAR))', 'show(-Dfoo=$ (BAR))'),
            ('show("x""y")', 'show("x" "y")'),
        ]
        for before, after in blocked:
            with self.subTest(blocked=before), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format(
                    {"type": "code", "language": "cmake", "code": before},
                    {"type": "code", "language": "cmake", "code": after})

    def test_cmake_command_header_and_invocation_separator_boundaries(self):
        # Verified against cmake -P: line breaks after a command name
        # or their removal between invocations change parse success.
        allowed = [
            ('message(STATUS "a")\nmessage(STATUS "b")',
             'message (STATUS "a")\n\nmessage\t(STATUS "b")'),
            ('message(STATUS "a")',
             'message(\n  STATUS "a"\n)'),
            ('if(TRUE AND (TRUE OR FALSE))\nmessage(STATUS "yes")\nendif()',
             'if(\n  TRUE AND (\n    TRUE OR FALSE\n  )\n)\n\nmessage(STATUS "yes")\nendif()'),
            ('message(STATUS "a") # note\nmessage(STATUS "b")',
             'message(STATUS "a")\n# note\nmessage(STATUS "b")'),
            ('message(STATUS "a") #[[note]]\nmessage(STATUS "b")',
             'message(STATUS "a")\n#[[note]]\nmessage(STATUS "b")'),
        ]
        for before, after in allowed:
            with self.subTest(safe=before):
                self.assertTrue(fmt.require_source_format(
                    {"type": "code", "language": "cmake", "code": before},
                    {"type": "code", "language": "cmake", "code": after}))
        blocked = [
            ('message(STATUS "ok")', 'message\n(STATUS "ok")'),
            ('message (STATUS "ok")', 'message\n(STATUS "ok")'),
            ('message(STATUS "a")\nmessage(STATUS "b")',
             'message(STATUS "a") message(STATUS "b")'),
            ('message(STATUS "a")\nmessage(STATUS "b")',
             'message(STATUS "a")\tmessage(STATUS "b")'),
            ('message(STATUS "a") message(STATUS "b")',
             'message(STATUS "a")\nmessage(STATUS "b")'),
            ('message(STATUS "a")\n#[[note]]\nmessage(STATUS "b")',
             'message(STATUS "a") #[[note]] message(STATUS "b")'),
            ('message(STATUS "a")\n#[[note]]\nmessage(STATUS "b")',
             'message(STATUS "a")\n#[[note]] message(STATUS "b")'),
            ('message(STATUS "a")\n#[[first\nsecond]]\nmessage(STATUS "b")',
             'message(STATUS "a")\n#[[first\nsecond]] message(STATUS "b")'),
        ]
        for before, after in blocked:
            with self.subTest(blocked=before), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format(
                    {"type": "code", "language": "cmake", "code": before},
                    {"type": "code", "language": "cmake", "code": after})

    def test_cmake_location_tokens_match_native_and_configure_expansion(self):
        allowed = [
            '#[=[ ${CMAKE_CURRENT_LIST_LINE} is inert ]=]\nmessage(ok)',
            'message([=[${CMAKE_CURRENT_LIST_LINE}]=])',
            'message([=[@CMAKE_CURRENT_FUNCTION_LIST_LINE@]=])',
            '# CMAKE_CURRENT_LIST_LINE is documentation\nmessage("CMAKE_CURRENT_FUNCTION_LIST_LINE")',
            'set(my_CMAKE_CURRENT_LIST_LINE 3)\nmessage(ok)',
        ]
        for before in allowed:
            with self.subTest(allowed=before):
                self.assertTrue(fmt.require_source_format(
                    {"type": "code", "language": "cmake", "code": before},
                    {"type": "code", "language": "cmake", "code": "\n" + before}))
        blocked = [
            'message("${CMAKE_CURRENT_LIST_LINE}")',
            'message("${CMAKE_CURRENT_FUNCTION_LIST_LINE}")',
            'if(CMAKE_CURRENT_LIST_LINE)\nmessage(ok)\nendif()',
            'string(CONFIGURE "@CMAKE_CURRENT_LIST_LINE@" result @ONLY)',
            'string(CONFIGURE [=[@CMAKE_CURRENT_LIST_LINE@]=] result @ONLY)',
            'file(CONFIGURE OUTPUT "out.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY)',
            'cmake_language(CALL file CONFIGURE OUTPUT "out.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY)',
            'cmake_language(CALL "file" "CONFIGURE" OUTPUT "out.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY)',
            'cmake_language(CALL "string" "CONFIGURE" [=[@CMAKE_CURRENT_FUNCTION_LIST_LINE@]=] result @ONLY)',
            'cmake_language(CALL [=[file]=] [=[CONFIGURE]=] OUTPUT "out.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY)',
            'set(command_name file)\ncmake_language(CALL "${command_name}" CONFIGURE OUTPUT "out.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY)',
            'set(subcommand CONFIGURE)\ncmake_language(CALL file "${subcommand}" OUTPUT "out.txt" CONTENT [=[@CMAKE_CURRENT_FUNCTION_LIST_LINE@]=] @ONLY)',
            'set(template [=[@CMAKE_CURRENT_FUNCTION_LIST_LINE@]=])\nSTRING(CONFIGURE "${template}" result @ONLY)',
            'cmake_language(CALL string CONFIGURE [=[@CMAKE_CURRENT_LIST_LINE@]=] result @ONLY)',
        ]
        for before in blocked:
            with self.subTest(blocked=before), self.assertRaises(fmt.SourceFormatError):
                fmt.require_source_format(
                    {"type": "code", "language": "cmake", "code": before},
                    {"type": "code", "language": "cmake", "code": "\n" + before})

    def test_answers_and_file_lists_are_not_formatting(self):
        before = fixture()
        after = edited(before)
        after["pages"][0]["blocks"][1]["answers"] = ["3"]
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format(before["pages"][0]["blocks"][1], after["pages"][0]["blocks"][1])
        after = edited(before)
        after["pages"][0]["blocks"][4]["projectFiles"][0]["path"] = "different.cmake"
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format(before["pages"][0]["blocks"][4], after["pages"][0]["blocks"][4])

    def test_v2_format_archive_preserves_all_old_storage_signatures(self):
        before = fixture()
        family = {"es": deepcopy(before)}
        original = layout.prepare_v2(before, "es", family)
        modified = edited(original)
        formatted = layout.prepare_v2(modified, "es", family, original)
        self.assertEqual(len(formatted["stateLayout"]["sourceFormatRevisions"]), 5)
        self.assertEqual(layout.validate(original), layout.validate(formatted))
        self.assertEqual(formatted["stateLayout"]["origin"], original["stateLayout"]["origin"])
        self.assertEqual(formatted["stateLayout"]["family"], original["stateLayout"]["family"])
        self.assertEqual(len(formatted["stateLayout"]["editorial"]), 5)
        layout.validate_transition(original, formatted)
        layout.validate_transition(before, formatted)
        self.assertEqual(fmt.source_projection(formatted["pages"][0]["blocks"][4],
                                                original["pages"][0]["blocks"][4]),
                         original["pages"][0]["blocks"][4])

    def test_retirement_reading_history_preserves_formatting_compatibility(self):
        # A retired, unrelated paragraph must not prevent the physical
        # formatting of code still visible in the earlier reading snapshot.
        before = fixture()
        before["pages"][0]["blocks"].append(
            {"type": "paragraph", "text": "Presentación anterior."})
        family = {"es": deepcopy(before)}
        initial = layout.prepare_v2(before, "es", family)
        obsolete = initial["stateLayout"]["current"][0]["blocks"][-1]
        retired = layout.retire(initial, [obsolete])
        self.assertEqual(len(retired["stateLayout"]["retirementHistory"]), 1)
        formatted = layout.prepare_v2(edited(retired), "es", family, retired)
        self.assertEqual(len(formatted["stateLayout"]["sourceFormatRevisions"]), 5)
        self.assertEqual(retired["stateLayout"]["retirementHistory"],
                         formatted["stateLayout"]["retirementHistory"])
        layout.validate(formatted)
        layout.validate_transition(retired, formatted)
        layout.validate_transition(initial, formatted)

    def test_archive_is_append_only_and_original_is_immutable(self):
        before = fixture(); family = {"es": deepcopy(before)}
        old = layout.prepare_v2(before, "es", family)
        revised = layout.prepare_v2(edited(old), "es", family, old)
        bad = deepcopy(revised)
        bad["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"] += " // forged"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = deepcopy(revised)
        bad["stateLayout"].pop("sourceFormatRevisions")
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = deepcopy(revised)
        bad["pages"][0]["blocks"][1]["answers"] = ["Other"]
        with self.assertRaises(layout.LayoutError): layout.validate(bad)
        bad = deepcopy(revised)
        bad["stateLayout"]["sourceFormatRevisions"][0]["id"] = "nonexistent"
        with self.assertRaises(layout.LayoutError): layout.validate(bad)

    def test_published_course_gate_accepts_only_own_archived_format(self):
        import validar_json_programacion as contract
        source = fixture()
        family = {"es": deepcopy(source)}
        previous = layout.prepare_v2(source, "es", family)
        published = layout.prepare_v2(edited(previous), "es", family, previous)
        path = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                "source_format_qa/lesson_source_format_qa.json")
        original = previous["pages"][0]["blocks"][1]
        changed = published["pages"][0]["blocks"][1]
        self.assertTrue(contract.preserves_source_format_revision(
            path, 0, 1, original, changed, previous_document=previous,
            current_document=published))
        self.assertFalse(contract.preserves_source_format_revision(
            path, 0, 1, original, changed, previous_document=previous,
            current_document=published, course_id="programming_practice_tools"))
        self.assertTrue(contract.preserves_previous_exercise(
            path, 0, 1, original, changed, previous_document=previous,
            current_document=published))
        forged = deepcopy(published)
        forged["pages"][0]["blocks"][1]["answers"] = ["different"]
        self.assertFalse(contract.preserves_source_format_revision(
            path, 0, 1, original, forged["pages"][0]["blocks"][1],
            previous_document=previous, current_document=forged))
        forgery = deepcopy(published)
        forgery["stateLayout"]["sourceFormatRevisions"][1]["block"]["code"] = "int other=1;"
        self.assertFalse(contract.preserves_source_format_revision(
            path, 0, 1, original, changed, previous_document=previous,
            current_document=forgery))

    def test_further_formatting_can_reuse_the_same_snapshot(self):
        before = fixture(); family = {"es": deepcopy(before)}
        original = layout.prepare_v2(before, "es", family)
        first = layout.prepare_v2(edited(original), "es", family, original)
        revised = deepcopy(first)
        revised["pages"][0]["blocks"][0]["code"] += "\n\n"
        second = layout.prepare_v2(revised, "es", family, first)
        self.assertEqual(first["stateLayout"]["sourceFormatRevisions"],
                         second["stateLayout"]["sourceFormatRevisions"])
        layout.validate_transition(original, second)
        layout.validate_transition(first, second)

    def test_combined_physical_format_and_explanations_preserve_response_contracts(self):
        import validar_json_programacion as course_validator

        before = fixture()
        before["pages"][0]["blocks"][1]["explanation"] = "Guía anterior."
        before["pages"][0]["blocks"][4]["outputCheck"]["cases"][0][
            "explanation"] = "Guía de la primera comprobación."
        published = layout.prepare_v2(before, "es", {"es": deepcopy(before)})
        edited = deepcopy(published)
        prediction = edited["pages"][0]["blocks"][1]
        prediction["code"] = "int number = 2;\n\nstd::cout << number;"
        prediction["explanation"] = "Guía aclarada con los mismos resultados."
        mark_authored_explanation(edited)
        compiler = edited["pages"][0]["blocks"][4]
        compiler["initialCode"] = "int main() {\n    return 0;\n}"
        compiler["outputCheck"]["cases"][0]["explanation"] = (
            "Guía aclarada sin cambiar el resultado esperado.")
        combined = layout.prepare_v2(edited, "es", {"es": published}, published)
        self.assertEqual(len(combined["stateLayout"]["sourceFormatRevisions"]), 2)
        self.assertEqual(layout.validate(published), layout.validate(combined))
        layout.validate_transition(published, combined)
        self.assertEqual(published["stateLayout"]["origin"],
                         combined["stateLayout"]["origin"])
        self.assertEqual(published["stateLayout"]["current"],
                         combined["stateLayout"]["current"])
        relative = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                    "source_format_qa/lesson_source_format_qa.json")
        for index in (1, 4):
            previous = published["pages"][0]["blocks"][index]
            current = combined["pages"][0]["blocks"][index]
            with self.subTest(owner=index):
                self.assertTrue(course_validator.preserves_source_format_revision(
                    relative, 0, index, previous, current,
                    previous_document=published, current_document=combined))
                self.assertTrue(course_validator.preserves_previous_exercise(
                    relative, 0, index, previous, current,
                    previous_document=published, current_document=combined))
        self.assertEqual(prediction["answers"], published["pages"][0]["blocks"][1]["answers"])
        self.assertEqual(compiler["outputCheck"]["cases"][0]["outputs"],
                         published["pages"][0]["blocks"][4]["outputCheck"]["cases"][0]["outputs"])

    def test_combined_format_requires_author_to_mark_the_revised_explanation(self):
        before = fixture()
        before["pages"][0]["blocks"][1]["explanation"] = "Guía histórica."
        published = layout.prepare_v2(before, "es", {"es": deepcopy(before)})
        draft = deepcopy(published)
        updated = draft["pages"][0]["blocks"][1]
        updated["code"] = "int number = 2;\n\nstd::cout << number;"
        updated["explanation"] = "Guía revisada por el autor."
        with self.assertRaisesRegex(
                layout.LayoutError, "explicit authored text semantics"):
            layout.prepare_v2(draft, "es", {"es": published}, published)
        # Even a marker for a different field is insufficient, and a marker
        # whose source fingerprint belongs to the old prose is not accepted.
        changed_path = deepcopy(draft)
        changed_path["textSemantics"] = {
            "version": 1, "fields": [
                layout.text_semantics.make_field(changed_path,
                    "block_0000_0001", "/question", [])]}
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(changed_path, "es", {"es": published}, published)
        marked = deepcopy(draft)
        mark_authored_explanation(marked)
        altered = deepcopy(marked)
        altered["textSemantics"]["fields"][0]["sourceSha256"] = "0" * 64
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(altered, "es", {"es": published}, published)
        valid = layout.prepare_v2(marked, "es", {"es": published}, published)
        layout.validate_transition(published, valid)
        self.assertEqual(layout.validate(published), layout.validate(valid))
        self.assertEqual(published["stateLayout"]["origin"], valid["stateLayout"]["origin"])
        self.assertEqual(published["pages"][0]["blocks"][1]["answers"],
                         valid["pages"][0]["blocks"][1]["answers"])

    def test_combined_format_cannot_hide_a_changed_answer_or_project_file(self):
        before = fixture()
        before["pages"][0]["blocks"][1]["explanation"] = "Explicación anterior."
        published = layout.prepare_v2(before, "es", {"es": deepcopy(before)})
        candidate = deepcopy(published)
        candidate["pages"][0]["blocks"][1]["code"] = "int number = 2;\n\nstd::cout << number;"
        candidate["pages"][0]["blocks"][1]["explanation"] = "Explicación aclarada."
        mark_authored_explanation(candidate)
        wrong_answer = deepcopy(candidate)
        wrong_answer["pages"][0]["blocks"][1]["answers"] = ["3"]
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(wrong_answer, "es", {"es": published}, published)
        wrong_question = deepcopy(candidate)
        wrong_question["pages"][0]["blocks"][1]["question"] = (
            "Pregunta sustituida sin revisión independiente")
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(wrong_question, "es", {"es": published}, published)
        wrong_path = deepcopy(candidate)
        wrong_path["pages"][0]["blocks"][4]["initialCode"] = (
            "int main() {\n    return 0;\n}")
        wrong_path["pages"][0]["blocks"][4]["projectFiles"][0]["path"] = "other.cmake"
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(wrong_path, "es", {"es": published}, published)
        wrong_oracle = deepcopy(candidate)
        wrong_oracle["pages"][0]["blocks"][4]["initialCode"] = (
            "int main() {\n    return 0;\n}")
        wrong_oracle["pages"][0]["blocks"][4]["outputCheck"]["cases"][0][
            "outputs"] = ["falsified"]
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(wrong_oracle, "es", {"es": published}, published)

    def test_prose_before_spacing_uses_one_authentic_format_archive(self):
        before = fixture()
        before["pages"][0]["blocks"][1]["explanation"] = "Descripción anterior."
        published = layout.prepare_v2(before, "es", {"es": deepcopy(before)})
        prose = deepcopy(published)
        prose["pages"][0]["blocks"][1]["explanation"] = "Descripción revisada."
        mark_authored_explanation(prose)
        prose = layout.prepare_v2(prose, "es", {"es": published}, published)
        later = deepcopy(prose)
        later["pages"][0]["blocks"][1]["code"] = (
            "int number = 2;\n\nstd::cout << number;")
        formatted = layout.prepare_v2(later, "es", {"es": published}, prose)
        layout.validate_transition(published, formatted)
        self.assertEqual(len(formatted["stateLayout"]["sourceFormatRevisions"]), 1)
        self.assertEqual(formatted["stateLayout"]["sourceFormatRevisions"][0]["block"],
                         prose["pages"][0]["blocks"][1])


    def _for_real_publications(self, relative, identities, check):
        first = real_format_family(relative, "first-format")
        coexisting = real_format_family(relative, "coexisting-format")
        old = active_owners(first["es"])[0]
        first_archives = {entry["id"] for entry in
                          first["es"]["stateLayout"].get("sourceFormatRevisions", [])}
        current_archives = {entry["id"]: entry["block"] for entry in
                            coexisting["es"]["stateLayout"]["sourceFormatRevisions"]}
        # Keep both original cases: first formatting really creates the owner
        # snapshot, while a later format release must retain that exact snapshot.
        self.assertTrue(set(identities).isdisjoint(first_archives))
        self.assertTrue(set(identities) <= current_archives.keys())
        for identity in identities:
            self.assertEqual(current_archives[identity], old[identity], identity)
        layout.validate_transition(first["es"], coexisting["es"])
        current = real_format_family(relative, "working-tree")
        layout.validate_transition(coexisting["es"], current["es"])
        for field in ("sourceFormatRevisions", "editorial"):
            published = coexisting["es"]["stateLayout"][field]
            self.assertEqual(current["es"]["stateLayout"][field][:len(published)], published)
        files = {locale: real_format_path(locale, relative).read_bytes()
                 for locale in ("es", "en", "fr")}
        for publication, family in (("first-format", first),
                                    ("coexisting-format", coexisting),
                                    ("working-tree", current)):
            with self.subTest(publication=publication, lesson=relative):
                unchanged = deepcopy(family)
                check(relative, family)
                self.assertEqual(family, unchanged)
                for locale, payload in files.items():
                    self.assertEqual(real_format_path(locale, relative).read_bytes(), payload)

    def _assert_real_format_history(self, before, after, identities):
        original, current = active_owners(before)[0], active_owners(after)[0]
        old_meta, new_meta = before["stateLayout"], after["stateLayout"]
        old = old_meta.get("sourceFormatRevisions", [])
        records = new_meta["sourceFormatRevisions"]
        old_archives = {entry["id"]: entry for entry in old}
        archives = {entry["id"]: entry for entry in records}
        self.assertEqual(len(old_archives), len(old))
        self.assertEqual(len(archives), len(records))
        self.assertEqual(records[:len(old)], old)
        missing = set(identities) - old_archives.keys()
        self.assertEqual(len(records), len(old) + len(missing))
        self.assertEqual(set(archives), set(old_archives) | set(identities))
        self.assertEqual({entry["id"] for entry in records[len(old):]}, missing)
        for identity in identities:
            expected = old_archives.get(identity, {"block": original[identity]})["block"]
            self.assertEqual(archives[identity]["block"], expected, identity)
        for identity in original.keys() - set(identities):
            self.assertEqual(current[identity], original[identity], identity)
        self.assertEqual(current.keys(), original.keys())
        editorial = old_meta["editorial"]
        self.assertEqual(new_meta["editorial"][:len(editorial)], editorial)
        expected_edges = [{"id": identity,
                           "beforeSha256": layout.fingerprint(original[identity]),
                           "afterSha256": layout.fingerprint(current[identity])}
                          for identity in identities]
        self.assertEqual(sorted(new_meta["editorial"][len(editorial):], key=lambda edge: edge["id"]),
                         sorted(expected_edges, key=lambda edge: edge["id"]))
        excluded = {"sourceFormatRevisions", "editorial"}
        self.assertEqual({key: value for key, value in old_meta.items() if key not in excluded},
                         {key: value for key, value in new_meta.items() if key not in excluded})
        self.assertEqual(layout.validate(before), layout.validate(after))

    def test_complete_real_format_publication_pins_reject_changed_bytes(self):
        for relative, locales in REAL_FORMAT_PINS.items():
            for edition, (name, commit) in enumerate(REAL_FORMAT_COMMITS.items()):
                for locale, digests in locales.items():
                    with self.subTest(lesson=relative, publication=name, locale=locale):
                        digest = digests[edition] if locale == "es" else digests
                        expected = Publication(commit, digest)
                        path = f"app/src/main/assets/content/{locale}/{relative}"
                        payload = publication_bytes(Path(__file__).resolve().parents[2], path, expected)
                        self.assertEqual(verify_publication_bytes(payload, expected), payload)
                        with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                            verify_publication_bytes(payload + b"\n", expected)

    def test_real_u7_l3_question_and_source_spacing_share_published_owner(self):
        relative = ("programming/cpp_fundamentals/errores_y_construccion_gradual/"
                    "lesson_validar_antes_y_elegir_reporte.json")
        self._for_real_publications(
            relative, ("block_0000_0003",), self._check_real_u7_question_and_source_spacing)

    def _check_real_u7_question_and_source_spacing(self, relative, family):
        """The authored question, explanation and source each keep their own proof."""
        import hashlib
        import json
        import validar_json_programacion as contract

        published = family["es"]
        layout.validate(published)
        identity = "block_0000_0003"
        originals, positions = active_owners(published)
        p, b = positions[identity]
        original = originals[identity]
        self.assertEqual(original["type"], "output_prediction")

        draft = deepcopy(published)
        block = draft["pages"][p]["blocks"][b]
        old_code = "    int piezas{10};\n    if (piezas < 0 || piezas > 10)"
        new_code = "    int piezas{10};\n\n    if (piezas < 0 || piezas > 10)"
        self.assertEqual(block["code"].count(old_code), 1)
        block["code"] = block["code"].replace(old_code, new_code, 1)
        block["question"] += " Examina si el extremo superior está incluido."
        block["explanation"] += " El límite superior también forma parte del intervalo."
        for field in ("/question", "/explanation"):
            marker = next(item for item in draft["textSemantics"]["fields"]
                          if item["block"] == identity and item["path"] == field)
            marker["sourceSha256"] = hashlib.sha256(
                block[field[1:]].encode("utf-8")).hexdigest()
        reviewed = layout.prepare_v2(draft, "es", family, published)
        layout.validate_transition(published, reviewed)
        self.assertEqual(layout.validate(published), layout.validate(reviewed))
        self.assertEqual(published["stateLayout"]["origin"], reviewed["stateLayout"]["origin"])
        self.assertEqual(published["stateLayout"]["current"], reviewed["stateLayout"]["current"])
        self.assertEqual(published["stateLayout"]["family"], reviewed["stateLayout"]["family"])
        self._assert_real_format_history(published, reviewed, (identity,))
        self.assertEqual(original["answers"], block["answers"])
        self.assertEqual(fmt.source_projection(block, original)["code"], original["code"])
        full_path = "app/src/main/assets/content/es/" + relative
        storage_p, storage_b = layout.validate(published)[p, b]
        self.assertTrue(contract.preserves_previous_exercise(
            full_path, storage_p, storage_b, original, reviewed["pages"][p]["blocks"][b],
            previous_document=published, current_document=reviewed))
        self.assertTrue(contract.preserves_source_format_revision(
            full_path, storage_p, storage_b, original, reviewed["pages"][p]["blocks"][b],
            previous_document=published, current_document=reviewed))

        stale = deepcopy(draft)
        old_question_marker = next(item for item in published["textSemantics"]["fields"]
                                   if item["block"] == identity and item["path"] == "/question")
        next(item for item in stale["textSemantics"]["fields"]
             if item["block"] == identity and item["path"] == "/question")[
                "sourceSha256"] = old_question_marker["sourceSha256"]
        with self.assertRaises(layout.LayoutError):
            layout.prepare_v2(stale, "es", family, published)

        forged = deepcopy(reviewed)
        forged["pages"][p]["blocks"][b]["answers"] = ["Falso"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, forged)

        changed_code = deepcopy(reviewed)
        changed_code["pages"][p]["blocks"][b]["code"] = block["code"].replace(
            "piezas > 10", "piezas >= 10")
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, changed_code)

    def test_combined_question_cannot_repaint_unchanged_explanation(self):
        relative = ("programming/cpp_fundamentals/errores_y_construccion_gradual/"
                    "lesson_validar_antes_y_elegir_reporte.json")
        self._for_real_publications(
            relative, ("block_0000_0003",), self._check_combined_question_preserves_unchanged_explanation)

    def _check_combined_question_preserves_unchanged_explanation(self, relative, family):
        import hashlib
        import json

        published = family["es"]
        identity = "block_0000_0003"
        p, b = active_owners(published)[1][identity]
        draft = deepcopy(published)
        block = draft["pages"][p]["blocks"][b]
        block["code"] = block["code"].replace(
            "    int piezas{10};\n    if (", "    int piezas{10};\n\n    if (", 1)
        block["question"] += " Contrasta el extremo superior."
        marker = next(item for item in draft["textSemantics"]["fields"]
                      if item["block"] == identity and item["path"] == "/question")
        marker["sourceSha256"] = hashlib.sha256(block["question"].encode()).hexdigest()
        accepted = layout.prepare_v2(draft, "es", family, published)
        layout.validate_transition(published, accepted)
        self._assert_real_format_history(published, accepted, (identity,))

        forged = deepcopy(draft)
        explanation = next(item for item in forged["textSemantics"]["fields"]
                           if item["block"] == identity and item["path"] == "/explanation")
        self.assertTrue(explanation["spans"])
        explanation["spans"][0]["role"] = "prose"
        # The draft's source-format change is not yet archived, so validating
        # it before prepare_v2 would reject the missing editorial history.
        with self.assertRaisesRegex(layout.LayoutError, "repainted an unchanged public field"):
            layout.prepare_v2(forged, "es", family, published)

    def test_real_u7_combined_format_fixtures_keep_authentic_history(self):
        """The exported Win32 input is a read-only copy of the real U7L3."""
        from code_format_fixture_source import real_question_spacing_fixtures

        documents = real_question_spacing_fixtures()
        before = documents["u7-question.before"]
        after = documents["u7-question.after"]
        identity = before["stateLayout"]["current"][0]["blocks"][3]
        self.assertEqual(identity, "block_0000_0003")
        self.assertEqual(before["pages"][0]["blocks"][3]["answers"],
                         after["pages"][0]["blocks"][3]["answers"])
        self.assertNotEqual(before["pages"][0]["blocks"][3]["question"],
                            after["pages"][0]["blocks"][3]["question"])
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        for forged in ("u7-question.changed-answer",
                       "u7-question.stale-question",
                       "u7-question.unmarked-question",
                       "u7-question.forged-source"):
            with self.subTest(forged=forged), self.assertRaises(layout.LayoutError):
                layout.validate(documents[forged])


    def test_real_u9_l3_three_source_and_explanation_revisions(self):
        relative = ("programming/cpp_fundamentals/datos_propios_y_estado_valido/"
                    "lesson_nombrar_alternativas_con_enum_class.json")
        self._for_real_publications(
            relative, ("block_0001_0002", "block_0002_0002", "block_0004_0003"), self._check_real_u9_sources_and_explanations)

    def _check_real_u9_sources_and_explanations(self, relative, family):
        """The reported enum-class slots can change spacing with authored prose.

        Nothing is written back to Work's lessons. All three published blocks
        keep their answers, owners, and authored spans in memory.
        """
        import hashlib
        import json
        import validar_json_programacion as contract

        before = family["es"]
        layout.validate(before)
        identities = ("block_0001_0002", "block_0002_0002", "block_0004_0003")
        positions = [(*active_owners(before)[1][identity], identity) for identity in identities]
        changed = deepcopy(before)
        original_sources = {}
        for page, index, identity in positions:
            self.assertEqual(before["stateLayout"]["current"][page]["blocks"][index], identity)
            initial = before["pages"][page]["blocks"][index]
            updated = changed["pages"][page]["blocks"][index]
            self.assertIn(initial["type"], ("fill_code", "output_prediction"))
            before_code = "    std::cout"
            self.assertEqual(initial["code"].count(before_code), 1)
            updated["code"] = initial["code"].replace(
                before_code, "\n    std::cout", 1)
            updated["explanation"] += (
                " El valor conserva el tipo Estado hasta que el ejemplo "
                "realiza la operación indicada.")
            marker = next(item for item in changed["textSemantics"]["fields"]
                          if item["block"] == identity and item["path"] == "/explanation")
            self.assertTrue(marker["spans"])
            marker["sourceSha256"] = hashlib.sha256(
                updated["explanation"].encode("utf-8")).hexdigest()
            original_sources[identity] = initial["code"]
            self.assertTrue(fmt.require_source_lexical(initial, updated))
            self.assertEqual(initial["answers"], updated["answers"])

        reviewed = layout.prepare_v2(changed, "es", family, before)
        layout.validate_transition(before, reviewed)
        self.assertEqual(layout.validate(before), layout.validate(reviewed))
        self.assertEqual(before["stateLayout"]["origin"], reviewed["stateLayout"]["origin"])
        self.assertEqual(before["stateLayout"]["family"], reviewed["stateLayout"]["family"])
        self.assertEqual(before["stateLayout"]["current"], reviewed["stateLayout"]["current"])
        self._assert_real_format_history(before, reviewed, identities)
        path = "app/src/main/assets/content/es/" + relative
        for page, index, identity in positions:
            previous_block = before["pages"][page]["blocks"][index]
            revised_block = reviewed["pages"][page]["blocks"][index]
            self.assertEqual(previous_block["code"], original_sources[identity])
            self.assertEqual(previous_block["answers"], revised_block["answers"])
            storage_p, storage_b = layout.validate(before)[page, index]
            self.assertTrue(contract.preserves_previous_exercise(
                path, storage_p, storage_b, previous_block, revised_block,
                previous_document=before, current_document=reviewed))
            self.assertTrue(contract.preserves_source_format_revision(
                path, storage_p, storage_b, previous_block, revised_block,
                previous_document=before, current_document=reviewed))
        tampered = deepcopy(reviewed)
        active_owners(tampered)[0]["block_0002_0002"]["answers"] = ["4"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, tampered)
        tampered = deepcopy(reviewed)
        tampered_block = active_owners(tampered)[0]["block_0004_0003"]
        tampered_block["code"] = (
            tampered_block["code"].replace(
                "Estado::cancelado", "Estado::preparado", 1))
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(before, tampered)


    def test_real_format_reorder_keeps_historical_addresses_and_archives(self):
        relative = ("programming/cpp_fundamentals/datos_propios_y_estado_valido/"
                    "lesson_nombrar_alternativas_con_enum_class.json")
        moved = ("block_0001_0002", "block_0002_0002")
        for publication in REAL_FORMAT_COMMITS:
            with self.subTest(publication=publication):
                family = real_format_family(relative, publication)
                unchanged = deepcopy(family)
                before = family["es"]
                positions = active_owners(before)[1]
                plan = deepcopy(before["stateLayout"]["current"])
                (p, b), (q, c) = (positions[owner] for owner in moved)
                plan[p]["blocks"][b], plan[q]["blocks"][c] = moved[1], moved[0]
                reordered = layout.reorder(before, plan)
                after_positions = active_owners(reordered)[1]
                old_storage, storage = layout.validate(before), layout.validate(reordered)
                for owner in moved:
                    self.assertNotEqual(positions[owner], after_positions[owner])
                    self.assertEqual(old_storage[positions[owner]], storage[after_positions[owner]])
                for field in ("sourceFormatRevisions", "editorial"):
                    self.assertEqual(before["stateLayout"].get(field),
                                     reordered["stateLayout"].get(field))
                # This also changes the visual order of newly archived owners.
                # Canonical snapshots and guard addresses still bind by identity.
                self._check_real_u9_sources_and_explanations(
                    relative, {**family, "es": reordered})
                self.assertEqual(family, unchanged)

    def test_real_u10_l4_combined_format_and_authored_guide(self):
        relative = ("programming/cpp_fundamentals/aleatoriedad_y_reproducibilidad/"
                    "lesson_combinar_ensayos_generados_y_casos_elegidos.json")
        self._for_real_publications(
            relative, ("block_0004_0007",), self._check_real_u10_compiler_and_authored_guide)

    def _check_real_u10_compiler_and_authored_guide(self, relative, family):
        """Authenticate a real published compiler owner without editing its JSON."""
        import hashlib
        import json
        import validar_json_programacion as contract

        published = family["es"]
        layout.validate(published)
        identity = "block_0004_0007"
        originals, positions = active_owners(published)
        p, b = positions[identity]
        original = originals[identity]
        self.assertEqual(original["type"], "cpp_compiler")
        before_code = "    int cantidad=0;\n    for(int dato:datos)"
        after_code = "    int cantidad=0;\n\n    for(int dato:datos)"
        self.assertEqual(original["initialCode"].count(before_code), 1)

        updated = deepcopy(published)
        editor = updated["pages"][p]["blocks"][b]
        editor["initialCode"] = editor["initialCode"].replace(
            before_code, after_code, 1)
        case = editor["outputCheck"]["cases"][0]
        case["explanation"] += (
            "\nRevisa que la igualdad no se cuente con la condición estricta.")
        marker = next(field for field in updated["textSemantics"]["fields"]
                      if field["block"] == identity and
                      field["path"] == "/outputCheck/cases/0/explanation")
        # Appending prose must preserve every genuine semantic span.
        published_marker = next(field for field in published["textSemantics"]["fields"]
                                if field["block"] == identity and
                                field["path"] == "/outputCheck/cases/0/explanation")
        self.assertTrue(published_marker["spans"])
        self.assertEqual(marker["spans"], published_marker["spans"])
        marker["sourceSha256"] = hashlib.sha256(
            case["explanation"].encode("utf-8")).hexdigest()
        self.assertNotEqual(original["initialCode"], editor["initialCode"])

        reviewed = layout.prepare_v2(updated, "es", family, published)
        layout.validate_transition(published, reviewed)
        self.assertEqual(layout.validate(published), layout.validate(reviewed))
        self.assertEqual(reviewed["stateLayout"]["origin"],
                         published["stateLayout"]["origin"])
        self.assertEqual(reviewed["stateLayout"]["family"],
                         published["stateLayout"]["family"])
        self.assertEqual(reviewed["stateLayout"]["current"],
                         published["stateLayout"]["current"])
        self._assert_real_format_history(published, reviewed, (identity,))
        revised_marker = next(field for field in reviewed["textSemantics"]["fields"]
                              if field["block"] == identity and
                              field["path"] == "/outputCheck/cases/0/explanation")
        self.assertEqual(revised_marker["spans"], published_marker["spans"])
        self.assertEqual(reviewed["pages"][p]["blocks"][b]["outputCheck"]
                         ["cases"][0]["outputs"], original["outputCheck"]["cases"][0]["outputs"])
        full_path = "app/src/main/assets/content/es/" + relative
        storage_p, storage_b = layout.validate(published)[p, b]
        self.assertTrue(contract.preserves_source_format_revision(
            full_path, storage_p, storage_b, original, reviewed["pages"][p]["blocks"][b],
            previous_document=published, current_document=reviewed))
        self.assertTrue(contract.preserves_previous_exercise(
            full_path, storage_p, storage_b, original, reviewed["pages"][p]["blocks"][b],
            previous_document=published, current_document=reviewed))

        incorrect = deepcopy(reviewed)
        incorrect["pages"][p]["blocks"][b]["initialCode"] = (
            editor["initialCode"].replace("dato>minimo", "dato>=minimo", 1))
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, incorrect)
        incorrect = deepcopy(reviewed)
        descriptor = next(field for field in incorrect["textSemantics"]["fields"]
                          if field["block"] == identity and
                          field["path"] == "/outputCheck/cases/0/explanation")
        descriptor["sourceSha256"] = next(
            field["sourceSha256"] for field in published["textSemantics"]["fields"]
            if field["block"] == identity and
            field["path"] == "/outputCheck/cases/0/explanation")
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, incorrect)



    def test_real_u11_l11_combined_format_and_review_feedback(self):
        relative = ("programming/cpp_fundamentals/texto_interpretacion_y_flujos/"
                    "lesson_una_gramatica_para_sumar_y_restar.json")
        self._for_real_publications(
            relative, ("block_0002_0001",), self._check_real_u11_prediction_and_review_feedback)

    def _check_real_u11_prediction_and_review_feedback(self, relative, family):
        """A published prediction keeps its answer and original guide spans."""
        import hashlib
        import json
        import validar_json_programacion as contract

        published = family["es"]
        layout.validate(published)
        identity = "block_0002_0001"
        originals, positions = active_owners(published)
        p, b = positions[identity]
        original = originals[identity]
        self.assertEqual(original["type"], "output_prediction")
        before_code = "    const int resultado = leer_expresion(lector);\n    const Token fin = lector.leer();"
        after_code = "    const int resultado = leer_expresion(lector);\n\n    const Token fin = lector.leer();"
        self.assertEqual(original["code"].count(before_code), 1)

        updated = deepcopy(published)
        puzzle = updated["pages"][p]["blocks"][b]
        puzzle["code"] = puzzle["code"].replace(before_code, after_code, 1)
        puzzle["reviewFeedback"] += (
            "\nEl terminador recuperado pertenece a la misma operación.")
        marker = next(field for field in updated["textSemantics"]["fields"]
                      if field["block"] == identity and
                      field["path"] == "/reviewFeedback")
        self.assertTrue(marker["spans"])
        marker["sourceSha256"] = hashlib.sha256(
            puzzle["reviewFeedback"].encode("utf-8")).hexdigest()
        reviewed = layout.prepare_v2(updated, "es", family, published)
        layout.validate_transition(published, reviewed)
        self.assertEqual(layout.validate(published), layout.validate(reviewed))
        self.assertEqual(reviewed["stateLayout"]["origin"],
                         published["stateLayout"]["origin"])
        self.assertEqual(reviewed["stateLayout"]["current"],
                         published["stateLayout"]["current"])
        self.assertEqual(reviewed["stateLayout"]["family"],
                         published["stateLayout"]["family"])
        self._assert_real_format_history(published, reviewed, (identity,))
        self.assertEqual(puzzle["answers"], original["answers"])
        revised_marker = next(field for field in reviewed["textSemantics"]["fields"]
                              if field["block"] == identity and
                              field["path"] == "/reviewFeedback")
        published_marker = next(field for field in published["textSemantics"]["fields"]
                                if field["block"] == identity and
                                field["path"] == "/reviewFeedback")
        self.assertEqual(revised_marker["spans"], published_marker["spans"])
        full_path = "app/src/main/assets/content/es/" + relative
        storage_p, storage_b = layout.validate(published)[p, b]
        self.assertTrue(contract.preserves_source_format_revision(
            full_path, storage_p, storage_b, original, reviewed["pages"][p]["blocks"][b],
            previous_document=published, current_document=reviewed))
        self.assertTrue(contract.preserves_previous_exercise(
            full_path, storage_p, storage_b, original, reviewed["pages"][p]["blocks"][b],
            previous_document=published, current_document=reviewed))

        forged = deepcopy(reviewed)
        forged["pages"][p]["blocks"][b]["answers"] = ["Resultado falsificado"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, forged)
        stale = deepcopy(reviewed)
        marker = next(field for field in stale["textSemantics"]["fields"]
                      if field["block"] == identity and
                      field["path"] == "/reviewFeedback")
        marker["sourceSha256"] = published_marker["sourceSha256"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(published, stale)



    def _closed_compiler_family(self, spanish, relative):
        """Use every genuine locale pinned by the published v2 family.

        A source-format proof changes only the Spanish owner, but the
        historical family check must still see its unchanged English/French
        translations. Never synthesize a locale or shrink that manifest.
        """
        repository = Path(__file__).resolve().parents[2]
        baseline_ref = "52d057d1aad844b133d0d26ff195cbfcbe18b2e1"
        locales = spanish["stateLayout"]["family"]
        return {
            language: (spanish if language == "es" else layout._git_document(
                repository, baseline_ref,
                relative.replace("/es/", f"/{language}/", 1)))
            for language in locales
        }

    def _closed_compiler_format_fixture(self, name, compiler, reference, *,
                                        format_compiler=True):
        """Use the real publication before the closed compiler expansions."""
        repository = Path(__file__).resolve().parents[2]
        relative = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                    f"texto_interpretacion_y_flujos/lesson_{name}.json")
        published = layout._git_document(
            repository, "52d057d1aad844b133d0d26ff195cbfcbe18b2e1", relative)
        if published.get("stateLayout", {}).get("version") != 2:
            published = layout.prepare_v2(published, "es", {"es": published}, published)
        self.assertFalse(published["stateLayout"].get("compilerRevisions"))
        self.assertFalse(published["stateLayout"].get("sourceFormatRevisions"))
        published_copy = deepcopy(published)
        expanded = layout.revise_compiler_cases(published, compiler)
        owners = (compiler, reference) if format_compiler else (reference,)
        updated = deepcopy(expanded)
        for identity in owners:
            block = layout._blocks(updated)[identity]
            field = "initialCode" if identity == compiler else "code"
            self.assertEqual(block[field].count("    return 0;\n}\n"), 1)
            block[field] = block[field].replace(
                "    return 0;\n}\n", "\n    return 0;\n}\n", 1)
        formatted = layout.prepare_v2(updated, "es", self._closed_compiler_family(expanded, relative), expanded)
        self.assertEqual(published, published_copy)
        return relative, published, expanded, formatted, owners

    def _assert_closed_format_publication(self, relative, previous, current, owners):
        """Exercise the supplied blocks and the actual aligned publication guards."""
        import validar_json_programacion as contract

        slots = layout.validate(previous)
        aligned = layout.align_for_previous_validation(previous, current)
        historical = layout.original_projection(previous)
        for p, record in enumerate(previous["stateLayout"]["current"]):
            for b, identity in enumerate(record["blocks"]):
                if identity not in owners:
                    continue
                origin_p, origin_b = slots[p, b]
                old = previous["pages"][p]["blocks"][b]
                actual = layout._blocks(current)[identity]
                self.assertTrue(contract.preserves_source_format_revision(
                    relative, origin_p, origin_b, old, actual,
                    previous_document=previous, current_document=current), identity)
                self.assertTrue(contract.preserves_previous_exercise(
                    relative, origin_p, origin_b, old, actual,
                    previous_document=previous, current_document=current), identity)
                delivered = aligned["pages"][p]["blocks"][b]
                self.assertEqual(delivered, actual)
                if old["type"] == "cpp_compiler":
                    allowed = (contract.preserves_previous_compiler(
                        relative, origin_p, origin_b, old, delivered, historical)
                        or contract.preserves_source_format_revision(
                            relative, origin_p, origin_b, old, delivered,
                            previous_document=previous, current_document=current))
                else:
                    allowed = contract.preserves_previous_exercise(
                        relative, origin_p, origin_b, old, delivered,
                        previous_document=previous, current_document=current)
                self.assertTrue(allowed, identity)

    def test_closed_compiler_expansion_and_formatting_compose_from_real_publication(self):
        cases = (
            ("tokens_con_categoria_y_valor", "block_0004_0004", "block_0007_0001", False),
            ("tokens_con_categoria_y_valor", "block_0004_0004", "block_0007_0001", True),
            ("construir_un_interprete_con_historial_y_pruebas",
             "block_0006_0003", "block_0010_0001", True),
        )
        for name, compiler, reference, format_compiler in cases:
            with self.subTest(lesson=name, format_compiler=format_compiler):
                relative, published, expanded, formatted, owners = (
                    self._closed_compiler_format_fixture(
                        name, compiler, reference, format_compiler=format_compiler))
                # A->B and B->C already worked; A->C must follow the same
                # authenticated expansion rather than compare A's tokens/cases to C.
                layout.validate_transition(published, expanded)
                layout.validate_transition(expanded, formatted)
                layout.validate_transition(published, formatted)
                before = layout._blocks(published)
                middle = layout._blocks(expanded)
                after = layout._blocks(formatted)
                archive = expanded["stateLayout"]["compilerRevisions"][0]
                self.assertEqual(archive["block"], before[compiler])
                self.assertEqual(archive["references"],
                                 [{"id": reference, "block": before[reference]}])
                self.assertNotEqual(before[compiler]["outputCheck"],
                                    middle[compiler]["outputCheck"])
                if name == "tokens_con_categoria_y_valor":
                    self.assertEqual(before[compiler]["initialCode"],
                                     middle[compiler]["initialCode"])
                else:
                    # U11L15 changes initialCode and output together. Restoring
                    # B's source into A's old cases would create a false contract.
                    self.assertNotEqual(before[compiler]["initialCode"],
                                        middle[compiler]["initialCode"])
                for identity in owners:
                    self.assertEqual(layout.source_format_transition_predecessor(
                        published, formatted, identity), middle[identity])
                    self.assertEqual(layout.source_format_transition_predecessor(
                        expanded, formatted, identity), middle[identity])
                    self.assertTrue(fmt.require_source_format(middle[identity], after[identity]))
                    snapshot = next(item for item in
                                    formatted["stateLayout"]["sourceFormatRevisions"]
                                    if item["id"] == identity)
                    self.assertEqual(snapshot["block"], middle[identity])
                self._assert_closed_format_publication(
                    relative, published, formatted, owners)
                self._assert_closed_format_publication(
                    relative, expanded, formatted, owners)

                # A second format release must retain B's first format archive
                # and every original A compiler/reference snapshot.
                updated = deepcopy(formatted)
                for identity in owners:
                    block = layout._blocks(updated)[identity]
                    field = "initialCode" if identity == compiler else "code"
                    block[field] = block[field].replace(
                        "    return 0;\n}\n", "\n    return 0;\n}\n", 1)
                later = layout.prepare_v2(updated, "es", self._closed_compiler_family(formatted, relative), formatted)
                self.assertEqual(later["stateLayout"]["sourceFormatRevisions"],
                                 formatted["stateLayout"]["sourceFormatRevisions"])
                for document in (formatted, later):
                    self.assertEqual(document["stateLayout"]["compilerRevisions"],
                                     expanded["stateLayout"]["compilerRevisions"])
                    for field in ("origin", "current", "family", "additions"):
                        self.assertEqual(document["stateLayout"][field],
                                         published["stateLayout"][field])
                    self.assertEqual(layout.validate(document), layout.validate(published))
                for identity in owners:
                    self.assertEqual(layout.source_format_transition_predecessor(
                        formatted, later, identity), after[identity])
                for previous in (published, expanded, formatted):
                    self._assert_closed_format_publication(relative, previous, later, owners)

    def test_composed_closed_format_history_rejects_foreign_sources_and_contracts(self):
        import validar_json_programacion as contract

        relative, published, expanded, formatted, owners = self._closed_compiler_format_fixture(
            "construir_un_interprete_con_historial_y_pruebas",
            "block_0006_0003", "block_0010_0001")
        compiler, reference = owners
        before = layout._blocks(published)
        middle = layout._blocks(expanded)

        def rehash_current(document, identity):
            proof = next(item for item in reversed(document["stateLayout"]["editorial"])
                         if item["id"] == identity)
            proof["afterSha256"] = layout.fingerprint(layout._blocks(document)[identity])

        invalid = []
        for identity in owners:
            forged = deepcopy(formatted)
            block = layout._blocks(forged)[identity]
            field = "initialCode" if identity == compiler else "code"
            block[field] = block[field].replace(
                "    return 0;\n}\n", "    return 1;\n}\n", 1)
            rehash_current(forged, identity)
            invalid.append(("changed token " + identity, forged))

            forged = deepcopy(formatted)
            snapshot = next(item for item in forged["stateLayout"]["sourceFormatRevisions"]
                            if item["id"] == identity)
            snapshot["block"] = deepcopy(before[identity])
            invalid.append(("A substituted for format snapshot B " + identity, forged))

        for field, value in (("outputs", ["salida ajena\n"]), ("exitCode", 7)):
            forged = deepcopy(formatted)
            layout._blocks(forged)[compiler]["outputCheck"]["cases"][0][field] = value
            rehash_current(forged, compiler)
            invalid.append(("changed case " + field, forged))

        forged = deepcopy(formatted)
        forged["stateLayout"]["compilerRevisions"][0].pop("references")
        invalid.append(("missing paired reference", forged))
        forged = deepcopy(formatted)
        entry = forged["stateLayout"]["compilerRevisions"][0]
        entry["block"] = deepcopy(middle[compiler])
        invalid.append(("rewritten original compiler archive", forged))
        forged = deepcopy(formatted)
        proofs = forged["stateLayout"]["editorial"]
        edge = next(item for item in proofs if item["id"] == reference)
        proofs.remove(edge)
        invalid.append(("missing A-to-B proof", forged))
        forged = deepcopy(formatted)
        forged["stateLayout"]["sourceFormatRevisions"][0]["id"] = reference
        invalid.append(("borrowed format owner", forged))
        forged = deepcopy(formatted)
        forged["id"] += "_ajena"
        invalid.append(("foreign lesson", forged))

        p, b = 6, 3
        self.assertEqual(published["stateLayout"]["current"][p]["blocks"][b], compiler)
        for reason, forged in invalid:
            with self.subTest(reason=reason):
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(published, forged)
                with self.assertRaises(layout.LayoutError):
                    layout.source_format_transition_predecessor(published, forged, compiler)
                self.assertFalse(contract.preserves_source_format_revision(
                    relative, p, b, before[compiler], layout._blocks(forged)[compiler],
                    previous_document=published, current_document=forged))
        with self.assertRaises(layout.LayoutError):
            layout.source_format_transition_predecessor(
                published, formatted, "block_9999_9999")
        # The public helper cannot make a correct B snippet an old A owner, nor
        # transfer the same valid history to another lesson path.
        self.assertFalse(contract.preserves_source_format_revision(
            relative, p, b, middle[compiler], layout._blocks(formatted)[compiler],
            previous_document=published, current_document=formatted))
        self.assertFalse(contract.preserves_source_format_revision(
            relative.replace("lesson_", "lesson_ajena_", 1), p, b,
            before[compiler], layout._blocks(formatted)[compiler],
            previous_document=published, current_document=formatted))


    def test_real_u11_compiler_references_allow_later_physical_spacing(self):
        """Nine real expanded references, without replacing their paired compiler."""

        names = {
            "tokens_con_categoria_y_valor": ("block_0007_0001",),
            "numeros_de_varios_digitos_y_limites": ("block_0008_0001",),
            "una_gramatica_para_sumar_y_restar": ("block_0008_0001",),
            "terminos_prioridad_y_division_segura": ("block_0008_0001",),
            "signos_unarios_parentesis_y_progreso_recursivo": ("block_0009_0001",),
            "diagnosticar_y_recuperar_hasta_fin_de_operacion": ("block_0008_0001",),
            "construir_un_interprete_con_historial_y_pruebas":
                ("block_0009_0001", "block_0010_0001"),
            "resto_y_asociacion_en_el_interprete": ("block_0006_0001",),
        }
        for name, identities in names.items():
            relative = ("programming/cpp_fundamentals/texto_interpretacion_y_flujos/"
                        f"lesson_{name}.json")
            self._for_real_publications(
                relative, identities,
                lambda path, family, owners=identities:
                    self._check_real_u11_compiler_references(path, family, owners))

    def _check_real_u11_compiler_references(self, relative, family, identities):
        import validar_json_programacion as contract

        before_code = "    return 0;\n}\n"
        after_code = "\n    return 0;\n}\n"
        published = family["es"]
        layout.validate(published)
        old_meta = published["stateLayout"]
        historical_refs = {
            reference["id"] for owner in old_meta.get("compilerRevisions", [])
            for reference in owner.get("references", [])
        }
        updated = deepcopy(published)
        positions = {}
        for p, record in enumerate(old_meta["current"]):
            for b, identity in enumerate(record["blocks"]):
                if identity in identities:
                    positions[identity] = (p, b)
        self.assertEqual(set(positions), set(identities))
        self.assertTrue(set(identities) <= historical_refs)
        for identity in identities:
            p, b = positions[identity]
            original = published["pages"][p]["blocks"][b]
            candidate = updated["pages"][p]["blocks"][b]
            self.assertEqual(original["type"], "code")
            self.assertEqual(original["code"].count(before_code), 1)
            candidate["code"] = candidate["code"].replace(
                before_code, after_code, 1)
            self.assertTrue(fmt.require_source_format(original, candidate))
            self.assertEqual(original, fmt.source_projection(candidate, original))

        revised = layout.prepare_v2(updated, "es", family, published)
        self.assertEqual(layout.validate(published), layout.validate(revised))
        layout.validate_transition(published, revised)
        for key in ("origin", "current", "family", "additions", "compilerRevisions",
                    "choiceRevisions", "retired", "retirementHistory"):
            self.assertEqual(old_meta.get(key), revised["stateLayout"].get(key), key)
        self.assertEqual(old_meta["editorial"],
                         revised["stateLayout"]["editorial"][:len(old_meta["editorial"])])
        self._assert_real_format_history(published, revised, identities)
        for identity in identities:
            p, b = positions[identity]
            original = published["pages"][p]["blocks"][b]
            changed = revised["pages"][p]["blocks"][b]
            self.assertEqual(changed["code"], updated["pages"][p]["blocks"][b]["code"])
            storage_p, storage_b = layout.validate(published)[p, b]
            self.assertTrue(contract.preserves_source_format_revision(
                "app/src/main/assets/content/es/" + relative, storage_p, storage_b,
                original, changed, previous_document=published,
                current_document=revised))
            self.assertTrue(contract.preserves_previous_exercise(
                "app/src/main/assets/content/es/" + relative, storage_p, storage_b,
                original, changed, previous_document=published,
                current_document=revised))

        # Rehashing a different program is not a whitespace revision,
        # and rewriting the archived complete predecessor is forbidden.
        identity = identities[0]
        p, b = positions[identity]
        forged = deepcopy(revised)
        forged_block = forged["pages"][p]["blocks"][b]
        forged_block["code"] = forged_block["code"].replace(
            "    return 0;\n}\n", "    return 1;\n}\n", 1)
        proof = next(item for item in reversed(forged["stateLayout"]["editorial"])
                     if item["id"] == identity)
        proof["afterSha256"] = layout.fingerprint(forged_block)
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged)
        altered_archive = deepcopy(revised)
        snapshot = next(item for item in
                        altered_archive["stateLayout"]["sourceFormatRevisions"]
                        if item["id"] == identity)
        snapshot["block"]["code"] += "\n// forged"
        with self.assertRaises(layout.LayoutError):
            layout.validate(altered_archive)



    def test_real_u12_identical_references_preserve_distinct_owners_after_formatting(self):
        relative = ("programming/cpp_fundamentals/archivos_y_programa_completo/"
                    "lesson_cerrar_reemplazar_y_anadir_datos.json")
        self._for_real_publications(
            relative, ("block_0006_0001", "block_0008_0001"),
            self._check_real_u12_identical_references)

    def _check_real_u12_identical_references(self, relative, family):
        """Signed physical formatting is not a transfer to a matching code owner."""
        import json
        import validar_json_programacion as validator

        published = family["es"]
        layout.validate(published)
        original = published["stateLayout"]
        same_source_owner = "block_0004_0001"
        repeated = ("block_0006_0001", "block_0008_0001")
        originals, positions = active_owners(published)
        source = originals[same_source_owner]["code"]
        # If Work has already matched this exact whitespace, the two distinct
        # owners still receive one common, new physical format in this QA draft.
        if any(originals[owner]["code"] == source for owner in repeated):
            source = "\n" + source
        self.assertTrue(any(item["id"] == same_source_owner
                            for item in original.get("sourceFormatRevisions", [])))
        updated = deepcopy(published)
        for owner in repeated:
            page, index = positions[owner]
            previous = published["pages"][page]["blocks"][index]
            changed = updated["pages"][page]["blocks"][index]
            self.assertEqual(previous["type"], "code")
            self.assertEqual(changed["code"].count("    return 0;\n}\n"), 1)
            self.assertNotEqual(previous["code"], source)
            changed["code"] = source
            self.assertTrue(fmt.require_source_format(previous, changed))
            self.assertEqual(previous, fmt.source_projection(changed, previous))
        revised = layout.prepare_v2(updated, "es", family, published)
        layout.validate_transition(published, revised)
        self.assertEqual(layout.validate(revised), layout.validate(published))
        for key in ("origin", "current", "family", "compilerRevisions", "additions"):
            self.assertEqual(original.get(key), revised["stateLayout"].get(key), key)
        self.assertEqual(original["editorial"],
                         revised["stateLayout"]["editorial"][:len(original["editorial"])])
        self._assert_real_format_history(published, revised, repeated)
        for owner in repeated:
            page, index = positions[owner]
            before_block = published["pages"][page]["blocks"][index]
            after_block = revised["pages"][page]["blocks"][index]
            self.assertEqual(after_block["code"], source)
            storage_p, storage_b = layout.validate(published)[page, index]
            self.assertTrue(validator.preserves_source_format_revision(
                "app/src/main/assets/content/es/" + relative, storage_p, storage_b,
                before_block, after_block, previous_document=published,
                current_document=revised))
            self.assertTrue(validator.preserves_previous_exercise(
                "app/src/main/assets/content/es/" + relative, storage_p, storage_b,
                before_block, after_block, previous_document=published,
                current_document=revised))
        forged_source = deepcopy(revised)
        block = active_owners(forged_source)[0][repeated[0]]
        block["code"] = block["code"].replace("    return 0;\n}\n",
                                              "    return 1;\n}\n", 1)
        latest = next(edge for edge in reversed(forged_source["stateLayout"]["editorial"])
                      if edge["id"] == repeated[0])
        latest["afterSha256"] = layout.fingerprint(block)
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged_source)
        forged_archive = deepcopy(revised)
        next(entry for entry in forged_archive["stateLayout"]["sourceFormatRevisions"]
             if entry["id"] == repeated[0])["block"]["code"] += "\n// forged"
        with self.assertRaises(layout.LayoutError):
            layout.validate(forged_archive)
        borrowed = deepcopy(revised)
        borrowed["stateLayout"]["sourceFormatRevisions"] = [
            entry for entry in borrowed["stateLayout"]["sourceFormatRevisions"]
            if entry["id"] != repeated[0]]
        with self.assertRaises(layout.LayoutError):
            layout.validate(borrowed)



if __name__ == "__main__":
    unittest.main()

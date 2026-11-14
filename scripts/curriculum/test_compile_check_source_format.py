"""Windows U6L2: three reviewed compileCheck goals may gain only physical spacing.

The fixtures use the real published lesson, but never rewrite a lesson asset.
State, diagnostics, project selections, answers and archived source are pinned.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as layout
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
RELATIVE = Path("programming/cpp_fundamentals/funciones_y_contratos/"
                "lesson_declaraciones_y_firmas.json")
POSITIONS = ((14, 2), (15, 2), (17, 2))
DIAGNOSTICS = (
    "u6-difference-unselected-definition",
    "u6-difference-conflicting-return",
    "u6-difference-missing-header",
)
BLANK = "\n    return 0;\n"
SPACED = "\n\n    return 0;\n"
OTHER_BLANK = "\n    return primero - segundo;\n"
OTHER_SPACED = "\n\n    return primero - segundo;\n"


def asset(locale: str = "es") -> dict:
    return json.loads((ROOT / "app/src/main/assets/content" / locale / RELATIVE)
                      .read_text(encoding="utf-8"))


def formatted_pair() -> tuple[dict, dict]:
    before = asset()
    assert layout.validate(before)
    changed = deepcopy(before)
    for (page, index), diagnostic in zip(POSITIONS, DIAGNOSTICS):
        original = before["pages"][page]["blocks"][index]
        updated = changed["pages"][page]["blocks"][index]
        identity = before["stateLayout"]["current"][page]["blocks"][index]
        assert original["compileCheck"]["diagnostic"] == diagnostic
        assert identity not in {item["id"] for item in before["stateLayout"]["sourceFormatRevisions"]}
        assert original["initialCode"].count(BLANK) == 1
        updated["initialCode"] = original["initialCode"].replace(BLANK, SPACED, 1)
        files = {entry["path"]: entry for entry in updated["projectFiles"]}
        assert files["main.cpp"]["initialCode"] == original["initialCode"]
        files["main.cpp"]["initialCode"] = updated["initialCode"]
        other = files["diferencia.cpp"]
        assert other["initialCode"].count(OTHER_BLANK) == 1
        other["initialCode"] = other["initialCode"].replace(OTHER_BLANK, OTHER_SPACED, 1)
        assert require_source_format(original, updated)
    family = {locale: asset(locale) for locale in ("es", "en", "fr")}
    after = layout.prepare_v2(changed, "es", family, before)
    assert layout.validate(before) == layout.validate(after)
    layout.validate_transition(before, after)
    return before, after


class CompileCheckFormatTest(unittest.TestCase):
    def test_three_authentic_diagnostic_goals_keep_all_historical_owners(self):
        before, after = formatted_pair()
        old_meta, new_meta = before["stateLayout"], after["stateLayout"]
        self.assertEqual(len(old_meta["sourceFormatRevisions"]) + 3,
                         len(new_meta["sourceFormatRevisions"]))
        self.assertEqual([old_meta["current"][p]["blocks"][b] for p, b in POSITIONS],
                         [entry["id"] for entry in new_meta["sourceFormatRevisions"][-3:]])
        for field in ("origin", "current", "family", "additions", "retired",
                      "retirementHistory", "orderRefinements", "choiceRevisions",
                      "compilerRevisions"):
            self.assertEqual(old_meta.get(field), new_meta.get(field), field)
        for (page, index), diagnostic in zip(POSITIONS, DIAGNOSTICS):
            old, new = before["pages"][page]["blocks"][index], after["pages"][page]["blocks"][index]
            self.assertEqual(old["compileCheck"], new["compileCheck"])
            self.assertEqual(diagnostic, new["compileCheck"]["diagnostic"])
            self.assertEqual(old["sourceFiles"], new["sourceFiles"])
            self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(
                __import__("code_format_compat").source_projection(new, old)))
            self.assertNotEqual(old["initialCode"], new["initialCode"])
            self.assertEqual(new["initialCode"], next(item["initialCode"]
                             for item in new["projectFiles"] if item["path"] == "main.cpp"))
            self.assertEqual(old, new_meta["sourceFormatRevisions"][-3 + POSITIONS.index((page,index))]["block"])

    def test_forgeries_are_rejected_even_if_editorial_digest_is_recomputed(self):
        before, after = formatted_pair()
        def altered(mode: str, pos: int) -> dict:
            document = deepcopy(after)
            page, block = POSITIONS[pos]
            owner = document["pages"][page]["blocks"][block]
            if mode == "diagnostic":
                owner["compileCheck"]["diagnostic"] = "explicit-assignment"
            elif mode == "source":
                owner["projectFiles"][1]["initialCode"] += "\nint forged = 1;"
            elif mode == "selection":
                owner["sourceFiles"].append("unused.cpp")
            elif mode == "header":
                owner["projectFiles"][0]["path"] = "wrong.h"
            elif mode == "initial":
                owner["initialCode"] = "int main() { return 0; }"
            elif mode == "archive":
                document["stateLayout"]["sourceFormatRevisions"][-3 + pos]["block"]["initialCode"] += "// forged"
            elif mode == "answer":
                owner["outputCheck"] = {"cases": [{"standardInput": "", "outputs": [""]}]}
            else:
                raise AssertionError(mode)
            if mode != "archive":
                identity = document["stateLayout"]["current"][page]["blocks"][block]
                proof = next(entry for entry in reversed(document["stateLayout"]["editorial"])
                             if entry["id"] == identity)
                proof["afterSha256"] = layout.fingerprint(owner)
            return document
        for pos in range(3):
            for mode in ("diagnostic", "source", "selection", "header",
                         "initial", "archive", "answer"):
                with self.subTest(position=pos, mode=mode):
                    with self.assertRaises(layout.LayoutError):
                        layout.validate(altered(mode, pos))

    def test_all_remaining_reviewed_diagnostics_keep_lexical_format_and_history(self):
        """Authentic U6/U9 owners, never a lesson-specific production allowlist."""
        from code_format_compat import source_projection

        targets = {
            "funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json": (
                (16, 2, "u6-envelopes-missing-header"),
                (17, 2, "u6-envelopes-missing-declaration"),
                (18, 2, "u6-envelopes-unselected-definitions"),
                (19, 2, "u6-envelopes-duplicate-definition"),
                (21, 2, "u6-series-unknown-call"),
                (22, 2, "u6-series-unselected-definitions"),
                (23, 2, "u6-series-duplicate-definition"),
            ),
            "funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json": (
                (13, 2, "u6-shelf-unselected-definition"),
                (14, 2, "u6-shelf-conflicting-return"),
            ),
            "datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json": (
                (6, 6, "const-member-call"),
            ),
            "datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json": (
                (12, 8, "explicit-copy-initialization"),
                (12, 11, "explicit-argument-conversion"),
                (14, 11, "explicit-assignment"),
            ),
            "datos_propios_y_estado_valido/lesson_argumentos_con_tipos_que_expresan_su_funcion.json": (
                (6, 6, "swapped-named-arguments"),
            ),
        }
        # The complete source is read from the actual published asset; no
        # historical hash, option, input, library or diagnostic is fabricated.
        for relative, entries in targets.items():
            with self.subTest(lesson=relative):
                basename = Path("programming/cpp_fundamentals") / relative
                family = {
                    language: json.loads((ROOT / "app/src/main/assets/content"
                                          / language / basename).read_text(encoding="utf-8"))
                    for language in ("es", "en", "fr")
                }
                before = family["es"]
                layout.validate(before)
                changed = deepcopy(before)
                ids = []
                for page, index, diagnostic in entries:
                    with self.subTest(lesson=relative, page=page + 1, index=index + 1):
                        original = before["pages"][page]["blocks"][index]
                        current = changed["pages"][page]["blocks"][index]
                        identity = before["stateLayout"]["current"][page]["blocks"][index]
                        ids.append(identity)
                        self.assertEqual(original["type"], "cpp_compiler")
                        self.assertEqual(original["compileCheck"]["diagnostic"], diagnostic)
                        self.assertFalse(any(record["id"] == identity for record in
                                             before["stateLayout"].get("sourceFormatRevisions", [])))
                        self.assertEqual(original["initialCode"].count(BLANK), 1)
                        current["initialCode"] = current["initialCode"].replace(BLANK, SPACED, 1)
                        if "projectFiles" in current:
                            mains = [file for file in current["projectFiles"]
                                     if file["path"] == "main.cpp"]
                            self.assertEqual(len(mains), 1)
                            self.assertEqual(mains[0]["initialCode"], original["initialCode"])
                            mains[0]["initialCode"] = current["initialCode"]
                        self.assertTrue(require_source_format(original, current))
                        self.assertEqual(original, source_projection(current, original))
                after = layout.prepare_v2(changed, "es", family, before)
                self.assertEqual(layout.validate(before), layout.validate(after))
                layout.validate_transition(before, after)
                self.assertEqual(before["stateLayout"]["origin"], after["stateLayout"]["origin"])
                self.assertEqual(before["stateLayout"]["current"], after["stateLayout"]["current"])
                self.assertEqual(before["stateLayout"]["family"], after["stateLayout"]["family"])
                self.assertEqual(before.get("textSemantics"), after.get("textSemantics"))
                self.assertEqual(
                    [record["id"] for record in after["stateLayout"]["sourceFormatRevisions"]
                     [-len(entries):]], ids)
                self.assertEqual(before["stateLayout"]["editorial"],
                                 after["stateLayout"]["editorial"][
                                     :len(before["stateLayout"]["editorial"])])
                for position, (page, index, diagnostic) in enumerate(entries):
                    old = before["pages"][page]["blocks"][index]
                    new = after["pages"][page]["blocks"][index]
                    archive = after["stateLayout"]["sourceFormatRevisions"][
                        -len(entries) + position]
                    self.assertEqual(archive["block"], old)
                    self.assertEqual(old["compileCheck"], new["compileCheck"])
                    self.assertEqual(old.get("sourceFiles"), new.get("sourceFiles"))
                    self.assertEqual(old.get("cmake"), new.get("cmake"))
                    self.assertEqual(old.get("outputCheck"), new.get("outputCheck"))
                    self.assertEqual(new["compileCheck"]["diagnostic"], diagnostic)

                    # Rehashing a malicious editorial edge is not enough:
                    # lexical source, source selection and the original
                    # immutable source-format archive must all remain valid.
                    for kind in ("logic", "diagnostic", "archive", "sourceSelection"):
                        with self.subTest(owner=archive["id"], mutation=kind):
                            forged = deepcopy(after)
                            candidate = forged["pages"][page]["blocks"][index]
                            if kind == "logic":
                                candidate["initialCode"] += "\nint forged = 1;"
                            elif kind == "diagnostic":
                                candidate["compileCheck"]["diagnostic"] = "explicit-assignment"
                            elif kind == "archive":
                                forged["stateLayout"]["sourceFormatRevisions"][
                                    -len(entries) + position]["block"]["initialCode"] += "\n// forged"
                            elif kind == "sourceSelection":
                                if "sourceFiles" in candidate:
                                    candidate["sourceFiles"].append("other.cpp")
                                else:
                                    candidate["sourceFiles"] = ["other.cpp"]
                            if kind != "archive":
                                fingerprint = layout.fingerprint(candidate)
                                proof = next(item for item in reversed(forged["stateLayout"]["editorial"])
                                             if item["id"] == archive["id"])
                                proof["afterSha256"] = fingerprint
                            with self.assertRaises(layout.LayoutError):
                                layout.validate(forged)

    def test_only_in_memory_sources_change(self):
        before_bytes = (ROOT / "app/src/main/assets/content/es" / RELATIVE).read_bytes()
        before, after = formatted_pair()
        self.assertNotEqual(before, after)
        self.assertEqual(before_bytes,
                         (ROOT / "app/src/main/assets/content/es" / RELATIVE).read_bytes())


if __name__ == "__main__":
    unittest.main()

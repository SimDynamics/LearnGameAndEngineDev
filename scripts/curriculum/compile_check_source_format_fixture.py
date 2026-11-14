"""Generate isolated, authentic U6L2 compileCheck format fixtures for Win32 QA.

Published lessons and student states are only read. Files are written solely
to the CI-selected output directory.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path

import stable_state_layout as layout
from code_format_compat import require_source_format
from test_compile_check_source_format import POSITIONS, formatted_pair, ROOT, BLANK, SPACED



def additional_pairs() -> dict[str, dict]:
    """Two actual U6/U9 diagnostic owners, never modifying published files."""
    samples = (
        ("u6more", "funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json",
         16, 2, "u6-envelopes-missing-header"),
        ("u9", "datos_propios_y_estado_valido/lesson_consultas_const_y_objetos_constantes.json",
         6, 6, "const-member-call"),
    )
    generated = {}
    for tag, relative, page, index, diagnostic in samples:
        relative_path = Path("programming/cpp_fundamentals") / relative
        family = {
            locale: json.loads((ROOT / "app/src/main/assets/content" / locale
                                / relative_path).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")
        }
        before = family["es"]
        layout.validate(before)
        modified = deepcopy(before)
        original = before["pages"][page]["blocks"][index]
        active = modified["pages"][page]["blocks"][index]
        owner = before["stateLayout"]["current"][page]["blocks"][index]
        assert original["compileCheck"]["diagnostic"] == diagnostic
        assert not any(record["id"] == owner for record in
                       before["stateLayout"].get("sourceFormatRevisions", []))
        assert original["initialCode"].count(BLANK) == 1
        active["initialCode"] = original["initialCode"].replace(BLANK, SPACED, 1)
        if "projectFiles" in active:
            mains = [item for item in active["projectFiles"]
                     if item["path"] == "main.cpp"]
            assert len(mains) == 1 and mains[0]["initialCode"] == original["initialCode"]
            mains[0]["initialCode"] = active["initialCode"]
        assert require_source_format(original, active)
        after = layout.prepare_v2(modified, "es", family, before)
        layout.validate_transition(before, after)
        assert layout.validate(before) == layout.validate(after)
        assert after["stateLayout"]["sourceFormatRevisions"][-1]["block"] == original

        def forged(kind: str) -> dict:
            change = deepcopy(after)
            block = change["pages"][page]["blocks"][index]
            if kind == "source":
                block["initialCode"] += "\nint unrelated = 7;"
            elif kind == "goal":
                block["compileCheck"]["diagnostic"] = "explicit-assignment"
            elif kind == "archive":
                change["stateLayout"]["sourceFormatRevisions"][-1]["block"][
                    "initialCode"] += "\n// forged"
            else:
                raise AssertionError(kind)
            if kind != "archive":
                change["stateLayout"]["editorial"][-1]["afterSha256"] = (
                    layout.fingerprint(block))
            try:
                layout.validate(change)
            except layout.LayoutError:
                pass
            else:
                raise AssertionError(f"Forged {kind} {tag} passed the curricular validator")
            return change

        generated.update({
            f"{tag}.before": before,
            f"{tag}.after": after,
            f"{tag}.forged-source": forged("source"),
            f"{tag}.forged-goal": forged("goal"),
            f"{tag}.forged-archive": forged("archive"),
        })
    return generated


def make() -> dict[str, dict]:
    original, formatted = formatted_pair()
    tampered_goal = deepcopy(formatted)
    p, b = POSITIONS[0]
    tampered_goal["pages"][p]["blocks"][b]["compileCheck"]["diagnostic"] = "explicit-assignment"
    tampered_goal["stateLayout"]["editorial"][-3]["afterSha256"] = layout.fingerprint(
        tampered_goal["pages"][p]["blocks"][b])
    tampered_archive = deepcopy(formatted)
    tampered_archive["stateLayout"]["sourceFormatRevisions"][-1]["block"]["projectFiles"][0]["path"] = "forged.h"
    tampered_source = deepcopy(formatted)
    p, b = POSITIONS[1]
    tampered_source["pages"][p]["blocks"][b]["projectFiles"][1]["initialCode"] += "\nint forged = 99;\n"
    identity = tampered_source["stateLayout"]["current"][p]["blocks"][b]
    next(edge for edge in reversed(tampered_source["stateLayout"]["editorial"])
         if edge["id"] == identity)["afterSha256"] = layout.fingerprint(
             tampered_source["pages"][p]["blocks"][b])
    generated = {
        "u6.before": original,
        "u6.after": formatted,
        "u6.forged-goal": tampered_goal,
        "u6.forged-archive": tampered_archive,
        "u6.forged-source": tampered_source,
    }
    generated.update(additional_pairs())
    return generated


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, document in make().items():
        (args.output_dir / (name + ".json")).write_text(
            json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("PASS original U6/U9 diagnostic format pairs, historical identity and forged-project rejection")


if __name__ == "__main__":
    main()

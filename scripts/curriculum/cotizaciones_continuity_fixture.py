"""Independent QA for the published Cotizaciones CMake objective.

The predecessor is the exact Git object. The reviewed client in this fixture
is the one already exercised by cpp_cmake_runner_tests, not Work's local
candidate. No curricular asset or student profile is written.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
ASSET = ("app/src/main/assets/content/es/programming/programming_practice_tools/"
         "construccion_reproducible/lesson_opciones_y_revision_de_la_configuracion.json")
COMMIT = "9e4c2be0c7218183d9946d0a417335c807e5b49d"
BLOB = "58bd9144890708e2675270006beb68062dfc669a"
OWNER = "block_0011_0003"
PARAGRAPHS = ("block_0011_0001", "block_0011_0002", "block_0011_0004")
CLIENT = '''#include "precio.h"
#include <iostream>
int main() {
    int unidades{};
    if (std::cin >> unidades) {
        std::cout << precio(unidades);
    } else {
        std::cout << precio(0) << ':' << precio(2) << ':' << precio(5);
    }
}
'''


def original_bytes():
    raw = subprocess.check_output(["git", "show", f"{COMMIT}:{ASSET}"], cwd=ROOT)
    if hashlib.sha1(b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw).hexdigest() != BLOB:
        raise ValueError("The complete published Cotizaciones predecessor differs from its Git pin")
    return raw


def stages(*, prose_first=False):
    before = json.loads(original_bytes())
    layout.validate(before)
    assert len(before["pages"]) == 14
    assert before["stateLayout"]["current"][11]["blocks"] == [
        f"block_0011_{i:04d}" for i in range(5)]
    old = layout._blocks(before)[OWNER]
    assert old["outputCheck"]["cases"][0]["outputs"] == ["0:4:10"]
    old_case = deepcopy(old["outputCheck"]["cases"][0])
    cases = [{"standardInput": units, "outputs": [price],
              "explanation": f"La tarifa ON da {price} para {units} unidades."}
             for units, price in (("2", "4"), ("0", "0"), ("5", "10"))] + [old_case]

    def split(doc):
        preview = deepcopy(doc)
        layout._blocks(preview)[OWNER]["outputCheck"]["cases"] = deepcopy(cases)
        fields = []
        for i, case in enumerate(cases[:3]):
            path = f"/outputCheck/cases/{i}/explanation"
            fields.append(layout.text_semantics.make_field(preview, OWNER, path, [
                layout.text_semantics.select(case["explanation"], "ON", "data", reading="technical")]))
        for field in layout._compiler_case_order_fields(doc, OWNER):
            saved = deepcopy(field)
            saved["path"] = saved["path"].replace("/cases/0/", "/cases/3/", 1)
            fields.append(saved)
        return layout.revise_compiler_client_cases(
            doc, OWNER, {"path": "muestra.cpp", "source": CLIENT}, cases, case_semantics=fields)

    def prose(doc):
        draft = deepcopy(doc)
        additions = (
            "\n\nEnsayo QA: muestra.cpp lee una cantidad; la entrada vacía mantiene las tres pruebas.",
            "\n\nEnsayo QA: escribe 2 para comprobar 4; 0 y 5 son entradas opcionales.",
            "\n\nEnsayo QA: la sonda OFF mantiene 0:6:15 con entrada vacía.",
        )
        for owner, suffix in zip(PARAGRAPHS, additions):
            block = layout._blocks(draft)[owner]
            block["text"] += suffix
            marker = next(field for field in draft["textSemantics"]["fields"]
                          if (field["block"], field["path"]) == (owner, "/text"))
            spans = deepcopy(marker["spans"])
            if owner == PARAGRAPHS[-1]:
                spans.append(layout.text_semantics.select(block["text"], "0:6:15", "output"))
            marker.update(layout.text_semantics.make_field(draft, owner, "/text", spans))
        return layout.prepare_v2(draft, "es", {"es": doc}, doc)

    middle = prose(before) if prose_first else split(before)
    after = split(middle) if prose_first else prose(middle)
    layout.validate_transition(before, middle)
    layout.validate_transition(middle, after)
    return before, middle, after


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    before, _, after = stages()
    layout.validate_publication_transition(before, after)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "before.json").write_bytes(original_bytes())
    (args.output_dir / "after.json").write_bytes(
        (json.dumps(after, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    print("PASS authentic Cotizaciones predecessor and independently authored joint publication")


if __name__ == "__main__":
    main()

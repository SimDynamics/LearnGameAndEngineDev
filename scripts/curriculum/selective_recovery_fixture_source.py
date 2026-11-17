"""Byte-pinned U3 selective recovery with seven independent editor cases.

These isolated QA documents retain the complete published lesson and its
History workflow. They do not modify curriculum or treat an aggregate receipt
as evidence for any of the new individual cases.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess

import stable_state_layout as layout


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "fa84e85dcdb5cbf1671aec1d88cc0eda3bdbd31d"
ASSET = ("app/src/main/assets/content/es/programming/programming_practice_tools/"
         "comprobaciones_repetibles/"
         "lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
BLOB_SHA1 = "2e3e83245a6647917845b5a680639d8f93f43541"
BYTE_COUNT = 235865
SHA256 = "ae4b2c73b61a922a5836e4b025ad2c150efe81c4008867a733e0e4c20cab1115"
OWNER = "block_0010_0003"
PATH = "pruebas/cajas_casos.cpp"
PAIRS = ((1, 1), (0, 0), (4, 1), (8, 2), (20, 5), (5, 2), (17, 5))


@lru_cache(maxsize=None)
def historical_bytes() -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    raw = subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{COMMIT}:{ASSET}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    git_blob = b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw
    if (len(raw) != BYTE_COUNT or hashlib.sha256(raw).hexdigest() != SHA256
            or hashlib.sha1(git_blob).hexdigest() != BLOB_SHA1):
        raise ValueError("The U3 fixture differs from its complete pinned publication")
    return raw


def selective_history_publication() -> dict:
    document = json.loads(historical_bytes())
    layout.validate(document)
    owner = layout._blocks(document)[OWNER]
    if (document.get("compilerWorkflowChecks") != [
            {"activity": OWNER, "kind": "cmakeSelectiveRecoveryV1"}]
            or len(document["pages"]) != 13
            or len(owner["projectFiles"]) != 6
            or len(owner["outputCheck"]["cases"]) != 1):
        raise ValueError("The authentic U3 History owner changed its published structure")
    return document


def selective_history_cases() -> list[dict]:
    # Expectations are explicit independent pairs, never computed by cajas_para.
    cases = []
    for pieces, expected in PAIRS:
        output = (f"piezas={pieces} esperado={expected} obtenido={expected} OK\n"
                  "Bateria: OK\n")
        cases.append({
            "standardInput": f"{pieces}\n", "outputs": [output],
            "explanation": "La salida esperada es " + output.rstrip("\n")
                           + "\nLa ejecución termina con cero. El caso obligatorio conserva "
                             "además la secuencia completa de Historial."})
    return cases


def selective_history_client(document: dict) -> str:
    block = layout._blocks(document)[OWNER]
    source = next(item["initialCode"] for item in block["projectFiles"] if item["path"] == PATH)
    marker = "\nint main() {"
    if source.count(marker) != 1 or source.count("bool comprobar(int piezas, int esperado)") != 1:
        raise ValueError("The independently selected U3 checker changed its entry point")
    # Keep the published comprobar function and all its exact diagnostics.
    # Only its client selects ONE independently specified expectation per run.
    return source[:source.index(marker)] + (
        "\nint main() {\n"
        "    int piezas{0};\n"
        "    if (!(std::cin >> piezas)) { return 2; }\n"
        "    int esperado{0};\n"
        "    switch (piezas) {\n"
        "        case 0: esperado = 0; break;\n"
        "        case 1: esperado = 1; break;\n"
        "        case 4: esperado = 1; break;\n"
        "        case 5: esperado = 2; break;\n"
        "        case 8: esperado = 2; break;\n"
        "        case 17: esperado = 5; break;\n"
        "        case 20: esperado = 5; break;\n"
        "        default: return 2;\n"
        "    }\n"
        "    const bool correcto{comprobar(piezas, esperado)};\n"
        "    if (!correcto) {\n"
        '        std::cout << "Bateria: FALLO\\n";\n'
        "        return 1;\n"
        "    }\n"
        '    std::cout << "Bateria: OK\\n";\n'
        "    return 0;\n"
        "}\n")


def _case_semantics(document: dict, cases: list[dict]) -> list[dict]:
    old = layout._blocks(document)[OWNER]["outputCheck"]["cases"]
    previous = layout._compiler_case_order_fields(document, OWNER)
    view = deepcopy(document)
    layout._blocks(view)[OWNER]["outputCheck"]["cases"] = deepcopy(cases)
    fields = []
    for index, case in enumerate(cases):
        prior = next((i for i, original in enumerate(old) if original == case), None)
        if prior is not None:
            prefix = f"/outputCheck/cases/{prior}/"
            for field in previous:
                if field["path"].startswith(prefix):
                    retained = deepcopy(field)
                    retained["path"] = f"/outputCheck/cases/{index}/" + field["path"][len(prefix):]
                    fields.append(retained)
        elif "explanation" in case:
            source = case["explanation"]
            spans = []
            if source.startswith("La salida esperada es "):
                spans = [layout.text_semantics.select(
                    source, case["outputs"][0].rstrip("\n"), "output", reading="natural")]
            fields.append(layout.text_semantics.make_field(
                view, OWNER, f"/outputCheck/cases/{index}/explanation", spans))
    return fields


def selective_history_revised(document: dict, *, source=None, cases=None, return_to=None) -> dict:
    selected_source = selective_history_client(document) if source is None else source
    selected_cases = selective_history_cases() if cases is None else cases
    revised = layout.revise_compiler_client_cases(
        document, OWNER, {"path": PATH, "source": selected_source}, selected_cases,
        case_semantics=_case_semantics(document, selected_cases),
        return_to=return_to)
    if revised["compilerWorkflowChecks"] != document["compilerWorkflowChecks"]:
        raise ValueError("Individual cases must not rewrite the selective History workflow")
    return revised


def build_fixtures() -> tuple[dict, dict[str, str]]:
    before = selective_history_publication()
    after = selective_history_revised(before)
    reordered = layout.reorder_compiler_cases(after, OWNER, [0, 6, 5, 4, 3, 2, 1])
    layout.validate_transition(before, after)
    layout.validate_transition(after, reordered)
    manifest = {
        "owner": OWNER, "clientPath": PATH,
        "publication": {"commit": COMMIT, "asset": ASSET, "blobSha1": BLOB_SHA1,
                        "byteCount": BYTE_COUNT, "sha256": SHA256},
        "before": "u3-selective.before.json",
        "after": "u3-selective.after.json",
        "reordered": "u3-selective.reordered.json"}
    payloads = {
        manifest["before"]: historical_bytes().decode("utf-8"),
        manifest["after"]: json.dumps(after, ensure_ascii=False, indent=2) + "\n",
        manifest["reordered"]: json.dumps(reordered, ensure_ascii=False, indent=2) + "\n"}
    return manifest, payloads


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    manifest, payloads = build_fixtures()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for filename, source in payloads.items():
        (args.output_dir / filename).write_text(source, encoding="utf-8", newline="\n")
    (args.output_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("Prepared the pinned aggregate and two authenticated individual U3 History fixtures")


if __name__ == "__main__":
    main()

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
from test_compile_check_source_format import POSITIONS, formatted_pair


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
    return {
        "u6.before": original,
        "u6.after": formatted,
        "u6.forged-goal": tampered_goal,
        "u6.forged-archive": tampered_archive,
        "u6.forged-source": tampered_source,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, document in make().items():
        (args.output_dir / (name + ".json")).write_text(
            json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("PASS five U6L2 format fixtures: three historical diagnostic goals and three forgeries")


if __name__ == "__main__":
    main()

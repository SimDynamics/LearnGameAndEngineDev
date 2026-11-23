"""Generate authentic U6/U9 compileCheck format fixtures for Win32 QA.

The default reviews the working-tree publication without changing its history.
--publication 921e1df and --publication 83e2d4e6 select complete byte-pinned
families from Git for historical and already-formatted regression coverage.
Both selectors emit the same fixture names into independently chosen QA trees.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path

import stable_state_layout as layout
from test_compile_check_source_format import (
    DIAGNOSTICS, LESSON_ID, OWNERS, PUBLICATIONS, archive, diagnostic_groups,
    family, format_family, formatted_documents, formatted_pair, lesson_relative,
    owner, project_file, rehash_final_edge,
)


def require_rejected(document: dict, label: str) -> None:
    try:
        layout.validate(document)
    except layout.LayoutError:
        return
    raise AssertionError("Forged compile-check fixture passed the curricular validator: " + label)


def additional_pairs(publication: str = "working-tree") -> dict[str, dict]:
    """Keep the original U6/U9 loader samples selected by their stable owners."""
    samples = (
        ("u6more", "cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos",
         "block_0016_0002", "u6-envelopes-missing-header"),
        ("u9", "cpp_fundamentals__datos_propios_y_estado_valido__consultas_const_y_objetos_constantes",
         "block_0006_0006", "const-member-call"),
    )
    generated = {}
    for tag, lesson, identity, diagnostic in samples:
        before, after = format_family(family(lesson_relative(lesson), publication),
                                      ((identity, diagnostic),))

        def forged(kind: str) -> dict:
            changed = deepcopy(after)
            block = owner(changed, identity)
            if kind == "source":
                block["initialCode"] += "\nint unrelated = 7;"
            elif kind == "goal":
                block["compileCheck"]["diagnostic"] = "explicit-assignment"
            elif kind == "archive":
                archive(changed, identity)["initialCode"] += "\n// forged"
            else:
                raise AssertionError(kind)
            if kind != "archive":
                rehash_final_edge(changed, identity)
            require_rejected(changed, tag + "." + kind)
            return changed

        generated.update({
            f"{tag}.before": before, f"{tag}.after": after,
            f"{tag}.forged-source": forged("source"),
            f"{tag}.forged-goal": forged("goal"),
            f"{tag}.forged-archive": forged("archive"),
        })
    return generated


def all_reviewed_pairs(publication: str = "working-tree") -> dict[str, dict]:
    """Retain every closed diagnostic and both U6L2 translation-unit edits.

    Coverage is bound to complete lesson/owner/diagnostic identities. An archive
    may precede any number of valid publications; its list position is not its
    identity and it is never replaced with the current formatted source.
    """
    generated, lessons, observed = {}, [], set()
    legacy_before, legacy_after = formatted_pair(publication)
    retained_legacy = False
    for index, (_, before, after, goals) in enumerate(formatted_documents(publication)):
        if before["id"] == LESSON_ID:
            if before != legacy_before or after != legacy_after or retained_legacy:
                raise ValueError("The existing U6L2 source-and-implementation regression changed")
            retained_legacy = True
        prefix = f"all-{index}"
        generated[prefix + ".before"] = before
        generated[prefix + ".after"] = after
        lessons.append({
            "before": prefix + ".before.json", "after": prefix + ".after.json",
            "owners": [{"id": identity, "diagnostic": diagnostic} for identity, diagnostic in goals],
        })
        for identity, diagnostic in goals:
            key = (before["id"], identity)
            if (key in observed or key not in layout.COMPILE_DIAGNOSTICS
                    or layout.COMPILE_DIAGNOSTICS[key]["diagnostic"] != diagnostic):
                raise ValueError("The closed-diagnostic fixture has a duplicate or mismatched owner")
            observed.add(key)
    if observed != set(layout.COMPILE_DIAGNOSTICS) or not retained_legacy:
        raise ValueError("The complete published closed-diagnostic coverage changed")
    if {document["id"] for name, document in generated.items() if name.endswith(".before")} != set(diagnostic_groups()):
        raise ValueError("A reviewed diagnostic lesson was omitted")
    generated["all-diagnostics.manifest"] = {"version": 1, "lessons": lessons}
    return generated


def make(publication: str = "working-tree") -> dict[str, dict]:
    original, formatted = formatted_pair(publication)
    identities = dict(zip(DIAGNOSTICS, OWNERS))
    goal_owner = identities["u6-difference-unselected-definition"]
    tampered_goal = deepcopy(formatted)
    owner(tampered_goal, goal_owner)["compileCheck"]["diagnostic"] = "explicit-assignment"
    rehash_final_edge(tampered_goal, goal_owner)

    archive_owner = identities["u6-difference-missing-header"]
    tampered_archive = deepcopy(formatted)
    project_file(archive(tampered_archive, archive_owner), "diferencia.h")["path"] = "forged.h"

    source_owner = identities["u6-difference-conflicting-return"]
    tampered_source = deepcopy(formatted)
    project_file(owner(tampered_source, source_owner), "diferencia.cpp")["initialCode"] += "\nint forged = 99;\n"
    rehash_final_edge(tampered_source, source_owner)
    generated = {
        "u6.before": original, "u6.after": formatted,
        "u6.forged-goal": tampered_goal, "u6.forged-archive": tampered_archive,
        "u6.forged-source": tampered_source,
    }
    for name, document in generated.items():
        if ".forged-" in name:
            require_rejected(document, name)
    generated.update(additional_pairs(publication))
    generated.update(all_reviewed_pairs(publication))
    return generated


def write_fixtures(output: Path, publication: str = "working-tree") -> dict[str, dict]:
    generated = make(publication)
    output.mkdir(parents=True, exist_ok=True)
    for name, document in generated.items():
        (output / (name + ".json")).write_bytes(
            (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    return generated


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--publication", choices=PUBLICATIONS, default="working-tree",
                        help="Complete current asset or authenticated Git publication")
    args = parser.parse_args()
    write_fixtures(args.output_dir, args.publication)
    print(f"PASS {len(layout.COMPILE_DIAGNOSTICS)} original/formatted U6/U9 goals, "
          f"stable owners and legacy forged fixtures ({args.publication})")


if __name__ == "__main__":
    main()

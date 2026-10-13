"""Contrasta plantillas fill_code completas con oráculos razonados y QA reutilizable.

No inventa main ni contexto para fragmentos: --contexts exige un mapa explícito
de fuentes publicadas y sustituciones revisadas. --execute compila únicamente las
variantes sin evidencia exacta; el resultado acredita escritorio, no Android.
"""

from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import uuid

if __package__:
    from . import verificar_predicciones_cpp as cpp
else:
    import verificar_predicciones_cpp as cpp


def inventory(oracles: Path):
    rows = cpp.load_json(oracles)["records"]
    expected = {(row["path"], row["page"], row["block"]): row for row in rows}
    if len(expected) != len(rows):
        raise ValueError("Oráculos duplicados")
    cases, fragments, snapshot, found = [], [], {}, set()
    course = cpp.CONTENT / "course_cpp_fundamentals.json"
    snapshot[course.relative_to(cpp.ROOT).as_posix()] = cpp.sha(course.read_bytes())
    lesson_count = fill_count = 0
    for unit_row in cpp.load_json(course)["units"]:
        unit = unit_row["id"].split("__")[-1]
        index = cpp.CONTENT / unit / f"unit_{unit}.json"
        snapshot[index.relative_to(cpp.ROOT).as_posix()] = cpp.sha(index.read_bytes())
        for lesson_row in cpp.load_json(index)["lessons"]:
            path = cpp.CONTENT / unit / ("lesson_" + lesson_row["id"].split("__")[-1] + ".json")
            lesson = cpp.load_json(path)
            if lesson["id"] != lesson_row["id"]:
                raise ValueError(f"ID distinto del índice: {path}")
            lesson_count += 1
            snapshot[path.relative_to(cpp.ROOT).as_posix()] = cpp.sha(path.read_bytes())
            for page, data in enumerate(lesson["pages"], 1):
                for block, item in enumerate(data["blocks"], 1):
                    if item["type"] != "fill_code":
                        continue
                    fill_count += 1
                    relative = path.relative_to(cpp.CONTENT).as_posix()
                    location = relative, page, block
                    origin = {"path": relative, "page": page, "block": block,
                              "unit": unit, "lesson": lesson["id"], "id": item.get("id"),
                              "type": "fill_code"}
                    template = item["code"]
                    if template.count("___") != 1 or "____" in template:
                        raise ValueError(f"Marcador no inequívoco: {location}")
                    if not re.search(r"\bmain\s*\(", template):
                        fragments.append({"origin": origin, "template_sha256": cpp.sha(template.encode()),
                                          "answers": item["answers"], "status": "requires_explicit_context"})
                        continue
                    oracle = expected.get(location)
                    if oracle is None or oracle["template_sha256"] != cpp.sha(template.encode()):
                        raise ValueError(f"Oráculo ausente o plantilla modificada: {location}")
                    found.add(location)
                    if oracle["exit"] != 0 or not isinstance(oracle["stdout"], str):
                        raise ValueError(f"Oráculo fuera del alcance de este lote: {location}")
                    if [row["answer"] for row in oracle["variants"]] != item["answers"]:
                        raise ValueError(f"Alternativas distintas del oráculo: {location}")
                    for variant in oracle["variants"]:
                        source = template.replace("___", variant["answer"])
                        if cpp.sha(source.encode()) != variant["source_sha256"]:
                            raise ValueError(f"Fuente completada distinta: {location}")
                        review, names = cpp.source_review(source, bool(oracle["stdin"]))
                        variant_origin = dict(origin, answer=variant["answer"])
                        cases.append(cpp.Case(variant_origin, source, cpp.sha(source.encode()),
                                              oracle["stdin"], [oracle["stdout"]], "exact",
                                              tuple(review), tuple(names)))
    if found != set(expected):
        raise ValueError(f"Oráculos fuera del índice: {sorted(set(expected) - found)}")
    return cases, fragments, {"indexed_lessons": lesson_count, "fill_code_blocks": fill_count,
                             "complete_blocks": len(found), "complete_variants": len(cases),
                             "snapshot_sha256": snapshot, "oracle_path": str(oracles),
                             "oracle_sha256": cpp.sha(oracles.read_bytes())}


def published_block(reference: dict) -> dict:
    path = (cpp.CONTENT / reference["path"]).resolve(strict=True)
    if not cpp.inside(path, cpp.CONTENT):
        raise ValueError("Referencia fuera del contenido curricular")
    lesson = cpp.load_json(path)
    block = lesson["pages"][reference["page"] - 1]["blocks"][reference["block"] - 1]
    if (lesson["id"] != reference["lesson"] or block.get("id") != reference["id"]
            or block["type"] != reference["type"] or block["code"] != reference["code"]
            or cpp.sha(block["code"].encode()) != reference["source_sha256"]):
        raise ValueError(f"Fuente publicada distinta del contexto: {reference['path']}")
    return block


def contextual_cases(contexts: Path, fragments: list[dict]):
    rows = cpp.load_json(contexts)["records"]
    pending = {(row["origin"]["path"], row["origin"]["page"], row["origin"]["block"]): row
               for row in fragments}
    found, cases, descriptions = set(), [], []
    for row in rows:
        reference = row["exercise"]
        location = reference["path"], reference["page"], reference["block"]
        if location not in pending or location in found:
            raise ValueError(f"Contexto extra o duplicado: {location}")
        found.add(location)
        block = published_block(reference)
        if [form["answer"] for form in row["forms"]] != block["answers"]:
            raise ValueError(f"Alternativas distintas del contexto: {location}")
        oracle = row["oracle"]
        if oracle["exit"] != 0 or not isinstance(oracle["stdout_lf"], str):
            raise ValueError(f"Oráculo fuera del alcance de este lote: {location}")
        for form in row["forms"]:
            fragment = block["code"].replace("___", form["answer"])
            if fragment != form["completed_fragment"] or cpp.sha(fragment.encode()) != form["source_sha256"]:
                raise ValueError(f"Fragmento completado distinto: {location}")
            dependencies = []
            if "published_project" in row:
                files = row["published_project"]["source_files"]
                for name, source_reference in files.items():
                    if Path(name).name != name or ":" in name or "\\" in name:
                        raise ValueError(f"Nombre de archivo no aislado: {name}")
                    published_block(source_reference)
                if fragment not in [file["code"] for name, file in files.items() if name.endswith(".h")]:
                    raise ValueError(f"Cabecera completada distinta del proyecto: {location}")
                source = files["source.cpp"]["code"]
                dependencies = [dict(name=name, code=file["code"], source_sha256=file["source_sha256"],
                                     path=file["path"], page=file["page"], block=file["block"])
                                for name, file in files.items() if name != "source.cpp"]
            else:
                source = published_block(row["published_source"])["code"]
                derived = [case for case in row["derived_cases"] if case["answer"] == form["answer"]]
                if len(derived) != 1:
                    raise ValueError(f"Caso derivado ausente o duplicado: {location}")
                derived = derived[0]
                for change in derived.get("transformations", [derived]):
                    if source.count(change["replace_exact"]) != 1:
                        raise ValueError(f"Sustitución no inequívoca: {location}")
                    source = source.replace(change["replace_exact"], change["with_exact"])
                if (source != derived["source"] or cpp.sha(source.encode()) != derived["source_sha256"]
                        or fragment not in source):
                    raise ValueError(f"Fuente derivada distinta del contexto: {location}")
            review, names = set(), set()
            known_headers = {file["name"] for file in dependencies}
            for program in [source, *[file["code"] for file in dependencies]]:
                issues, file_names = cpp.source_review(program, False)
                review.update(issues)
                names.update(file_names)
                for header in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', program, re.MULTILINE):
                    if header not in known_headers:
                        review.add("Cabecera local no mapeada explícitamente")
            origin = dict(pending[location]["origin"], answer=form["answer"],
                          type="fill_code_fragment", context_kind=row["kind"])
            cases.append(cpp.Case(origin, source, cpp.sha(source.encode()), "", [oracle["stdout_lf"]],
                                  "exact", tuple(sorted(review)), tuple(sorted(names)), tuple(dependencies)))
        descriptions.append({"origin": pending[location]["origin"], "kind": row["kind"],
                             "oracle": oracle, "preconditions": row.get("preconditions", []),
                             "status": "explicit_context_mapped"})
    if found != set(pending):
        raise ValueError(f"Fragmentos sin contexto: {sorted(set(pending) - found)}")
    return cases, descriptions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracles", required=True, type=Path)
    parser.add_argument("--contexts", type=Path, help="Mapa revisado de los fragmentos y su contexto publicado")
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--output", type=Path, help="Carpeta nueva dentro de la QA ignorada")
    parser.add_argument("--compiler", type=Path, default=Path("C:/msys64/mingw64/bin/g++.exe"))
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs debe ser positivo")
    if subprocess.check_output(["git", "branch", "--show-current"], cwd=cpp.ROOT, text=True).strip() != "main":
        parser.error("Este trabajo curricular se ejecuta exclusivamente en main")
    cases, fragments, metadata = inventory(args.oracles.resolve(strict=True))
    if args.contexts:
        contexts = args.contexts.resolve(strict=True)
        extra, fragments = contextual_cases(contexts, fragments)
        cases.extend(extra)
        metadata.update(contextual_variants=len(extra), contexts_path=str(contexts),
                        contexts_sha256=cpp.sha(contexts.read_bytes()))
    info = cpp.compiler_info(args.compiler)
    evidence = cpp.EvidenceIndex(info, [cpp.QA_ROOT, cpp.APP_EVIDENCE])
    plan = {"scope": "Indexed fill_code; fragments only with explicit reviewed context; host only",
            "compiler": info, "flags": cpp.FLAGS, "inventory": metadata,
            "reuse": evidence.summary(cases), "fragments": fragments,
            "omitted": [{"origin": case.origin, "reasons": list(case.review)} for case in cases if case.review],
            "host_only": True, "android_certified": False}
    print(json.dumps({"inventory": {key: value for key, value in metadata.items() if key != "snapshot_sha256"},
                      "reuse": plan["reuse"], "fragments": fragments, "omitted": plan["omitted"]},
                     ensure_ascii=False, indent=2), flush=True)
    if not args.execute:
        return 0
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = args.output.resolve() if args.output else cpp.QA_ROOT / ("rellenos-" + stamp + "-" + uuid.uuid4().hex[:8])
    if not cpp.inside(output, cpp.QA_ROOT) or output.exists():
        parser.error("output debe ser una carpeta nueva dentro de la QA ignorada")
    output.mkdir(parents=True, exist_ok=False)
    cpp.atomic_json(output / "plan.json", plan)
    runner = cpp.Runner(info, evidence, output, 60, 10)
    records = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(runner.check, case): case for case in cases}
        for future in as_completed(futures):
            case = futures[future]
            try:
                record = future.result()
            except Exception as error:
                record = {"origin": case.origin, "status": "verifier_error", "error": repr(error)}
            records.append(record)
            counts = dict(Counter(row["status"] for row in records))
            cpp.atomic_json(output / "resultados.json", {**plan, "completed": len(records),
                                                        "statuses": counts, "records": records})
            if len(records) % 20 == 0 or len(records) == len(cases):
                print(f"{len(records)}/{len(cases)}: {counts}; {output}", flush=True)
    return 0 if all(row["status"] == "matched" for row in records) else 1


if __name__ == "__main__":
    raise SystemExit(main())

"""Exact authored semantics across retirement; isolated Python and Win32 fixtures.

The real U7L3 publication is independently byte-pinned. Synthetic publications
use the normal authoring and transition APIs. No curricular asset or learner
profile is written. The manifest identifies each complete historical source.
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
import source_format_retirement_fixture_source as format_fixtures


ROOT = Path(__file__).resolve().parents[2]
COMMIT = "52a871c4ca18875837d22453374abbf938b4d2b4"
RELATIVE = format_fixtures.SOURCES["u7"][1]
SOURCE_SHA256 = format_fixtures.SOURCES["u7"][2]
REAL_OWNERS = ("block_0000_0001", "block_0001_0001")
OUTPUT, ORDER, COMPILER = "block_0000_0001", "block_0000_0002", "block_0000_0004"
NOTE_A, NOTE_B, NOTE_C, MATH = (f"block_0000_{index:04d}" for index in (5, 6, 7, 8))
CHOICE = "block_0000_0009"


def verify_bytes(payload: bytes) -> bytes:
    if hashlib.sha256(payload).hexdigest() != SOURCE_SHA256:
        raise ValueError("Semantic retirement fixture differs from its pinned publication")
    return payload


@lru_cache(maxsize=1)
def historical_bytes() -> bytes:
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    return verify_bytes(subprocess.check_output(
        ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}",
         "show", f"{COMMIT}:{RELATIVE}"],
        cwd=ROOT, env=environment, stderr=subprocess.PIPE))


def published() -> dict:
    result = json.loads(historical_bytes())
    layout.validate(result)
    owners = layout._blocks(result)
    marked = {field["block"] for field in result["textSemantics"]["fields"]}
    if (result["id"] != format_fixtures.U7_ID
            or any(owners[owner]["type"] != "paragraph" or owner not in marked
                   for owner in REAL_OWNERS)
            or len(result["stateLayout"].get("sourceFormatRevisions", [])) != 6):
        raise ValueError("Pinned U7L3 owners, markings or format archives changed")
    return result


def mark(document: dict, identity: str, path: str, fragments: list[tuple] = ()) -> dict:
    """Author exact spans, replacing one field without disturbing its neighbors."""
    source = layout.text_semantics.public_text_fields(layout._blocks(document)[identity])[path]
    spans = sorted((layout.text_semantics.select(source, text, role, reading=reading)
                    for text, role, reading in fragments), key=lambda span: span["start"])
    descriptor = layout.text_semantics.make_field(document, identity, path, spans)
    fields = document.setdefault("textSemantics", {"version": 1, "fields": []})["fields"]
    matches = [index for index, field in enumerate(fields)
               if (field["block"], field["path"]) == (identity, path)]
    if matches:
        fields[matches[0]] = descriptor
    else:
        fields.append(descriptor)
    return descriptor


def annotate(document: dict) -> dict:
    """Explicit QA authorship, including feedback and a supplementary scalar."""
    result = deepcopy(document)
    active = layout._blocks(result)
    for owner in (NOTE_A, NOTE_B, NOTE_C):
        if owner in active:
            mark(result, owner, "/text", [("🌌", "data", "characters"), ("N=7", "output", "natural")])
    mark(result, OUTPUT, "/question", [("N=2", "output", "natural")])
    mark(result, OUTPUT, "/explanation", [("resultado", "output", "natural"),
         ("(Autor, 2024, p. 9)", "citation", "natural")])
    mark(result, OUTPUT, "/reviewFeedback", [("N=3", "output", "natural")])
    mark(result, ORDER, "/question", [])
    if COMPILER in active:
        cases = active[COMPILER]["outputCheck"]["cases"]
        if len(cases) == 1:
            mark(result, COMPILER, "/outputCheck/cases/0/explanation", [("vacía", "output", "natural")])
            mark(result, COMPILER, "/outputCheck/cases/0/reviewFeedback/0/text", [("N=2", "output", "natural")])
        else:
            for index in range(len(cases)):
                role, reading = ("output", "natural") if index == 0 else ("data", "characters")
                mark(result, COMPILER, f"/outputCheck/cases/{index}/explanation", [("resultado", role, reading)])
                mark(result, COMPILER, f"/outputCheck/cases/{index}/reviewFeedback/0/text", [("resultado", role, reading)])
    mark(result, MATH, "/prompt", [("N=7", "data", "characters")])
    mark(result, MATH, "/feedback/correct", [("N=7", "output", "natural")])
    mark(result, MATH, "/feedback/incorrect", [("N=7", "output", "natural")])
    mark(result, CHOICE, "/options/0", [("N=7", "output", "natural")])
    mark(result, CHOICE, "/explanation", [("N=7", "output", "natural")])
    title = result["title"]
    result["textSemantics"]["rootFields"] = [layout.text_semantics.make_root_field(
        result, "/title", [layout.text_semantics.select(title, "🌌", "data", reading="characters")])]
    layout.validate_transition(document, result)
    return result


def synthetic(*, marked: bool = True, multicase: bool = False) -> dict:
    source = format_fixtures.fixture()
    source["id"] = "cpp_fundamentals__semantic_retirement_qa__semantic_retirement_qa"
    source["title"] = "Resultados 🌌 y lectura"
    blocks = source["pages"][0]["blocks"]
    blocks[1].update(question="¿Qué salida N=2 produce el programa?",
        explanation="El resultado coincide con el valor (Autor, 2024, p. 9).",
        reviewFeedback="Comprueba N=3.")
    blocks[4]["outputCheck"]["cases"][0].update(
        explanation="La salida vacía corresponde al programa.",
        reviewFeedback=[{"outputs": ["N=2"], "text": "Revisa la salida N=2."}])
    if multicase:
        code = ("#include <iostream>\nint main() {\n    int value = 0;\n"
                "    if (!(std::cin >> value)) return 1;\n    std::cout << value;\n}\n")
        blocks[4]["initialCode"] = code
        blocks[4]["projectFiles"][1]["initialCode"] = code
        blocks[4]["outputCheck"]["cases"] = [{
            "standardInput": f"{value}\n", "outputs": [str(value)],
            "explanation": "El resultado está listo.",
            "reviewFeedback": [{"outputs": ["incorrecto"], "text": "Revisa el resultado."}],
        } for value in (2, 3)]
    blocks.extend([
        {"type": "paragraph", "text": "🌌 La salida N=7 conserva int."},
        {"type": "paragraph", "text": "🌌 La salida N=7 conserva int."},
        {"type": "note", "text": "🌌 Otro resultado N=7."},
        {"type": "math_puzzle", "schemaVersion": 1, "id": "semantic_math", "prompt": "Relaciona N=7.",
         "responses": [{"id": "pairs", "type": "math_matching_drag",
             "sources": [{"id": "a", "label": "N=7"}],
             "targets": [{"id": "b", "label": "N=7"}],
             "validator": {"type": "matching", "expected": {"a": "b"}}}],
         "feedback": {"correct": "Correcto: N=7.", "incorrect": "Comprueba N=7."}},
        {"type": "multiple_choice", "id": "semantic_choice", "question": "Elige la salida.",
         "options": ["N=7", "N=8"], "correctIndex": 0, "explanation": "El resultado es N=7."},
    ])
    result = layout.prepare_v2(source, "es", {"es": source})
    return annotate(result) if marked else result


def semantic_revision(document: dict) -> dict:
    result = deepcopy(document)
    mark(result, NOTE_B, "/text", [("🌌", "prose", "natural"), ("N=7", "data", "characters")])
    mark(result, OUTPUT, "/question", [("N=2", "data", "characters")])
    result["textSemantics"]["rootFields"][0]["spans"][0].update(role="prose", reading="natural")
    layout.validate_transition(document, result)
    return result


def wording_revision(document: dict) -> dict:
    text = "Predice la salida N=2 antes de ejecutar."
    result = layout.revise_ordinary_editorial(document, OUTPUT, {"question": {
        "text": text, "spans": [layout.text_semantics.select(text, "N=2", "output", reading="natural")]}})
    prompt = "Relaciona el dato N=7 con su rótulo."
    return layout.revise_math_prompt_editorial(result, MATH, prompt,
        spans=[layout.text_semantics.select(prompt, "N=7", "data", reading="characters")])


def bibliography_revision(document: dict) -> dict:
    result = deepcopy(document)
    layout._blocks(result)[OUTPUT]["explanation"] = "El resultado coincide con el valor (Autor, 2024)."
    mark(result, OUTPUT, "/explanation", [("resultado", "output", "natural"), ("(Autor, 2024)", "citation", "natural")])
    return layout.prepare_v2(result, "es", {"es": document}, document)


def formatted(document: dict) -> dict:
    return layout.prepare_v2(format_fixtures.edited(document), "es", {"es": document}, document)


def history_view(document: dict, index: int = 0) -> dict:
    """Public text validation view; absence/null never borrows current marks."""
    snapshot = document["stateLayout"]["retirementHistory"][index]
    result = {"title": document["title"], "pages": deepcopy(snapshot["pages"]),
              "stateLayout": {"version": 2, "current": deepcopy(snapshot["current"])}}
    if snapshot.get("textSemantics") is not None:
        result["textSemantics"] = deepcopy(snapshot["textSemantics"])
    return result


def negative_documents(document: dict) -> dict[str, tuple[dict, str | None]]:
    result = {}
    def make(name, mutate, message=None):
        bad = deepcopy(document)
        mutate(bad["stateLayout"]["retirementHistory"][0])
        result[name] = bad, message
    def field(snapshot, owner=NOTE_A, path="/text"):
        return next(item for item in snapshot["textSemantics"]["fields"]
                    if (item["block"], item["path"]) == (owner, path))
    make("overlay-array", lambda item: item.update(textSemantics=[]),
         "Historical text semantics must be an object or null")
    make("overlay-version", lambda item: item["textSemantics"].update(version=True))
    make("overlay-floating-version", lambda item: item["textSemantics"].update(version=1.0))
    make("overlay-extra-key", lambda item: item["textSemantics"].update(inferred=True))
    make("unknown-owner", lambda item: field(item).update(block="unknown"))
    make("duplicate-field", lambda item: item["textSemantics"]["fields"].append(deepcopy(field(item))))
    make("stale-source", lambda item: field(item).update(sourceSha256="0" * 64),
         "Text semantic source fingerprint is stale")
    make("hidden-answer", lambda item: field(item, OUTPUT, "/question").update(path="/answers/0"))
    make("hidden-compiler-oracle", lambda item: field(item, COMPILER,
         "/outputCheck/cases/0/explanation").update(path="/outputCheck/cases/0/outputs/0"))
    make("hidden-math-validator", lambda item: field(item, MATH, "/prompt").update(
         path="/responses/0/validator/expected/a"))
    make("noncanonical-path", lambda item: field(item).update(path="/text/"))
    make("split-scalar", lambda item: field(item)["spans"][0].update(end=1))
    make("floating-span-start", lambda item: field(item)["spans"][0].update(
         start=float(field(item)["spans"][0]["start"])))
    make("changed-selection", lambda item: field(item)["spans"][0].update(text="x"))
    make("unknown-role", lambda item: field(item)["spans"][0].update(role="keyword"))
    make("unknown-reading", lambda item: field(item)["spans"][0].update(reading="phonetic"))
    make("partial-feedback", lambda item: item["textSemantics"].update(fields=[
         entry for entry in item["textSemantics"]["fields"]
         if (entry["block"], entry["path"]) != (MATH, "/feedback/incorrect")]))
    make("root-stale-source", lambda item: item["textSemantics"]["rootFields"][0].update(sourceSha256="0" * 64),
         "Text semantic source fingerprint is stale")
    make("root-duplicate-field", lambda item: item["textSemantics"]["rootFields"].append(
         deepcopy(item["textSemantics"]["rootFields"][0])))
    make("root-hidden-path", lambda item: item["textSemantics"]["rootFields"][0].update(path="/id"))
    make("root-split-scalar", lambda item: item["textSemantics"]["rootFields"][0]["spans"][0].update(
         end=item["textSemantics"]["rootFields"][0]["spans"][0]["end"] - 1))
    def different_archived_source(snapshot):
        source = next(block for page, record in zip(snapshot["pages"], snapshot["current"])
                      for block, owner in zip(page["blocks"], record["blocks"]) if owner == NOTE_A)
        source["emphasis"] = [{"text": "N=7", "style": "bold"}]
    make("different-archived-block", different_archived_source,
         "A semantic retirement batch must preserve its exact archived block")
    wrong_current = deepcopy(document)
    wrong_current["textSemantics"]["fields"].append(deepcopy(
        field(document["stateLayout"]["retirementHistory"][0])))
    result["retired-owner-in-current"] = wrong_current, None
    return result


def build_fixtures() -> tuple[dict[str, dict], dict]:
    documents, manifest = {}, {"cases": [], "invalid": []}
    def put(name, document):
        filename = name + ".json"
        if filename in documents and documents[filename] != document:
            raise ValueError("A fixture name cannot replace another publication")
        documents[filename] = deepcopy(document)
        return filename
    def case(name, before, after, history):
        layout.validate_transition(before, after)
        manifest["cases"].append({"name": name, "before": put(name + "-before", before),
            "after": put(name + "-after", after),
            "retired": [item["id"] for item in after["stateLayout"].get("retired", [])],
            "history": [{"index": index, "source": put(name + f"-history-{index}", source)}
                        for index, source in enumerate(history)]})
    real = published()
    first = layout.retire(real, [REAL_OWNERS[0]])
    case("u7-marked-batch", real, layout.retire(real, list(REAL_OWNERS)), [real])
    case("u7-marked-first", real, first, [real])
    second = layout.retire(first, [REAL_OWNERS[1]])
    case("u7-marked-second", first, second, [real, first])

    base = synthetic()
    batch = layout.retire(base, [NOTE_A, NOTE_B])
    case("semantic-batch", base, batch, [base])
    first = layout.retire(base, [NOTE_A])
    repainted = semantic_revision(first)
    second = layout.retire(repainted, [NOTE_B])
    revised = wording_revision(second)
    third = layout.retire(revised, [NOTE_C])
    # Each step is its own real transition. The earlier source files are
    # complete publications; neither a later descriptor nor a patched SHA is
    # substituted into an earlier snapshot.
    case("semantic-repainted", repainted, second, [base, repainted])
    case("semantic-reworded", revised, third, [base, repainted, revised])

    biblio = bibliography_revision(first)
    after_biblio = layout.retire(biblio, [NOTE_B])
    case("semantic-bibliography", biblio, after_biblio, [base, biblio])

    ordinary_text = "Predice la salida N=2 antes de ejecutar."
    revised = layout.revise_ordinary_editorial(first, OUTPUT, {"question": {
        "text": ordinary_text, "spans": [layout.text_semantics.select(ordinary_text, "N=2", "output")]}})
    second = layout.retire(revised, [NOTE_B])
    old_question = layout._blocks(base)[OUTPUT]["question"]
    returned = layout.revise_ordinary_editorial(second, OUTPUT, {
        "question": {"text": old_question, "spans": [layout.text_semantics.select(old_question, "N=2", "data")]},
        "returnTo": 0})
    case("semantic-editorial-return", returned, layout.retire(returned, [NOTE_C]), [base, revised, returned])

    plain = synthetic(marked=False)
    unmarked = layout.retire(plain, [NOTE_A])
    marked = annotate(unmarked)
    case("semantic-null-before-marking", marked, layout.retire(marked, [NOTE_B]), [plain, marked])
    legacy = deepcopy(unmarked)
    del legacy["stateLayout"]["retirementHistory"][0]["textSemantics"]
    layout.validate_transition(plain, legacy)
    marked_legacy = annotate(legacy)
    case("semantic-legacy-before-marking", marked_legacy,
         layout.retire(marked_legacy, [NOTE_B]), [plain, marked_legacy])

    empty = deepcopy(plain)
    empty["textSemantics"] = {"version": 1, "fields": [], "rootFields": []}
    layout.validate_transition(plain, empty)
    case("semantic-empty-overlay", empty, layout.retire(empty, [NOTE_A]), [empty])
    root_only = deepcopy(empty)
    root_only["textSemantics"]["rootFields"] = deepcopy(base["textSemantics"]["rootFields"])
    layout.validate_transition(empty, root_only)
    case("semantic-root-only", root_only, layout.retire(root_only, [NOTE_A]), [root_only])

    after_format = formatted(base)
    case("semantic-format-before-retirement", after_format,
         layout.retire(after_format, [NOTE_A]), [after_format])
    case("semantic-retirement-before-format", first, formatted(first), [base])
    case("semantic-compiler-retirement", base, layout.retire(base, [COMPILER]), [base])
    multicase = synthetic(multicase=True)
    case("semantic-multicase-compiler", multicase, layout.retire(multicase, [NOTE_A]), [multicase])
    manifest["cases"][-1]["compilerCases"] = [{"id": COMPILER, "case": index} for index in (0, 1)]

    combined_choice = layout.revise_choice_editorial(first, CHOICE, 0, "N=7 conserva el valor.")
    case("semantic-retirement-and-choice", base, combined_choice, [base])
    revised_choice = layout.revise_choice_editorial(base, CHOICE, 0, "N=7 conserva el valor.")
    case("semantic-choice-before-retirement", revised_choice,
         layout.retire(revised_choice, [NOTE_A]), [revised_choice])
    manifest["cases"][-1]["stateSource"] = put("semantic-choice-original-state", base)

    # The general option contract deliberately protects every other field.
    # A subsequent explanation rewrite is a rejection, not an expanded waiver.
    invalid_choice = deepcopy(documents[manifest["cases"][-1]["after"]])
    original_choice = deepcopy(layout._blocks(invalid_choice)[CHOICE])
    layout._blocks(invalid_choice)[CHOICE]["explanation"] = "El resultado sigue siendo N=7 después de comparar."
    mark(invalid_choice, CHOICE, "/explanation", [("N=7", "output", "natural")])
    invalid_choice["stateLayout"]["editorial"].append({"id": CHOICE,
        "beforeSha256": layout.fingerprint(original_choice),
        "afterSha256": layout.fingerprint(layout._blocks(invalid_choice)[CHOICE])})

    for name, (bad, message) in negative_documents(batch).items():
        item = {"name": name, "file": put("invalid-" + name, bad)}
        if message:
            item["message"] = message
        manifest["invalid"].append(item)
    manifest["invalid"].append({"name": "choice-explanation-rewrite",
        "file": put("invalid-choice-explanation-rewrite", invalid_choice)})
    return documents, manifest


def export_fixtures(output_dir: Path) -> Path:
    documents, manifest = build_fixtures()
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, document in documents.items():
        (output_dir / name).write_text(json.dumps(document, ensure_ascii=False, indent=2,
            allow_nan=False) + "\n", encoding="utf-8", newline="\n")
    destination = output_dir / "manifest.json"
    destination.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8", newline="\n")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    print(export_fixtures(args.output_dir))

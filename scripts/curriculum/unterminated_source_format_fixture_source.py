"""Authenticated U1L4 example: format only before its incomplete literal.

The existing source-format archive keeps the whole published block. The
ordinary literal and every following byte remain exact; no diagnostic goal or
product JSON is authored here. These copies are isolated QA evidence only.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

import code_format_compat as source_format
from diagnostic_fixture_source import checked_output, existing_matches
import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
PUBLISHED_COMMIT = "55e60dcd4c9da2ddf71aad6eddd1c5937e312337"
RELATIVE_PATH = ("programming/cpp_fundamentals/programas_y_ejecucion/"
                 "lesson_fuente_compilacion_y_diagnostico.json")
OWNER = "p1u1l4_codigo_sin_comilla"
PUBLICATIONS = {
    "es": ("e6281a98576dfe78a3072965cb498875c1ceb953269fc4c0c7e5ee396bf2b610",
           "730d544de582f902a2e5800eaaeb888eefeac2f4"),
    "en": ("a43ec64f51fec893866d40021adc507a2b19d7a5d60cdaeef6106a154330e6f6",
           "295f7c0ba99613395d252f7937ecce8012f64d0d"),
    "fr": ("488034bafea6aadccf52968f22d81fe0aced723012c43143f81a38e9b9dff2c0",
           "8f02ef555262744255f9ffc4ee63dfdab776689d"),
}
SOURCE_SHA256 = "48bcc0f8d5b3f5c06cd5bad73030efa2beecba100a653a7da4553b2c0573bf75"
FORMATTED_SHA256 = "af6544e511a22b6653c555a80dbe4ba0b63a7f1067c594cac11bb5d53d5b5248"
BLOCK_SHA256 = "0ab6970f945685277fa4e81dc8f7ea67df1b037691cf96a9b93dbe8b1c2062cd"
NEGATIVE_NAMES = ("tail_space", "tail_newline", "repaired_quote", "prefix_token",
                  "line_splice", "removed_archive", "altered_archive", "borrowed_owner")


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def json_bytes(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def verify_published_bytes(locale: str, payload: bytes) -> bytes:
    if locale not in PUBLICATIONS:
        raise ValueError("Unknown incomplete-literal publication locale")
    digest, blob = PUBLICATIONS[locale]
    actual_blob = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload).hexdigest()
    if sha256(payload) != digest or actual_blob != blob:
        raise ValueError("Incomplete-literal QA differs from its complete published Git bytes")
    return payload


def published_bytes(locale: str = "es") -> bytes:
    if locale not in PUBLICATIONS:
        raise ValueError("Unknown incomplete-literal publication locale")
    reference = PUBLISHED_COMMIT + ":app/src/main/assets/content/" + locale + "/" + RELATIVE_PATH
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    try:
        payload = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", reference],
            cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError("Incomplete-literal QA requires its pinned local Git source; "
                           "no fetch or current-asset fallback is permitted") from error
    return verify_published_bytes(locale, payload)


def formatted_source(source: str) -> str:
    if sha256(source.encode("utf-8")) != SOURCE_SHA256:
        raise ValueError("Incomplete-literal QA lost its exact original source")
    revised = source.replace("\nint main() {", "\n\nint main() {", 1)
    if sha256(revised.encode("utf-8")) != FORMATTED_SHA256:
        raise ValueError("Incomplete-literal QA changed its exact formatting edit")
    return revised


def verify_format_only(before: dict, after: dict) -> None:
    original, revised = state._blocks(before)[OWNER], state._blocks(after)[OWNER]
    expected = {**original, "code": formatted_source(original["code"])}
    if revised != expected or not source_format.require_source_format(original, revised):
        raise ValueError("Incomplete-literal QA changed more than its one source-format edit")
    previous = before["stateLayout"]
    archives = previous.get("sourceFormatRevisions", [])
    if any(item["id"] == OWNER for item in archives):
        raise ValueError("Incomplete-literal baseline unexpectedly already archives this owner")
    edge = {"id": OWNER, "beforeSha256": state.fingerprint(original),
            "afterSha256": state.fingerprint(revised)}
    current = after["stateLayout"]
    if current.get("sourceFormatRevisions") != [*archives, {"id": OWNER, "block": original}]:
        raise ValueError("Incomplete-literal QA changed the archive or its owner")
    if current["editorial"] != [*previous["editorial"], edge]:
        raise ValueError("Incomplete-literal QA changed its original editorial prefix")
    restored = deepcopy(after)
    state._blocks(restored)[OWNER]["code"] = original["code"]
    restored["stateLayout"]["editorial"] = deepcopy(previous["editorial"])
    if "sourceFormatRevisions" in previous:
        restored["stateLayout"]["sourceFormatRevisions"] = deepcopy(archives)
    else:
        restored["stateLayout"].pop("sourceFormatRevisions")
    if restored != before or state.validate(before) != state.validate(after):
        raise ValueError("Incomplete-literal QA changed curriculum, history or storage addresses")
    state.validate_transition(before, after)


def invalid_documents(after: dict) -> dict[str, dict]:
    cases = {}
    for name in NEGATIVE_NAMES:
        invalid = deepcopy(after)
        block = state._blocks(invalid)[OWNER]
        if name == "tail_space":
            block["code"] = block["code"].replace('"Listo;', '"Listo ;', 1)
        elif name == "tail_newline":
            block["code"] = block["code"].replace("\n    return", "\n\n    return", 1)
        elif name == "repaired_quote":
            block["code"] = block["code"].replace('"Listo;', '"Listo";', 1)
        elif name == "prefix_token":
            block["code"] = block["code"].replace("std::cout", "std::cerr", 1)
        elif name == "line_splice":
            block["code"] = block["code"].replace("std::cout", "std::co\\\nut", 1)
        elif name == "removed_archive":
            invalid["stateLayout"]["sourceFormatRevisions"].pop()
        elif name == "altered_archive":
            invalid["stateLayout"]["sourceFormatRevisions"][-1]["block"]["code"] += " "
        elif name == "borrowed_owner":
            invalid["stateLayout"]["sourceFormatRevisions"][-1]["id"] = "p1u1l4_codigo_sin_cierre"
        # Re-seal the public endpoint: rejection must examine the source and
        # original archive, rather than merely find an outdated endpoint hash.
        invalid["stateLayout"]["editorial"][-1]["afterSha256"] = state.fingerprint(block)
        cases[name] = invalid
    return cases


def export_payloads() -> dict[str, bytes]:
    original_bytes = published_bytes()
    before = state.parse_json(original_bytes.decode("utf-8"))
    family = {"es": before, **{
        locale: state.parse_json(published_bytes(locale).decode("utf-8"))
        for locale in ("en", "fr")}}
    state.validate(before)
    original = state._blocks(before)[OWNER]
    if (original.get("type") != "code" or original.get("language") != "cpp"
            or state.fingerprint(original) != BLOCK_SHA256):
        raise ValueError("Incomplete-literal QA changed its authentic example owner")
    modified = deepcopy(before)
    state._blocks(modified)[OWNER]["code"] = formatted_source(original["code"])
    after = state.prepare_v2(modified, "es", family, before)
    verify_format_only(before, after)
    payloads = {"before.json": original_bytes, "after.json": json_bytes(after)}
    negatives = []
    for name, document in invalid_documents(after).items():
        path = f"negative/{name}.json"
        payloads[path] = json_bytes(document)
        negatives.append({"name": name, "path": path, "sha256": sha256(payloads[path])})
    payloads["manifest.json"] = json_bytes({
        "version": 1, "kind": "unterminated-source-format", "publishedCommit": PUBLISHED_COMMIT,
        "owner": OWNER, "lessonId": before["id"], "publishedPath": RELATIVE_PATH,
        "before": "before.json", "after": "after.json",
        "beforeSha256": sha256(original_bytes), "afterSha256": sha256(payloads["after.json"]),
        "sourceSha256": SOURCE_SHA256, "formattedSourceSha256": FORMATTED_SHA256,
        "negative": negatives,
    })
    return payloads


def export(output_dir: Path) -> Path:
    output = checked_output(output_dir)
    payloads = export_payloads()
    for name, payload in payloads.items():
        existing_matches(output / name, payload)
    for name, payload in payloads.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        checked_output(target.parent)
        if not existing_matches(target, payload):
            with target.open("xb") as stream:
                stream.write(payload)
    return output / "manifest.json"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    print(export(parser.parse_args().output_dir))

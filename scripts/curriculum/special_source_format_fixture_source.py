"""Authenticate three complete U12 publications for special-source format QA.

Each isolated AFTER copy adds one blank line before its owner's main(), one
sourceFormatRevisions archive and the ordinary authenticated editorial edge.
The full published sources, output contracts, other archives and all history
remain intact. No authored lesson or personal profile is an export target.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import code_format_compat as source_format
from diagnostic_fixture_source import checked_output, existing_matches
import stable_state_layout as state


ROOT = Path(__file__).resolve().parents[2]
# These three Git blobs are identical in main@08a11877 and Codex@f3da5e1e.
PUBLISHED_COMMIT = "08a11877a2d2c0bf96a846660e3baea69e53f758"
CONTENT = "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/"
PUBLICATIONS = {
    "crt": {
        "path": CONTENT + "lesson_clasificar_caracteres_y_normalizar_una_copia.json",
        "sha256": "c81c2bdc6faf28736d5208183ca8ca224c54813fc74b3e79127251aa93d1e44f",
        "blobSha": "46d01275dbff424ce326e62385e07d5ac034b374",
        "owner": "block_0005_0004", "kind": "crt-output",
        "blockSha256": "e979f8e501bf3aca08c01d1d89b85ad6e4ec14f8427f567e776d582b9d1bd4da",
        "sourceSha256": "5b5d9a36e9c9b94ae1719daa09ded0942108b6942c82aa0ccc4580f1da2d8584",
        "formattedSourceSha256": "ca4a330de7d236469a38fb44b209a91c6f97b1ab776aff195a9305d8f59c14e4",
        "executionOwner": "block_0008_0001", "executionPath": "/code",
        "executionBlockSha256": "c4e82f51295ea7d4bf9a3521e106ad1daa16051c1d8f7a8177857a4db3c22c6f",
        "executionSourceSha256": "fe09c9d9b33431c7f66f2f8be5ea6839c4c810cd6a1ea65c9efba67e6401d1d6",
    },
    "exit_stream": {
        "path": CONTENT + "lesson_compartir_flujos_sin_acoplar_el_calculo.json",
        "sha256": "793b441f408f7645fc2b22024cfb8d10bfe28c5d1afc07312ea52969532a206d",
        "blobSha": "a33359fbe803ab156e9c011dcd7a7064b0cd1520",
        "owner": "block_0005_0002", "kind": "intentional-exit",
        "blockSha256": "409e9eef8d8c00485a4893b17478d8db9ecf90dedcee137e16ce077f470d0c1d",
        "sourceSha256": "b8c398d5b6e0bd49a62eb13e3fa1a28733c4ca3541ebdd6ae5187c9b0b765d58",
        "formattedSourceSha256": "96cd7102c71d677f70372528dd11c1edd1a613c42a415f8f98a1a61f006f59a1",
        "executionOwner": "block_0005_0002", "executionPath": "/initialCode",
        "executionBlockSha256": "409e9eef8d8c00485a4893b17478d8db9ecf90dedcee137e16ce077f470d0c1d",
        "executionSourceSha256": "b8c398d5b6e0bd49a62eb13e3fa1a28733c4ca3541ebdd6ae5187c9b0b765d58",
    },
    "exit_guard": {
        "path": CONTENT + "lesson_construir_un_programa_de_carga_y_guardado.json",
        "sha256": "73eb2577ac3ed1622aeee92ed54204994927309a7c3c8d4cb0b3daaa4e83f18f",
        "blobSha": "fdd4a0d281ef7b5fa9349bee6f9dc3ac49be4db6",
        "owner": "block_0004_0017", "kind": "intentional-exit",
        "blockSha256": "3cba5c957ee8065d09441329d88e0b2f2ab83a72f845269d1b5b6e564102f79f",
        "sourceSha256": "55e1e30b1c83b9c85cc9de03d6dedec3c310d6b1f2ea098547807aedb348b504",
        "formattedSourceSha256": "fdb6f6cbf4867436537644eedd340fa691d9ec777d7b617ad93c718bca05e347",
        "executionOwner": "block_0004_0017", "executionPath": "/initialCode",
        "executionBlockSha256": "3cba5c957ee8065d09441329d88e0b2f2ab83a72f845269d1b5b6e564102f79f",
        "executionSourceSha256": "55e1e30b1c83b9c85cc9de03d6dedec3c310d6b1f2ea098547807aedb348b504",
    },
}

# Read-only siblings authenticate the complete existing language family for
# prepare_v2. They are neither edited nor exported as new lesson fixtures.
TRANSLATIONS = {
    "crt": {
        "en": {"sha256": "c6c5d472b23bb77d4e21e424de5b548208185b5668da995c9ac3f0a8a8ba96e8",
               "blobSha": "d5e4053105ce5bd5ee6898e8ff59e1e79f0df286"},
        "fr": {"sha256": "7577d8c18f96ebd98759a0b85fe566241fb1da93103f4fd4a2547350f929c258",
               "blobSha": "a1d8c153d7b640f3fa674c721a8a5b5923f00b33"},
    },
    "exit_stream": {
        "en": {"sha256": "48371d8596fa7bde34be7572a5e56a0b7206bb8947ddcfcebc0949eba1476f4c",
               "blobSha": "7454ec3b563b1029fcaf916b238bdfd33d6fb052"},
        "fr": {"sha256": "39cedacd43e7f7ddeb1d6139ebe359d6974521c0adcdeac2ba6a5ce6d14bcd25",
               "blobSha": "3ca3abb046586794380fa5c820b81ebfcd2c52dd"},
    },
    "exit_guard": {
        "en": {"sha256": "97df36676734aa4adf4cfec8573d36ffe1026717988813367df405aee3d87a23",
               "blobSha": "067bd2b4c4bfc283645a3f8c31da86da14922926"},
        "fr": {"sha256": "70cbaaee10733a7740e9b82363d2628d4ab016ef9846925adc09427313b368fd",
               "blobSha": "343ba33b715a4b5a866ca5ebb58cfeeb0fe1f17f"},
    },
}


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def json_bytes(document: dict) -> bytes:
    return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def publication_path(name: str, locale: str = "es") -> str:
    if name not in PUBLICATIONS or locale not in {"es", "en", "fr"}:
        raise ValueError("Unknown special-source-format publication")
    return PUBLICATIONS[name]["path"].replace("/content/es/", f"/content/{locale}/", 1)


def verify_published_bytes(name: str, payload: bytes, locale: str = "es") -> bytes:
    publication_path(name, locale)
    pin = PUBLICATIONS[name] if locale == "es" else TRANSLATIONS[name][locale]
    blob = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii") + b"\0" + payload).hexdigest()
    if sha256(payload) != pin["sha256"] or blob != pin["blobSha"]:
        raise ValueError(f"Special-source-format QA differs from its complete authenticated publication: {name}")
    return payload


def published_bytes(name: str, locale: str = "es") -> bytes:
    reference = PUBLISHED_COMMIT + ":" + publication_path(name, locale)
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    try:
        payload = subprocess.check_output(
            ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", reference],
            cwd=ROOT, env=environment, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError("Special-source-format QA requires its pinned local Git source; "
                           "no fetch or current-asset fallback is permitted") from error
    return verify_published_bytes(name, payload, locale)


def formatted_source(source: str) -> str:
    marker = "\nint main() {"
    if source.count(marker) != 1:
        raise ValueError("Special-source-format QA lost its unique published main declaration")
    return source.replace(marker, "\n\nint main() {", 1)


def execution_source(name: str, document: dict) -> dict:
    pin = PUBLICATIONS[name]
    identity, path = pin["executionOwner"], pin["executionPath"]
    block = state._blocks(document)[identity]
    expected_type = "code" if path == "/code" else "cpp_compiler"
    if (block.get("type") != expected_type or state.fingerprint(block) != pin["executionBlockSha256"]
            or (expected_type == "code" and block.get("language") != "cpp")):
        raise ValueError(f"Special-source-format QA changed an authentic execution owner: {name}")
    source = block[path[1:]]
    if sha256(source.encode("utf-8")) != pin["executionSourceSha256"]:
        raise ValueError(f"Special-source-format QA changed an authentic execution source: {name}")
    if identity != pin["owner"]:
        relation = next((entry for entry in document.get("referenceAfter", [])
                         if entry["activity"] == pin["owner"]), None)
        if relation is None or identity not in relation["blocks"]:
            raise ValueError("Special-source-format QA lost its authored execution reference relation")
    return {"sourceOwner": identity, "sourcePath": path,
            "source": source, "sourceSha256": sha256(source.encode("utf-8"))}


def verify_format_only(name: str, before: dict, after: dict) -> None:
    """The new archive and edge authenticate only one source whitespace edit."""
    identity = PUBLICATIONS[name]["owner"]
    original, current = state._blocks(before)[identity], state._blocks(after)[identity]
    expected = deepcopy(original)
    expected["initialCode"] = formatted_source(original["initialCode"])
    if current != expected or not source_format.require_source_format(original, current):
        raise ValueError("Special-source-format QA changed more than the one declared source spacing edit")
    previous_archives = before["stateLayout"].get("sourceFormatRevisions", [])
    if any(entry["id"] == identity for entry in previous_archives):
        raise ValueError("The special-source-format baseline unexpectedly already archives this owner")
    if after["stateLayout"].get("sourceFormatRevisions") != [
            *previous_archives, {"id": identity, "block": original}]:
        raise ValueError("Special-source-format QA replaced an archive or added another owner")
    previous_edges = before["stateLayout"]["editorial"]
    if after["stateLayout"]["editorial"] != [
            *previous_edges, {"id": identity, "beforeSha256": state.fingerprint(original),
                              "afterSha256": state.fingerprint(current)}]:
        raise ValueError("Special-source-format QA changed its exact editorial prefix or format edge")
    restored = deepcopy(after)
    restored_owner = state._blocks(restored)[identity]
    restored_owner["initialCode"] = original["initialCode"]
    restored["stateLayout"]["sourceFormatRevisions"] = deepcopy(previous_archives)
    restored["stateLayout"]["editorial"] = deepcopy(previous_edges)
    if "sourceFormatRevisions" not in before["stateLayout"]:
        del restored["stateLayout"]["sourceFormatRevisions"]
    if restored != before:
        raise ValueError("Special-source-format QA changed unrelated metadata, curriculum or history")
    if state.validate(before) != state.validate(after):
        raise ValueError("Special-source-format QA changed a stable storage address")
    state.validate_transition(before, after)


def lesson_payloads(name: str) -> tuple[dict[str, bytes], dict]:
    """Build a complete authenticated pair in memory; no export is required."""
    pin = PUBLICATIONS[name]
    original_bytes = published_bytes(name)
    before = state.parse_json(original_bytes.decode("utf-8"))
    family = {"es": before, **{locale: state.parse_json(published_bytes(name, locale).decode("utf-8"))
                              for locale in ("en", "fr")}}
    state.validate(before)
    original = state._blocks(before)[pin["owner"]]
    if (original.get("type") != "cpp_compiler" or state.fingerprint(original) != pin["blockSha256"]
            or sha256(original["initialCode"].encode("utf-8")) != pin["sourceSha256"]):
        raise ValueError(f"Special-source-format QA changed its authentic compiler owner: {name}")
    if (pin["kind"] == "crt-output" and not state.reviewed_crt_output_variant(original)
            or pin["kind"] == "intentional-exit" and not state.reviewed_intentional_exit_goal(original)):
        raise ValueError("Special-source-format QA lost the original reviewed special objective")
    execution = execution_source(name, before)
    revised_source = formatted_source(original["initialCode"])
    if sha256(revised_source.encode("utf-8")) != pin["formattedSourceSha256"]:
        raise ValueError("Special-source-format QA changed its independently pinned formatting edit")
    modified = deepcopy(before)
    state._blocks(modified)[pin["owner"]]["initialCode"] = revised_source
    after = state.prepare_v2(modified, "es", family, before)
    verify_format_only(name, before, after)

    before_name, after_name = f"{name}/before.json", f"{name}/after.json"
    revised_bytes = json_bytes(after)
    return {before_name: original_bytes, after_name: revised_bytes}, {
        "id": name, "lessonId": before["id"], "publishedPath": pin["path"],
        "owner": pin["owner"], "sourcePath": "/initialCode", "kind": pin["kind"],
        "before": before_name, "after": after_name,
        "beforeSha256": sha256(original_bytes), "afterSha256": sha256(revised_bytes),
        "source": original["initialCode"], "sourceSha256": pin["sourceSha256"],
        "formattedSource": revised_source, "formattedSourceSha256": pin["formattedSourceSha256"],
        "outputCheck": deepcopy(original["outputCheck"]), "execution": execution,
    }


def export_payloads() -> dict[str, bytes]:
    from special_source_format_cases import historical_documents, invalid_documents

    payloads, lessons = {}, []
    for name in PUBLICATIONS:
        files, lesson = lesson_payloads(name)
        payloads.update(files)
        before = state.parse_json(files[lesson["before"]].decode("utf-8"))
        after = state.parse_json(files[lesson["after"]].decode("utf-8"))
        # These additional documents are derived QA controls and retirement
        # scenarios, never additional historical Git publications.
        for category, documents in (
                ("negative", invalid_documents(before, after, lesson["owner"])),
                ("historical", historical_documents(before, after, lesson["owner"]))):
            lesson[category] = []
            for label, document in documents.items():
                if (type(label) is not str or re.fullmatch(r"[a-z][a-z0-9_]*", label) is None
                        or type(document) is not dict):
                    raise ValueError("Special-source-format QA received an invalid derived case")
                path = f"{name}/{category}/{label}.json"
                payload = json_bytes(document)
                payloads[path] = payload
                lesson[category].append({"name": label, "path": path, "sha256": sha256(payload)})
        lessons.append(lesson)
    payloads["manifest.json"] = json_bytes({
        "version": 1, "kind": "special-source-format", "publishedCommit": PUBLISHED_COMMIT,
        "lessons": lessons,
    })
    return payloads


def export(output_dir: Path) -> Path:
    output = checked_output(output_dir)
    payloads = export_payloads()
    # Authenticate every source and preflight every destination before the
    # first write. An existing different file is never overwritten.
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
    arguments = parser.parse_args()
    print(export(arguments.output_dir))

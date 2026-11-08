"""Prepare the existing Spanish/Dora resources for isolated Windows CI.

The Kotlin manifests remain the authority. Only their six Spanish/Dora files
are downloaded; the English FP32 pack and its notices come from this checkout.
No source asset, model contract, recording or learner profile is modified.

  python -B scripts/windows/prepare_narration_ci_resources.py \
      --resources out/qa/narration-resources/kokoro \
      --english-resources out/qa/narration-resources/english

Use --verify-only to verify both destinations without network or writes.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import sys
import tempfile
from urllib.parse import urlsplit
import urllib.request


ROOT = Path(__file__).resolve().parents[2]
NARRATION = ROOT / "app/src/main/java/com/learngraphicsprogramming/app/narration"
sys.path.insert(0, str(ROOT / "scripts/narration"))
import prepare_english_ai_pack as english_pack

Artifact = english_pack.Artifact
SHARED_NAMES = frozenset({"model.onnx", "tokenizer.json", "tokenizer_config.json", "config.json"})
CRLF_CONFIGURATIONS = frozenset({"en_gb_bart.json", "en_us_bart.json"})
_ARTIFACT = re.compile(
    r'\bArtifact\(\s*"([^"$\\/]+)",\s*"([^"$\\]+)",\s*([0-9][0-9_]*)L,\s*'
    r'"([0-9a-f]{64})"\s*(?:,\s*"([^"$\\]+)"\s*)?\)')


def _constant(text: str, name: str) -> str:
    matches = re.findall(r'\bconst\s+val\s+' + re.escape(name) + r'\s*=\s*"([^"$\\]+)"', text)
    if len(matches) != 1:
        raise ValueError(f"Unsupported Kokoro constant: {name}")
    return matches[0]


def _pinned_url(url: str) -> None:
    parsed = urlsplit(url)
    if (not english_pack.stable_https_url(url) or parsed.hostname != "huggingface.co"
            or not re.fullmatch(r"/[^/]+/[^/]+/resolve/[0-9a-f]{40}/[^?]+", parsed.path)
            or any(part in (".", "..") for part in parsed.path.split("/"))):
        raise ValueError(f"Kokoro resources require an immutable HTTPS revision: {url}")


def spanish_contract(model_manifest: Path = NARRATION / "KokoroModelManifest.kt",
                     voice_manifest: Path = NARRATION / "KokoroVoiceCatalog.kt") -> list[Artifact]:
    text = model_manifest.read_text(encoding="utf-8-sig")
    revision, source = _constant(text, "revision"), _constant(text, "source")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Kokoro must retain its immutable model revision")
    if not re.search(r'val\s+url:\s*String\s+get\(\)\s*=\s*sourceUrl\s*\?:\s*'
                     r'"\$source/resolve/\$revision/\$remotePath"', text):
        raise ValueError("Unsupported Kokoro artifact URL contract")
    shared = re.search(r'\bval\s+sharedArtifacts\s*=\s*listOf\((.*?)\n\s*\)', text, re.DOTALL)
    if shared is None:
        raise ValueError("Missing Kokoro shared artifacts")
    entries = _ARTIFACT.findall(shared[1])
    if len(entries) != 4 or {entry[0] for entry in entries} != SHARED_NAMES:
        raise ValueError("Expected the four original shared Kokoro artifacts")
    lexical = [entry for entry in _ARTIFACT.findall(text) if entry[0] == "es_lexicon_cache.json"]
    if len(lexical) != 1 or not lexical[0][4]:
        raise ValueError("Expected the original independently pinned Spanish lexicon")
    entries += lexical
    artifacts = [Artifact(name, int(size.replace("_", "")), sha, url or f"{source}/resolve/{revision}/{remote}")
                 for name, remote, size, sha, url in entries]
    voices = voice_manifest.read_text(encoding="utf-8-sig")
    dora = re.findall(r'\bKokoroVoice\("ef_dora",\s*NarrationLanguage\.SPANISH,\s*"([0-9a-f]{64})"\)', voices)
    size = re.findall(r'"\$id\.bin",\s*"voices/\$id\.bin",\s*([0-9][0-9_]*)L,\s*sha256\)', voices)
    if len(dora) != 1 or len(size) != 1:
        raise ValueError("Expected the original Spanish Dora voice artifact contract")
    artifacts.append(Artifact("ef_dora.bin", int(size[0].replace("_", "")), dora[0],
                              f"{source}/resolve/{revision}/voices/ef_dora.bin"))
    for artifact in artifacts:
        if artifact.size <= 0 or artifact.name in (".", ".."):
            raise ValueError("Empty or invalid Kokoro artifacts are forbidden")
        _pinned_url(artifact.url)
    return artifacts


def _is_link(path: Path) -> bool:
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def _exists(path: Path) -> bool:
    return path.exists() or _is_link(path)


def _physical_path(path: Path) -> Path:
    path = path.absolute()
    for part in (path, *path.parents):
        if _is_link(part):
            raise ValueError(f"Linked resource destinations are forbidden: {part}")
    return path.resolve()


def destinations(resources: Path, english: Path) -> tuple[Path, Path]:
    resources, english = _physical_path(resources), _physical_path(english)
    if resources.is_relative_to(english) or english.is_relative_to(resources):
        raise ValueError("Spanish and English resource destinations must be separate, non-nested directories")
    for destination in (resources, english):
        for protected in (ROOT / "app", ROOT / "english_misaki"):
            if destination.is_relative_to(protected) or protected.is_relative_to(destination):
                raise ValueError(f"Resource staging cannot write into or contain source assets: {destination}")
        if destination.exists() and not destination.is_dir():
            raise ValueError(f"Resource destination is not a directory: {destination}")
    return resources, english


def _publish(temporary: Path, destination: Path, artifact: Artifact) -> None:
    english_pack.verify_file(temporary, artifact)
    try:
        # Same-directory hard linking publishes the verified complete file
        # atomically and cannot replace a concurrently created destination.
        os.link(temporary, destination)
    except FileExistsError:
        english_pack.verify_file(destination, artifact)


def _existing_files(directory: Path) -> set[str]:
    _physical_path(directory)
    if not directory.exists():
        return set()
    if not directory.is_dir():
        raise ValueError(f"Resource destination is not a directory: {directory}")
    result = set()
    for path in directory.rglob("*"):
        if _is_link(path):
            raise ValueError(f"Linked resource files/directories are forbidden: {path}")
        if path.is_file():
            result.add(path.relative_to(directory).as_posix())
    return result


def _preflight(directory: Path, artifacts: list[Artifact]) -> None:
    unexpected = _existing_files(directory) - {artifact.name for artifact in artifacts}
    if unexpected:
        raise ValueError(f"Unexpected resource files: {sorted(unexpected)}")
    for artifact in artifacts:
        if _exists(directory / artifact.name):
            english_pack.verify_file(directory / artifact.name, artifact)


def download_verified(destination: Path, artifact: Artifact) -> None:
    _physical_path(destination)
    if _exists(destination):
        english_pack.verify_file(destination, artifact)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{destination.name}-", suffix=".part", dir=destination.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            request = urllib.request.Request(artifact.url, headers={"Accept-Encoding": "identity"})
            with urllib.request.urlopen(request, timeout=60) as response:
                if urlsplit(response.geturl()).scheme != "https":
                    raise ValueError("Kokoro download did not retain HTTPS")
                received = 0
                while chunk := response.read(1024 * 1024):
                    received += len(chunk)
                    if received > artifact.size:
                        raise ValueError(f"Download exceeds the manifest size: {artifact.name}")
                    output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        _publish(temporary, destination, artifact)
    finally:
        temporary.unlink(missing_ok=True)


def _write_verified(destination: Path, payload: bytes, artifact: Artifact) -> None:
    _physical_path(destination)
    if _exists(destination):
        english_pack.verify_file(destination, artifact)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{destination.name}-", suffix=".part", dir=destination.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        _publish(temporary, destination, artifact)
    finally:
        temporary.unlink(missing_ok=True)


def _english_payload(payload: bytes, artifact: Artifact) -> bytes:
    def matches(value: bytes) -> bool:
        return len(value) == artifact.size and hashlib.sha256(value).hexdigest() == artifact.sha256
    if matches(payload):
        return payload
    if artifact.name in CRLF_CONFIGURATIONS and b"\r" not in payload:
        original = payload.replace(b"\n", b"\r\n")
        if matches(original):
            return original
    raise ValueError(f"Original English artifact size/SHA-256 mismatch: {artifact.name}")


def stage_english(destination: Path, source: Path = english_pack.DEFAULT_ASSETS) -> None:
    artifacts = english_pack.contract()
    expected = {a.name for a in artifacts} | english_pack.REQUIRED_NOTICES | {"provenance.json"}
    if not source.is_dir() or _is_link(source):
        raise ValueError(f"Missing original English source assets: {source}")
    actual = set()
    for path in source.rglob("*"):
        if _is_link(path):
            raise ValueError(f"Linked English source assets are forbidden: {path}")
        if path.is_file():
            actual.add(path.relative_to(source).as_posix())
    if actual != expected:
        raise ValueError(f"Unexpected or missing English source assets: {sorted(actual ^ expected)}")
    english_pack.validate_metadata(source, artifacts)
    files = [(artifact, _english_payload((source / artifact.name).read_bytes(), artifact))
             for artifact in artifacts]
    for name in sorted(english_pack.REQUIRED_NOTICES | {"provenance.json"}):
        payload = (source / name).read_bytes()
        files.append((Artifact(name, len(payload), hashlib.sha256(payload).hexdigest(), ""), payload))
    # Reject an incompatible existing file before adding any staged files.
    _preflight(destination, [artifact for artifact, _ in files])
    for artifact, payload in files:
        _write_verified(destination / artifact.name, payload, artifact)
    english_pack.verify_staged_assets(destination)


def prepare_spanish(destination: Path, artifacts: list[Artifact]) -> None:
    _preflight(destination, artifacts)
    for artifact in artifacts:
        download_verified(destination / artifact.name, artifact)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--english-resources", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args(argv)
    try:
        resources, english = destinations(args.resources, args.english_resources)
        artifacts = spanish_contract()
        if not args.verify_only:
            stage_english(english)
            prepare_spanish(resources, artifacts)
        actual = _existing_files(resources)
        expected = {artifact.name for artifact in artifacts}
        if actual != expected:
            raise ValueError(f"Unexpected or missing Spanish/Dora resources: {sorted(actual ^ expected)}")
        for artifact in artifacts:
            english_pack.verify_file(resources / artifact.name, artifact)
        pack = english_pack.verify_staged_assets(english)
    except (OSError, ValueError) as error:
        print(f"Narration CI resource preparation FAILED: {error}", file=sys.stderr)
        return 1
    print(f"Verified {len(artifacts)} original Spanish/Dora resources "
          f"({sum(a.size for a in artifacts)} bytes) at {resources}")
    print(f"Verified {pack.artifact_count} original English FP32 artifacts "
          f"({pack.total_artifact_bytes} bytes) and {pack.metadata_file_count} audited provenance/license files at {english}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

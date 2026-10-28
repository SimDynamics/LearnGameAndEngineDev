"""Verify the existing English FP32 assets without downloads or changes.

The historical filename is retained from the Android verifier. This Windows
adaptation checks only existing local files; it does not prepare or export
models. EnglishAuxResources.kt remains the authority for the eight artifact
names, sizes, SHA-256 values and source URLs. Audited provenance and notices
are pinned to Android commit 0d765ea910e2a3557d8e84ce52507c298b6b7a4e.

  python scripts/narration/prepare_english_ai_pack.py --assets <directory> --verify-staged
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "app/src/main/java/com/learngraphicsprogramming/app/narration/EnglishAuxResources.kt"
DEFAULT_ASSETS = ROOT / "english_misaki/src/main/assets"
PACK_NAME = "english_misaki"
AUDITED_SOURCE_COMMIT = "0d765ea910e2a3557d8e84ce52507c298b6b7a4e"
REQUIRED_NOTICES = frozenset({
    "licenses/NOTICE.txt", "licenses/APACHE-2.0.txt", "licenses/BART-ATTRIBUTION.txt",
    "licenses/SPACY-POS-ATTRIBUTION.txt", "licenses/SPACY-MODEL-MIT.txt",
    "licenses/SPACY-MIT.txt", "licenses/THINC-MIT.txt", "licenses/SPACY-LOOKUPS-MIT.txt",
})
REQUIRED_SOURCES = {
    "bart-us": "Apache-2.0", "bart-gb": "Apache-2.0", "spacy-pos": "MIT",
    "spacy-frontend": "MIT", "thinc-architecture": "MIT", "spacy-lexeme-normalization": "MIT",
}
# Git checkouts may use CRLF for these text files. Only CRLF is normalized to
# LF for this audit; binary/configuration artifact hashes remain byte-exact.
AUDITED_METADATA_SHA256 = {
    "provenance.json": "32512d328c4ef40714a6ac4a690768518b0ab8de0b21bf5fc1d28c5a04be96a3",
    "licenses/APACHE-2.0.txt": "c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4",
    "licenses/BART-ATTRIBUTION.txt": "c159c39212299db1075ad6680827b5ab16b7d42f981c63441e43eb7086611069",
    "licenses/NOTICE.txt": "4b7ec60be3fd00094cd01299044a952d0b93dc3cdbd6026d56c89ec9461bf10a",
    "licenses/SPACY-LOOKUPS-MIT.txt": "f7f74ea82f9a10d7ce2ba05dd7e460c8c2bb42a6cc7d9ae71ab35661b5f71c9b",
    "licenses/SPACY-MIT.txt": "3f4378e67708fecacd3e27651c8bc0f977cf20789e19da0d81a2840e7cf573a2",
    "licenses/SPACY-MODEL-MIT.txt": "3933c176979b68bc6d0bcc902c7d6c130f1d127f476f17ba5cdba8d99cfd0012",
    "licenses/SPACY-POS-ATTRIBUTION.txt": "34b650bf533bca28b7b43cd920ce1b8511d63030cab9e84fe7d7e19d4cc795f2",
    "licenses/THINC-MIT.txt": "d7192459f33a5d8c66358e2115ccbd3265478750f0b9d15f0c22e68e3ce6cc1e",
}


@dataclass(frozen=True)
class Artifact:
    name: str
    size: int
    sha256: str
    url: str


@dataclass(frozen=True)
class VerificationResult:
    artifact_count: int
    total_artifact_bytes: int
    metadata_file_count: int


def stable_https_url(url: str) -> bool:
    parsed = urlsplit(url)
    return (parsed.scheme == "https" and bool(parsed.hostname) and not parsed.username
            and not parsed.password and not parsed.query and not parsed.fragment
            and parsed.path not in ("", "/") and "\\" not in url)


def contract(manifest: Path = DEFAULT_MANIFEST) -> list[Artifact]:
    text = Path(manifest).read_text(encoding="utf-8-sig")
    release = re.search(r'const\s+val\s+release\s*=\s*"([^"$\\]+)"', text)
    base = re.search(r'const\s+val\s+downloadBase\s*=\s*"([^"\\]+)"', text)
    if release is None or base is None or 'sourceUrl = "$downloadBase/$name"' not in text:
        raise ValueError("Unsupported EnglishAuxResources HTTPS source contract")
    base_url = base[1].replace("$release", release[1])
    if "$" in base_url or not stable_https_url(base_url):
        raise ValueError("English resources require a stable absolute HTTPS source")
    matches = re.findall(r'\bartifact\("([^"\\/]+)",\s*(\d+)L,\s*"([a-f0-9]{64})"\)', text)
    artifacts = [Artifact(name, int(size), sha256, base_url + "/" + name)
                 for name, size, sha256 in matches]
    if len(artifacts) != 8 or len({a.name for a in artifacts}) != 8:
        raise ValueError("EnglishAuxResources must declare exactly eight distinct artifacts")
    if sum(a.name.endswith(".onnx") for a in artifacts) != 5 or sum(a.name.endswith(".json") for a in artifacts) != 3:
        raise ValueError("Expected the existing five ONNX graphs and three JSON configurations")
    if any(a.size <= 0 or a.name in (".", "..") for a in artifacts):
        raise ValueError("Empty or invalid model artifacts are forbidden")
    return artifacts


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            result.update(chunk)
    return result.hexdigest()


def _is_link(path: Path) -> bool:
    # Junctions also redirect outside the staged tree on Windows.
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def _regular_file(path: Path) -> None:
    if not path.is_file() or _is_link(path):
        raise ValueError(f"Missing original regular file: {path}")


def verify_file(path: Path, artifact: Artifact) -> None:
    _regular_file(path)
    if path.stat().st_size != artifact.size:
        raise ValueError(f"Size mismatch: {path}")
    if digest(path) != artifact.sha256:
        raise ValueError(f"SHA-256 mismatch: {path}")


def validate_provenance(provenance: dict, artifacts: list[Artifact]) -> None:
    if provenance.get("format") != 1 or provenance.get("packName") != PACK_NAME:
        raise ValueError("Unsupported English resource provenance")
    if provenance.get("artifactContract") != "EnglishAuxResources.artifacts" or provenance.get("precision") != "FP32":
        raise ValueError("Provenance must preserve the existing FP32 artifact contract")
    if provenance.get("totalArtifactBytes") != sum(a.size for a in artifacts):
        raise ValueError("Provenance size differs from EnglishAuxResources")
    expected = {a.name: (a.size, a.sha256) for a in artifacts}
    entries = provenance.get("artifacts", [])
    actual = {a.get("fileName"): (a.get("bytes"), a.get("sha256")) for a in entries}
    if len(entries) != 8 or actual != expected:
        raise ValueError("Provenance hashes differ from EnglishAuxResources")
    sources = provenance.get("sources", [])
    source_licenses = {source.get("id"): source.get("license") for source in sources}
    if source_licenses != REQUIRED_SOURCES or len(sources) != len(REQUIRED_SOURCES):
        raise ValueError("Missing audited original sources/licenses")
    if any(a.get("source") not in REQUIRED_SOURCES for a in entries):
        raise ValueError("Unattributed artifact source")
    if any(not stable_https_url(source.get("sourceUrl", "")) or
           not stable_https_url(source.get("licenseDeclarationUrl", "")) for source in sources):
        raise ValueError("Missing original source/license references")
    notices = provenance.get("notices", [])
    if set(notices) != REQUIRED_NOTICES or len(notices) != len(REQUIRED_NOTICES):
        raise ValueError("Incomplete license/attribution notices")


def validate_metadata(assets: Path, artifacts: list[Artifact]) -> None:
    for name, expected_sha256 in AUDITED_METADATA_SHA256.items():
        path = assets / name
        _regular_file(path)
        payload = path.read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(payload).hexdigest() != expected_sha256:
            raise ValueError(f"Audited provenance/license SHA-256 mismatch: {name}")
    provenance = json.loads((assets / "provenance.json").read_text(encoding="utf-8-sig"))
    if not isinstance(provenance, dict):
        raise ValueError("English resource provenance must be a JSON object")
    validate_provenance(provenance, artifacts)


def verify_staged(assets: Path, artifacts: list[Artifact] | None = None,
                  *, manifest: Path = DEFAULT_MANIFEST) -> VerificationResult:
    """Validate the complete staged English asset directory, reading files only.

    Supply artifacts from contract() to share a parsed runtime contract. With no
    explicit list the unchanged Kotlin contract is read from manifest instead.
    A verification failure raises ValueError or OSError; no resources are repaired.
    """
    assets = Path(assets)
    artifacts = contract(manifest) if artifacts is None else artifacts
    if not assets.is_dir() or _is_link(assets):
        raise ValueError(f"Missing original asset directory: {assets}")
    expected = {a.name for a in artifacts} | REQUIRED_NOTICES | {"provenance.json"}
    actual = set()
    for path in assets.rglob("*"):
        if _is_link(path):
            raise ValueError(f"Linked resource assets are forbidden: {path}")
        if path.is_file():
            actual.add(path.relative_to(assets).as_posix())
    if actual != expected:
        raise ValueError(f"Unexpected or missing resource assets: {sorted(actual ^ expected)}")
    validate_metadata(assets, artifacts)
    for artifact in artifacts:
        verify_file(assets / artifact.name, artifact)
    return VerificationResult(len(artifacts), sum(a.size for a in artifacts),
                              len(REQUIRED_NOTICES) + 1)


def verify_staged_assets(assets: Path = DEFAULT_ASSETS,
                         *, manifest: Path = DEFAULT_MANIFEST) -> VerificationResult:
    """Offline entry point for the Windows distribution builder."""
    return verify_staged(assets, manifest=manifest)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--assets", type=Path, default=DEFAULT_ASSETS)
    parser.add_argument("--verify-staged", action="store_true", required=True)
    args = parser.parse_args(argv)
    try:
        result = verify_staged_assets(args.assets, manifest=args.manifest)
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        print(f"English resource verification FAILED: {error}", file=sys.stderr)
        return 1
    print(f"Verified {result.artifact_count} existing FP32 resources "
          f"({result.total_artifact_bytes} bytes) and {result.metadata_file_count} "
          "audited provenance/license files; no files changed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

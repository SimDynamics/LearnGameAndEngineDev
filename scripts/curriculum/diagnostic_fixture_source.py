"""Export three immutable pre-diagnostic lessons into a native QA build tree.

Only pinned local Git bytes are accepted. No fetch, live-asset fallback,
rewriting of existing different files or writes into authored trees are allowed.
"""
from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
import os
from pathlib import Path
import stat
import subprocess


ROOT = Path(__file__).resolve().parents[2]
BASELINE_COMMIT = "7862f1064487a961d5cfc9668d4316da6d1b3ef2"
RELATIVE_UNIT = "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido"
FIXTURE_SHA256 = {
    "lesson_consultas_const_y_objetos_constantes.json": "81361b13427e9e5a0364e4f7985829f4c09e0e987fc652b8bee103f77bc1babe",
    "lesson_valores_por_defecto_con_significado.json": "72d00361184d58d72e0c7f362f08ea8f9a707faa85373c0d23d84cbaa676dc70",
    "lesson_argumentos_con_tipos_que_expresan_su_funcion.json": "baa356c561869b91b18adc340e8dbc61c2af0909c799b93894f41f8bff13a97a",
}


def verify_bytes(filename: str, payload: bytes) -> bytes:
    if filename not in FIXTURE_SHA256:
        raise ValueError(f"Unknown historical diagnostic fixture: {filename}")
    if hashlib.sha256(payload).hexdigest() != FIXTURE_SHA256[filename]:
        raise RuntimeError(f"Historical diagnostic fixture SHA-256 mismatch: {filename}")
    return payload


@lru_cache(maxsize=len(FIXTURE_SHA256))
def historical_bytes(filename: str) -> bytes:
    if filename not in FIXTURE_SHA256:
        raise ValueError(f"Unknown historical diagnostic fixture: {filename}")
    reference = f"{BASELINE_COMMIT}:{RELATIVE_UNIT}/{filename}"
    # A partial clone must not lazily fetch a missing object. These settings
    # affect only this child and never change Git/global/user configuration.
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    command = ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}", "show", reference]
    try:
        payload = subprocess.check_output(command, cwd=ROOT, stderr=subprocess.PIPE, env=environment)
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"Missing local diagnostic fixture {reference}; no fetch or current-asset fallback is permitted") from error
    return verify_bytes(filename, payload)


def checked_output(output_dir: Path) -> Path:
    absolute = Path(os.path.abspath(output_dir))
    for entry in (absolute, *absolute.parents):
        try:
            attributes = entry.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
            raise ValueError("Diagnostic fixture output cannot follow symlinks, junctions or reparse points")
    resolved = absolute.resolve()
    if resolved.is_relative_to(ROOT):
        relative = resolved.relative_to(ROOT)
        if not relative.parts or relative.parts[0].lower() not in {"out", "build"}:
            raise ValueError("Diagnostic fixtures cannot be exported into authored/source trees")
    parts = tuple(part.lower() for part in resolved.parts)
    if any(parts[index:index + 2] == ("app", "src") for index in range(len(parts) - 1)):
        raise ValueError("Diagnostic fixtures cannot be exported into authored/source trees")
    for parent in (resolved, *resolved.parents):
        if parent == ROOT:
            break  # This checkout can itself live inside an unrelated parent repository.
        if (parent / ".git").exists():
            raise ValueError("Diagnostic fixtures cannot be exported into another source checkout")
    return resolved


def existing_matches(target: Path, payload: bytes) -> bool:
    # Existing files are never opened for writing, including hard links.
    checked_output(target.parent)
    try:
        attributes = target.lstat()
    except FileNotFoundError:
        return False
    if (not stat.S_ISREG(attributes.st_mode) or stat.S_ISLNK(attributes.st_mode)
            or getattr(attributes, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)):
        raise FileExistsError(f"Diagnostic fixture destination is not an ordinary file: {target.name}")
    if target.read_bytes() != payload:
        raise FileExistsError(f"Refusing to overwrite a different diagnostic fixture: {target.name}")
    return True


def export_fixtures(output_dir: Path) -> tuple[Path, ...]:
    output = checked_output(output_dir)
    documents = {name: historical_bytes(name) for name in FIXTURE_SHA256}
    # Validate all three sources and all existing destinations before creating
    # anything. A conflict in the second/third file cannot leave earlier copies.
    for filename, payload in documents.items():
        existing_matches(output / filename, payload)
    output.mkdir(parents=True, exist_ok=True)
    checked_output(output)
    for filename, payload in documents.items():
        target = output / filename
        if existing_matches(target, payload):
            continue
        try:
            with target.open("xb") as stream:
                stream.write(payload)
        except FileExistsError:
            # Concurrent identical exporters may safely finish the same set;
            # a different or partial file still fails without being rewritten.
            if not existing_matches(target, payload):
                raise
    return tuple(output / filename for filename in documents)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        files = export_fixtures(arguments.output_dir)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Diagnostic fixtures not exported: {error}\n")
    print(f"Verified {len(files)} pre-diagnostic fixtures from local {BASELINE_COMMIT}.")


if __name__ == "__main__":
    main()

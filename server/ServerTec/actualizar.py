"""Apply a verified source-only release without resetting .venv or runtime state.

Standard-library only; no ServerTec application modules are imported.
Can run as a standalone downloaded file before installing dependencies. Stop the local service first.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import zipfile


class UpdateError(RuntimeError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalized_digest(data: bytes) -> str:
    return digest(data.replace(b'\r\n', b'\n'))


def safe_name(name: str) -> bool:
    # Flat, code-only package. No paths, archives, private keys, settings or DBs.
    return (name in {'.gitignore', '.gitattributes'} or
            re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*\.(py|ps1|md|txt)', name) is not None)


def no_links(path: Path) -> None:
    for item in (path, *path.parents):
        if item.is_symlink() or (hasattr(item, 'is_junction') and item.is_junction()):
            raise UpdateError('Destino con enlaces o junctions: se cancela sin sobrescribir.')


def apply_update(archive: Path, expected_sha256: str, destination: Path) -> int:
    if not re.fullmatch(r'[0-9a-fA-F]{64}', expected_sha256):
        raise UpdateError('Falta un SHA-256 valido del paquete.')
    if archive.stat().st_size > 5_000_000:
        raise UpdateError('El paquete excede el limite de codigo fuente.')
    package = archive.read_bytes()
    if digest(package) != expected_sha256.lower():
        raise UpdateError('SHA-256 incorrecto. No se cambiaron archivos.')
    import io
    with zipfile.ZipFile(io.BytesIO(package)) as z:
        names = z.namelist()
        if len(names) != len(set(names)) or not 2 <= len(names) <= 128:
            raise UpdateError('Paquete duplicado o incompleto.')
        if 'release-manifest.json' not in names:
            raise UpdateError('Falta el manifiesto de integridad.')
        if sum(i.file_size for i in z.infolist()) > 5_000_000:
            raise UpdateError('Contenido descomprimido demasiado grande.')
        manifest = json.loads(z.read('release-manifest.json'))
        if manifest.get('format') != 1 or not isinstance(manifest.get('files'), dict):
            raise UpdateError('Formato del manifiesto no compatible.')
        entries = manifest['files']
        if set(names) != set(entries) | {'release-manifest.json'}:
            raise UpdateError('Hay archivos no declarados en el paquete.')
        new_files = {}
        for name, entry in entries.items():
            if not safe_name(name) or PurePosixPath(name).name != name:
                raise UpdateError('El paquete contiene una ruta no permitida.')
            blob = z.read(name)
            if not isinstance(entry, dict) or digest(blob) != entry.get('sha256'):
                raise UpdateError('Contenido no coincide con el manifiesto.')
            new_files[name] = blob

    destination = destination.expanduser().absolute()
    no_links(destination)
    destination = destination.resolve()
    if destination.parent == destination:
        raise UpdateError('No se permite usar la raiz del disco como destino.')
    if destination.exists() and not destination.is_dir():
        raise UpdateError('El destino no es una carpeta.')
    # Read-only preflight for every file BEFORE replacing the first one.
    snapshots: dict[str, bytes | None] = {}
    for name, new in new_files.items():
        path = destination / name
        no_links(path)
        if path.exists():
            if not path.is_file() or path.stat().st_size > 2_000_000:
                raise UpdateError('Un archivo de destino no es codigo fuente regular.')
            current = path.read_bytes()
            acceptable = {normalized_digest(new), entries[name].get('previous_normalized_sha256')}
            if normalized_digest(current) not in acceptable:
                raise UpdateError('Se detectaron cambios locales en ' + name + '. No se sobrescribio nada.')
            snapshots[name] = current
        else:
            snapshots[name] = None
    destination.mkdir(parents=True, exist_ok=True)
    changed = [name for name, new in new_files.items() if snapshots[name] != new]
    if not changed:
        return 0
    applied = []
    with tempfile.TemporaryDirectory(prefix='.ServerTec-update-', dir=destination.parent) as temp:
        stage = Path(temp)
        for name in changed:
            (stage / name).write_bytes(new_files[name])
        try:
            for name in changed:
                target = destination / name
                no_links(target)
                current = target.read_bytes() if target.exists() else None
                if current != snapshots[name]:
                    raise UpdateError('Un archivo cambio durante la actualizacion: ' + name)
                os.replace(stage / name, target)
                applied.append(name)
        except BaseException:
            # Roll back only files written by THIS update and still matching it.
            # Never overwrite another program's concurrent edits.
            for name in reversed(applied):
                target = destination / name
                no_links(target)
                if target.read_bytes() != new_files[name]:
                    raise UpdateError('Cambio concurrente: revisa manualmente ' + name) from None
                prior = snapshots[name]
                if prior is None:
                    target.unlink()
                else:
                    (stage / name).write_bytes(prior)
                    os.replace(stage / name, target)
            raise
    return len(changed)


def main() -> int:
    parser = argparse.ArgumentParser(description='Actualizacion de codigo ServerTec; no toca datos ni abre puertos.')
    parser.add_argument('--archivo', required=True, type=Path)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--destino', required=True, type=Path)
    args = parser.parse_args()
    try:
        count = apply_update(args.archivo, args.sha256, args.destino)
    except UpdateError as error:
        print(str(error), file=sys.stderr)
        return 1
    except Exception as error:
        print('Actualizacion no completada: ' + type(error).__name__, file=sys.stderr)
        return 1
    print('ServerTec actualizado: ' + str(count) + ' archivos de codigo.')
    print('No se borraron .venv, certificados, ajustes, DB, ni archivos ajenos al paquete.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

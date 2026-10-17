import hashlib
import json
from pathlib import Path
import zipfile

import pytest

import actualizar
from actualizar import UpdateError, apply_update, digest, normalized_digest


def make_package(tmp_path, blobs=None, old=None):
    blobs = blobs or {'servertec.py': b'NEW\n', 'README.md': b'New docs\n'}
    old = old or {'servertec.py': b'OLD\n'}
    manifest = {'format': 1, 'files': {name: {'sha256': digest(content),
        'previous_normalized_sha256': normalized_digest(old[name]) if name in old else None}
        for name, content in blobs.items()}}
    archive = tmp_path / 'update.zip'
    with zipfile.ZipFile(archive, 'w') as z:
        z.writestr('release-manifest.json', json.dumps(manifest))
        for name, content in blobs.items():
            z.writestr(name, content)
    return archive, digest(archive.read_bytes())


def test_known_old_sources_update_and_other_files_never_touched(tmp_path):
    archive, sha = make_package(tmp_path)
    dest = tmp_path / 'ServerTec'
    dest.mkdir()
    (dest / 'servertec.py').write_bytes(b'OLD\r\n')  # git's Windows EOL is accepted
    for name in ('local-notes.txt', 'servertec.db', '.env'):
        (dest / name).write_bytes(b'PRIVATE_UNCHANGED')
    (dest / '.venv').mkdir()
    (dest / '.venv' / 'marker').write_bytes(b'VENV_UNCHANGED')
    assert apply_update(archive, sha, dest) == 2
    assert (dest / 'servertec.py').read_bytes() == b'NEW\n'
    for name in ('local-notes.txt', 'servertec.db', '.env'):
        assert (dest / name).read_bytes() == b'PRIVATE_UNCHANGED'
    assert (dest / '.venv' / 'marker').read_bytes() == b'VENV_UNCHANGED'
    assert apply_update(archive, sha, dest) == 0


def test_wrong_zip_hash_rejected_before_creating_destination(tmp_path):
    archive, _ = make_package(tmp_path)
    dest = tmp_path / 'absent'
    with pytest.raises(UpdateError, match='SHA-256 incorrecto'):
        apply_update(archive, '0' * 64, dest)
    assert not dest.exists()


def test_preflight_does_not_overwrite_any_local_edits(tmp_path):
    archive, sha = make_package(tmp_path)
    dest = tmp_path / 'ServerTec'
    dest.mkdir()
    (dest / 'servertec.py').write_bytes(b'OLD\n')
    (dest / 'README.md').write_bytes(b'MY_PERSONAL_EDIT')
    with pytest.raises(UpdateError, match='cambios locales'):
        apply_update(archive, sha, dest)
    assert (dest / 'servertec.py').read_bytes() == b'OLD\n'
    assert (dest / 'README.md').read_bytes() == b'MY_PERSONAL_EDIT'


@pytest.mark.parametrize('name', ['../evil.py', '/tmp/evil.py', 'state/config.json', 'private.pem', '.env', 'servertec.db'])
def test_archive_cannot_write_paths_or_private_runtime_files(tmp_path, name):
    archive, sha = make_package(tmp_path, {name: b'evil'})
    with pytest.raises(UpdateError):
        apply_update(archive, sha, tmp_path / 'ServerTec')
    assert not (tmp_path / 'ServerTec').exists()


def test_symlink_destination_or_existing_link_is_refused(tmp_path):
    archive, sha = make_package(tmp_path)
    other = tmp_path / 'other'
    other.mkdir()
    link = tmp_path / 'link'
    try:
        link.symlink_to(other, target_is_directory=True)
    except OSError:
        pytest.skip('Creating symlinks not permitted in this environment')
    with pytest.raises(UpdateError):
        apply_update(archive, sha, link)
    assert not list(other.iterdir())


def test_mid_update_oserror_rolls_back_only_own_writes(tmp_path, monkeypatch):
    archive, sha = make_package(tmp_path)
    dest = tmp_path / 'ServerTec'
    dest.mkdir()
    (dest / 'servertec.py').write_bytes(b'OLD\n')
    replace = actualizar.os.replace
    calls = []
    def fail_second(*args):
        calls.append(args)
        if len(calls) == 2:
            raise OSError('simulated_disk_failure')
        return replace(*args)
    monkeypatch.setattr(actualizar.os, 'replace', fail_second)
    with pytest.raises(OSError):
        apply_update(archive, sha, dest)
    assert (dest / 'servertec.py').read_bytes() == b'OLD\n'
    assert not (dest / 'README.md').exists()
    assert not list(tmp_path.glob('.ServerTec-update-*'))

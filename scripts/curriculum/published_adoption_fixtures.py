"""Pinned pre-adoption publications for transition tests.

Never derive a historical predecessor by deleting contracts from the live
lesson. These bytes come from an actual Git publication before adoption.
The live lesson is checked independently and is never rewritten by QA.
"""
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
PIN_MANIFEST = json.loads(Path(__file__).with_name(
    "published_adoption_pins.json").read_text(encoding="utf-8"))
BASE = PIN_MANIFEST["publication"]
PINS = PIN_MANIFEST["blobs"]


@lru_cache(maxsize=None)
def historical_bytes(path, *, epoch=None):
    relative = path.relative_to(ROOT).as_posix()
    record = PIN_MANIFEST if epoch is None else PIN_MANIFEST["epochs"][epoch]
    if relative not in record["blobs"]:
        raise ValueError("No authenticated pre-adoption publication: " + relative)
    expected = record["blobs"][relative]
    commit = record["publication"]
    environment = dict(os.environ, GIT_NO_LAZY_FETCH="1",
                       GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0")
    git = ["git", "--no-optional-locks", "-c", f"safe.directory={ROOT}"]
    publication = subprocess.run(
        git + ["show", commit + ":" + relative, "--"],
        cwd=ROOT, env=environment, capture_output=True)
    if publication.returncode == 0:
        payload = publication.stdout
    else:
        # A shallow checkout may still contain this *exact* immutable blob
        # when Work has not changed the lesson. Never read an unpinned live
        # file or strip declarations to simulate the historical predecessor.
        blob = subprocess.run(git + ["cat-file", "blob", expected],
                              cwd=ROOT, env=environment, capture_output=True)
        if blob.returncode != 0:
            raise ValueError(
                "Missing authenticated pre-adoption Git object for " + relative
                + "; fetch the pinned history before running transition QA")
        payload = blob.stdout
    digest = hashlib.sha1(b"blob " + str(len(payload)).encode("ascii")
                          + b"\0" + payload).hexdigest()
    if digest != expected:
        raise ValueError("Pre-adoption Git blob differs from its pin: " + relative)
    return payload


def before_adoption(path, *, epoch=None):
    return state.parse_json(historical_bytes(path, epoch=epoch).decode("utf-8-sig"))


def assert_live_owner_addresses(test, path, owners):
    """Validate live contracts and storage without inventing a new transition."""
    raw = path.read_bytes()
    live = state.parse_json(raw.decode("utf-8-sig"))
    baseline = before_adoption(path)
    old_positions = state.validate(baseline)
    live_positions = state.validate(live)
    test.assertEqual(baseline["id"], live["id"])
    test.assertEqual(baseline["stateLayout"]["family"], live["stateLayout"]["family"])

    def positions(document):
        return {owner: (p, b)
                for p, page in enumerate(document["stateLayout"]["current"])
                for b, owner in enumerate(page["blocks"])}

    old, current = positions(baseline), positions(live)
    for owner in owners:
        with test.subTest(path=path.name, owner=owner):
            test.assertIn(owner, old)
            test.assertIn(owner, current)
            test.assertEqual(old_positions[old[owner]], live_positions[current[owner]])
    test.assertEqual(path.read_bytes(), raw)
    return live

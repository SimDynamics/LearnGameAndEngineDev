"""A published edit after a paused recording must preserve its verified playback offset."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import uuid

import test_app_audio_revision as qa


BEFORE = "652cdf7dcea14e27d98a2f214a42da0621353481"
AFTER = "08887bd1495ac745ec77b361b9dd9ac4670c23c5"


def publication(revision, relative):
    path = (Path("app/src/main/assets/content") / relative).as_posix()
    return subprocess.check_output(["git", "show", f"{revision}:{path}"], cwd=qa.ROOT)


def current(snapshot):
    reader = snapshot.get("reader", {})
    parts = reader.get("segments", [])
    index = reader.get("index", -1)
    return reader, parts[index] if 0 <= index < len(parts) else {}


def update(path, content):
    temporary = path.with_suffix(".qa-new")
    temporary.write_bytes(content)
    temporary.replace(path)


def acceptance(executable, root, previous_executable=None):
    token, relative, location, _, copies = qa.published_fixture(root)
    destination = root / "content" / relative
    original = destination.read_bytes()
    old, new = (publication(revision, relative) for revision in (BEFORE, AFTER))
    old_document, new_document = (json.loads(raw.decode("utf-8-sig")) for raw in (old, new))
    page = 1
    before = old_document["pages"][page]["blocks"]
    after = new_document["pages"][page]["blocks"]
    qa.require(before[:26] == after[:26] and before[26]["responses"] != after[26]["responses"],
               "Expected the published update to change a later math exercise")
    exercise = next(index for index, block in enumerate(before) if block["type"] == "output_prediction")
    prefix = next((index, block) for index, block in enumerate(old_document["pages"][0]["blocks"])
                  if block["type"] == "multiple_choice")
    app = qa.Application(previous_executable or executable, root, token)
    update(destination, old)
    try:
        app.start()
        app.command("preferences", values={"kokoro_voice:es": "em_santa", "rate": 1.0})
        app.command("open", path=str(relative), location=location)
        app.command("answer_choice", block=prefix[0], option=prefix[1]["correctIndex"])
        app.command("page", page=page)
        app.command("pause")
        app.command("answer", block=exercise, answer=before[exercise]["answers"][0])
        app.wait(lambda snapshot: current(snapshot)[1].get("block") == exercise and
                 current(snapshot)[1].get("correct"), "confirmed feedback selected for playback", 90)
        app.command("play")

        def partial(snapshot):
            reader, segment = current(snapshot)
            return (reader.get("playing") and segment.get("block") == exercise and segment.get("feedback")
                    and segment.get("duration", 0) >= 3000 and 1100 < reader["offset"] < 1900)

        app.wait(partial, "genuine partial feedback prior to the later publication", 180)
        paused = app.command("pause")
        reader, segment = current(paused)
        saved = reader["offset"]
        recording = Path(segment["recording"])
        fingerprint, modified = qa.digest(recording), recording.stat().st_mtime_ns
        qa.require(1000 < saved < segment["duration"] - 500, "Pause did not land inside this recording")
        revision = paused["lesson"]["contentRevision"]
        if previous_executable:
            app.close()
        update(destination, new)
        if previous_executable:
            # Reopen the same isolated study session after both the program and
            # the source have changed, starting from its genuine old bookmark.
            app = qa.Application(executable, root, token)
            app.start()

        def adopted(snapshot):
            plan, part = current(snapshot)
            return ((previous_executable or snapshot.get("lesson", {}).get("contentRevision", 0) > revision) and plan.get("planned")
                    and plan["key"] != reader["key"] and part.get("available"))

        changed = app.wait(adopted, "new published guide adopted while paused", 180)
        rebound, passage = current(changed)
        qa.require(not rebound["playing"] and passage["region"] == segment["region"] and
                   passage["source"] == segment["source"] and rebound["offset"] == saved,
                   "A later edit moved the paused, unchanged passage")
        qa.require(Path(passage["recording"]) == recording and qa.digest(recording) == fingerprint and
                   recording.stat().st_mtime_ns == modified, "A later edit replaced the unchanged recording")
        app.command("play")
        resumed = app.wait(lambda snapshot: current(snapshot)[0].get("playing"),
                           "explicit resume after adoption", 90)
        resumed_reader, resumed_part = current(resumed)
        print(f"saved={saved}ms resumed={resumed_reader['offset']}ms "
              f"same_source={resumed_part['source'] == segment['source']}", flush=True)
        qa.require(resumed_part["region"] == segment["region"] and resumed_part["source"] == segment["source"]
                   and resumed_reader["offset"] >= saved - 1,
                   "An unrelated published revision replayed part of a verified recording")
        app.wait(lambda snapshot: current(snapshot)[0].get("playing") and
                 current(snapshot)[0]["offset"] >= saved + 200 and current(snapshot)[1]["source"] == segment["source"],
                 "actual clock advanced from the restored offset", 30)
        print("PASS unchanged published recording retained its partial clock across the subsequent math revision")
    finally:
        try:
            app.close()
        finally:
            update(destination, original)
            qa.verify_published_copies(copies)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--previous-executable", type=Path, help="Create the paused bookmark in this older binary and reopen in the current one")
    parser.add_argument("--qa-root", type=Path, default=Path("C:/LGAED-QA") / ("app-published-pause-" + uuid.uuid4().hex))
    args = parser.parse_args()
    acceptance(args.executable, qa.checked_root(args.qa_root), args.previous_executable)

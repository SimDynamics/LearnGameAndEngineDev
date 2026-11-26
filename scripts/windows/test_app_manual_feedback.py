"""Actual hidden Windows app: real +5s preview, restart, newer feedback and gate."""
from __future__ import annotations

import argparse
from copy import deepcopy
from pathlib import Path
import uuid

import test_app_audio_revision as qa


def fixture(root):
    qa.require(not root.exists(), "Use a new private QA directory")
    root.mkdir(parents=True)
    token = "app-manual-feedback-" + uuid.uuid4().hex[:20]
    qa.write_json(root / "qa-config.json", dict(qaInstance=token))
    source = qa.ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json"
    checksum, document = qa.digest(source), qa.read_json(source)
    original = document["pages"][1]["blocks"]
    indices = (25, 38, 39, 40, 41, 45)
    qa.require(original[25]["type"] == "multiple_choice" and original[38]["answers"] == ["6"]
               and original[39]["text"] == "Reparar un contador de tareas", "Reported sources changed")
    raw = dict(id=qa.LESSON, title="Lectura posterior a un avance manual", compilerPuzzlesRequired=True,
               pages=[dict(blocks=[deepcopy(original[index]) for index in indices])])
    lesson = qa.layout.prepare_v2(raw, "es", {"es": raw})
    owners = dict(zip((document["stateLayout"]["current"][1]["blocks"][index] for index in indices),
                      lesson["stateLayout"]["current"][0]["blocks"]))
    fields = [deepcopy(field) for field in document["textSemantics"]["fields"] if field["block"] in owners]
    for field in fields:
        field["block"] = owners[field["block"]]
    lesson["textSemantics"] = dict(version=1, fields=fields)
    qa.layout.validate(lesson)
    content = root / "content"
    qa.write_json(content / "es/programming/courses.json", [dict(id=qa.COURSE, title="Audio QA", number=1)])
    qa.write_json(content / "es/math_physics/courses.json", [])
    course = content / "es/programming" / qa.COURSE
    qa.write_json(course / f"course_{qa.COURSE}.json", dict(id=qa.COURSE, title="QA", units=[dict(id=qa.UNIT, title="QA", number=1)]))
    qa.write_json(course / "unit/unit_unit.json", dict(courseId=qa.COURSE, unitId=qa.UNIT, title="QA", lessons=[dict(id=qa.LESSON, title="QA", number=1)]))
    qa.write_json(content / qa.LESSON_RELATIVE, lesson)
    return token, original[25]["correctIndex"], source, checksum


def current(value):
    reader = value.get("reader", {})
    parts, index = reader.get("segments", []), reader.get("index", -1)
    return reader, parts[index] if 0 <= index < len(parts) else {}


def verify_later_answer(app, unheard, evidence, *, resume):
    app.command("answer", block=1, answer="6")
    prepared = app.wait(lambda value: value.get("reader", {}).get("planned") and
                        any(part["block"] == 1 and part["feedback"] and part["correct"]
                            for part in value["reader"]["segments"]),
                        "updated plan for the newly checked exercise", 60)["reader"]
    expected = [part["source"] for part in prepared["segments"] if part["block"] == 3 and not part["feedback"]]
    latest_feedback = [index for index, part in enumerate(prepared["segments"])
                       if part["block"] == 1 and part["feedback"] and part["correct"]]
    qa.require(latest_feedback and prepared["segments"][latest_feedback[-1] + 1]["block"] == 2,
               "Fixture no longer takes the last feedback sentence straight to the title")
    if resume:
        app.command("play")
    heard_last, title, instruction, observed = False, False, [], set()

    def completed(value):
        nonlocal heard_last, title
        reader, part = current(value)
        if not reader.get("planned"):
            return False
        if reader["playing"]:
            qa.require(part["block"] != 0, "A later answer returned to the earlier exercise")
            if part["block"] == 1 and part["feedback"]:
                qa.require(abs(reader["playbackRate"] - 3) < .02, "Actual application lost the selected3X rate")
                if reader["index"] == latest_feedback[-1]:
                    heard_last = True
            if part["block"] == 2:
                qa.require(heard_last and part["source"] == "Reparar un contador de tareas", "New feedback skipped its title")
                qa.require(all(item["feedbackFullyPlayed"] for item in reader["segments"]
                               if item["block"] == 1 and item["feedback"] and item["correct"]),
                           "Title bypassed genuinely unheard newer feedback")
                title = True
            if part["block"] == 3 and reader["index"] not in observed:
                qa.require(title, "Compiler instruction skipped its title")
                observed.add(reader["index"])
                instruction.append(part["source"])
                qa.pcm(Path(part["recording"]))
            qa.require(part["block"] <= 4, "Unresolved required compiler released later content")
        if not reader["waitingGate"] or reader["playing"] or part["block"] != 4:
            return False
        qa.require(title and instruction == expected and expected, "Instruction omitted, repeated or reordered source sentences")
        qa.require(all(not reader["segments"][index]["feedbackFullyPlayed"] for index in unheard)
                   and reader["heardThrough"] <= min(unheard),
                   "Later answer fabricated hearing of an earlier sentence")
        return True

    result = app.wait(completed, "new complete feedback, repair title, full instruction and unresolved compiler gate after a later answer", 90)
    qa.write_json(evidence, result)


def acceptance(executable, root):
    token, option, source, checksum = fixture(root)
    app = qa.Application(executable, root, token)
    try:
        app.start()
        app.command("preferences", values={"kokoro_voice:es": "em_santa", "rate": 3.0})
        app.command("open", path=str(qa.LESSON_RELATIVE), location=qa.LOCATION)
        app.ready()  # This regression tests explicit preview, not cold synthesis.
        app.command("pause")
        app.command("answer_choice", block=0, option=option)
        app.ready()
        app.command("play")
        played = app.wait(lambda value: current(value)[0].get("playing") and
                          current(value)[1].get("block") == 0 and current(value)[1].get("feedback") and
                          current(value)[1].get("duration", 0) > 7200 and
                          1400 <= current(value)[0]["offset"] <= 1900,
                          "genuine partial first feedback before the five-second preview", 90)
        heard = app.command("pause")["reader"]
        skipped = app.command("seek", seconds=5)["reader"]
        gap_index = skipped["index"]
        qa.require(gap_index == heard["index"] and skipped["offset"] == heard["offset"] + 5000,
                   "The actual controls did not preview five seconds inside the same recording")
        state = app.inspect()["lesson_narration_resume_bounded_v1"]
        slot = next(str(index) for index in range(3) if state.get(f"key_{index}") == skipped["key"])
        prefix = state[f"heard_prefix_v1_{slot}"]
        qa.require(int(prefix[1]) + 4_000_000 < state[f"position_{slot}"] * 1000,
                   "Preview fabricated hearing credit for the actual skipped PCM")
        qa.write_json(root / "actual-preview-gap.json", dict(reader=skipped, prefix=prefix))
        app.command("play")
        reached = app.wait(lambda value: current(value)[0].get("waitingGate") and
                           not current(value)[0].get("playing") and current(value)[1].get("block") == 1,
                           "every remaining first-answer sentence plays before the next unanswered gate", 90)
        first = [(index, part) for index, part in enumerate(reached["reader"]["segments"])
                 if part["block"] == 0 and part["feedback"] and part["correct"]]
        qa.require(len(first) > 1 and not reached["reader"]["segments"][gap_index]["feedbackFullyPlayed"] and
                   all(part["feedbackFullyPlayed"] for index, part in first if index != gap_index),
                   "Another first-answer sentence remained unheard or preview falsely became actual hearing")
        for _, part in first:
            qa.pcm(Path(part["recording"]))
        paused = app.command("pause")["reader"]
        before_restart = app.inspect()
        app.close()
        app.start()
        restored = app.ready()["reader"]
        qa.require(not restored["playing"] and not restored["allocated"] and restored["key"] == paused["key"] and
                   restored["index"] == paused["index"] and restored["offset"] == paused["offset"],
                   "Actual app restart changed the private next-exercise pause")
        after_restart = app.inspect()
        qa.require(before_restart.get("learning_progress") == after_restart.get("learning_progress"),
                   "Actual app restart changed a correctly checked response")
        verify_later_answer(app, [gap_index], root / "actual-restart-continuation.json", resume=True)

        # The older exercise is still solved. Replay it and check the next
        # exercise while its first feedback sentence is playing normally.
        app.command("reset_answer", block=1)
        app.ready()
        app.command("restart")
        interrupted = app.wait(lambda value: current(value)[0].get("playing") and
                 current(value)[1].get("block") == 0 and current(value)[1].get("feedback") and
                 600 <= current(value)[0]["offset"] <= 2500,
                 "natural playback inside the earlier solved exercise", 90)["reader"]
        older = [index for index, part in enumerate(interrupted["segments"])
                 if part["block"] == 0 and part["feedback"] and part["correct"]]
        qa.require(len(older) > 1 and interrupted["index"] == older[0] and
                   0 < interrupted["offset"] < interrupted["segments"][older[0]]["duration"] and
                   all(not interrupted["segments"][index]["feedbackFullyPlayed"] for index in older),
                   "The earlier feedback must include a partial sentence and a wholly unheard sentence")
        verify_later_answer(app, older, root / "actual-interrupted-feedback-continuation.json", resume=False)
        print(f"PASS actual Windows app Santa3X: genuine+5s gap, other feedback really played, full app restart, interrupted older feedback, newer feedback→title→instruction→required compiler; evidence={root}", flush=True)
    finally:
        try:
            app.close()
        finally:
            qa.require(qa.digest(source) == checksum, "Actual application QA altered its published source")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--qa-root", type=Path, default=Path("C:/LGAED-QA") / ("app-manual-feedback-" + uuid.uuid4().hex))
    args = parser.parse_args()
    acceptance(args.executable, qa.checked_root(args.qa_root))

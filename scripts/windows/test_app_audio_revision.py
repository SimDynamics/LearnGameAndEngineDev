"""Local acceptance through LearnGameAndEngineDev.exe, never a substitute reader.

Uses private synthetic curriculum/state/audio under C:\\LGAED-QA. The actual
application remains hidden and all actions target its own native controls.
No personal profile, desktop input, cloud job or model installation is used.
Raw application observations and page manifests are retained in the QA tree.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid
import wave

if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    _kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                    wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    _kernel32.CreateFileW.restype = wintypes.HANDLE
    _kernel32.GetFileSizeEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_longlong)]
    _kernel32.GetFileSizeEx.restype = wintypes.BOOL
    _kernel32.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                                 ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    _kernel32.ReadFile.restype = wintypes.BOOL
    _kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    _kernel32.CloseHandle.restype = wintypes.BOOL

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/curriculum"))
import stable_state_layout as layout

COURSE = "qa_audio_revision"
UNIT = COURSE + "__unit"
LESSON = UNIT + "__lesson"
NEW_LESSON = UNIT + "__new_lesson"
LOCATION = dict(locale="es", route="programming", course=COURSE, unit=UNIT)
LESSON_RELATIVE = Path("es/programming") / COURSE / "unit/lesson_lesson.json"
HEADING = "Usar el valor después del bloque"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def write_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".pending")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    replace_owned_file(temporary, path)


def replace_owned_file(temporary, path):
    deadline = time.monotonic() + 1
    while True:
        try:
            os.replace(temporary, path)
            break
        except OSError as failure:
            if getattr(failure, "winerror", None) != 32 or time.monotonic() >= deadline:
                raise
            time.sleep(.005)


def read_json(path: Path):
    if os.name == "nt" and path.name == "snapshot.json":
        # The application atomically replaces this file. A normal CRT reader
        # omits FILE_SHARE_DELETE and can transiently reject that replacement.
        handle = _kernel32.CreateFileW(str(path), 0x80000000, 0x7, None, 3, 0x80, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            length = ctypes.c_longlong()
            if not _kernel32.GetFileSizeEx(handle, ctypes.byref(length)):
                raise ctypes.WinError(ctypes.get_last_error())
            require(0 <= length.value <= 128 * 1024 * 1024, "Unexpected application snapshot size")
            buffer = ctypes.create_string_buffer(length.value)
            received = wintypes.DWORD()
            if not _kernel32.ReadFile(handle, buffer, length.value, ctypes.byref(received), None):
                raise ctypes.WinError(ctypes.get_last_error())
            if received.value != length.value:
                raise OSError("Application snapshot read was incomplete")
            payload = buffer.raw
        finally:
            _kernel32.CloseHandle(handle)
        # Parsing needs only these copied bytes. Release the file before JSON
        # decoding so it never keeps a replaced namespace entry pending.
        return json.loads(payload.decode("utf-8-sig"))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path: Path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_root(path: Path):
    path = path.resolve()
    require(path.is_relative_to(Path("C:/LGAED-QA").resolve()), "Acceptance output must remain under C:\\LGAED-QA")
    require(path != Path("C:/LGAED-QA").resolve(), "Use a dedicated new acceptance directory")
    return path


def paragraph(text):
    return dict(type="paragraph", text=text)


def fill(name, answer="ok"):
    return dict(type="fill_code", language="cpp", question=f"Escribe {answer} en {name}.",
                code="___", answers=[answer], explanation=f"Correcto. Terminó {name}.")


def fixture(root: Path, *, cold=False):
    require(not root.exists(), "A fresh run must preserve every existing QA directory")
    root.mkdir(parents=True)
    token = "app-audio-revision-" + uuid.uuid4().hex[:20]
    write_json(root / "qa-config.json", dict(qaInstance=token))
    content = root / "content"
    write_json(content / "es/programming/courses.json", [dict(id=COURSE, title="Audio QA", number=1)])
    write_json(content / "es/math_physics/courses.json", [])
    course_root = content / "es/programming" / COURSE
    write_json(course_root / f"course_{COURSE}.json", dict(id=COURSE, title="Audio QA", units=[dict(id=UNIT, title="QA", number=1)]))
    write_json(course_root / "unit/unit_unit.json", dict(courseId=COURSE, unitId=UNIT, title="QA", lessons=[dict(id=LESSON, title="QA", number=1)]))
    # Read the reported exercise verbatim; the real curriculum is never edited.
    source = read_json(ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json")
    blocks = source["pages"][1]["blocks"]
    saldo = deepcopy(next(block for block in blocks if block.get("answers") == ["saldo"]))
    require(blocks[blocks.index(saldo) + 1].get("text") == HEADING, "Reported saldo/heading source changed")
    pages = [dict(blocks=[paragraph(f"Página {index + 1}. El círculo azul avanza.")]) for index in range(32)]
    pages[1]["blocks"] = [paragraph("Inicio de la página."), saldo,
                            dict(type="heading", text=HEADING), paragraph("El valor continúa disponible."), fill("ejercicio final")]
    if cold:
        # Keep the reported prose, C++ caption/source and the first pending
        # exercise literal. The extra QA suffix deliberately remains cold
        # while the first real recording should already be playable.
        pages = [dict(blocks=deepcopy(blocks[:6]) + [paragraph(
            f"Apéndice de prueba número {index + 1}. La parte posterior número {index + 1} mantiene su propia frase sin generar mientras empieza la lectura anterior."
        ) for index in range(249)])]
    raw = dict(id=LESSON, title="Prueba local de narración", pages=pages)
    baseline = layout.prepare_v2(raw, "es", {"es": raw})
    if cold:
        owners = dict(zip(source["stateLayout"]["current"][1]["blocks"][:6],
                          baseline["stateLayout"]["current"][0]["blocks"][:6]))
        fields = []
        for field in source.get("textSemantics", {}).get("fields", []):
            if field["block"] in owners:
                field = deepcopy(field)
                field["block"] = owners[field["block"]]
                fields.append(field)
        baseline["textSemantics"] = dict(version=1, fields=fields)
        layout.validate(baseline)
    write_json(content / LESSON_RELATIVE, baseline)
    write_json(root / "baseline-fixture.json", baseline)
    return token, baseline


def published_fixture(root):
    require(not root.exists(), "A fresh published QA must preserve existing directories")
    root.mkdir(parents=True)
    token = "app-cold-published-" + uuid.uuid4().hex[:20]
    write_json(root / "qa-config.json", dict(qaInstance=token))
    content = ROOT / "app/src/main/assets/content"
    relative = Path("es/programming/cpp_fundamentals/edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json")
    course_root = content / "es/programming/cpp_fundamentals"
    # Opening through the real catalogue validates all active edition entries,
    # including lessons other than the one exercised below. Keep the complete
    # original course JSON tree, without borrowing another course's resources.
    paths = []
    for source in sorted(course_root.rglob("*.json")):
        require(source.resolve().is_relative_to(course_root.resolve()),
                "Published QA source escaped the original C++ course")
        paths.append(source.relative_to(content))
    paths.append(Path("es/programming/courses.json"))
    copies = []
    for path in paths:
        source, destination = content / path, root / "content" / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        original = source.read_bytes()
        destination.write_bytes(original)
        copies.append((source, destination, hashlib.sha256(original).hexdigest()))
    write_json(root / "content/es/math_physics/courses.json", [])
    lesson = read_json(content / relative)
    exercises = [(index, block) for index, block in enumerate(lesson["pages"][0]["blocks"])
                 if block["type"] in ("multiple_choice", "fill_code", "output_prediction", "math_puzzle", "cpp_compiler")]
    require(len(exercises) == 1 and exercises[0][1]["type"] == "multiple_choice",
            "Published preceding-page prefix changed; update its real QA control actions")
    location = dict(locale="es", route="programming", course="cpp_fundamentals",
                    unit="cpp_fundamentals__edition_es_1__expresiones_y_decisiones")
    verify_published_copies(copies)
    return token, relative, location, exercises[0], copies


def verify_published_copies(copies):
    for source, copy, checksum in copies:
        require(digest(source) == checksum and digest(copy) == checksum,
                f"Published cold QA modified its literal curricular source: {source}")


class Application:
    def __init__(self, executable: Path, root: Path, token: str):
        self.executable, self.root, self.token = executable.resolve(), root, token
        self.process = None
        self.sequence = 0

    def start(self):
        require(self.executable.is_file(), "Build the actual graphical application before acceptance")
        self.process = subprocess.Popen([str(self.executable), "--profile", "Codex", "--qa-instance", self.token,
                                         "--qa-audio-control", str(self.root)],
                                        cwd=ROOT, creationflags=subprocess.CREATE_NO_WINDOW)
        self.wait(lambda value: value.get("pid") == self.process.pid, "actual hidden application startup", 60)
        require(self.snapshot()["hidden"], "Acceptance must never display or activate the study window")
        self.sequence = int(self.snapshot().get("sequence", 0))

    def snapshot(self):
        try:
            value = read_json(self.root / "snapshot.json")
        except (OSError, ValueError):
            return {}
        if self.process and value.get("pid") != self.process.pid:
            return {}
        require(value.get("hidden", True) and not value.get("ownsForeground", False), "QA application unexpectedly displayed or activated a window")
        require(not value.get("error"), "Application QA command: " + str(value.get("error")))
        require(not value.get("reader", {}).get("error"), "Actual reader: " + str(value.get("reader", {}).get("error")))
        require(not value.get("generation", {}).get("error"), "Actual producer: " + str(value.get("generation", {}).get("error")))
        return value

    def wait(self, predicate, description, timeout=180):
        until = time.monotonic() + timeout
        value = {}
        while time.monotonic() < until:
            require(self.process.poll() is None, f"Actual application exited early: {self.process.returncode}; {description}")
            value = self.snapshot()
            if value and predicate(value):
                return value
            time.sleep(.025)
        value = self.snapshot() or value
        evidence = self.root / f"snapshot-timeout-{self.process.pid}-{time.time_ns()}.json"
        write_json(evidence, value)
        reader = value.get("reader", {})
        segments, index = reader.get("segments", []), reader.get("index", -1)
        current = segments[index] if isinstance(index, int) and 0 <= index < len(segments) else {}
        summary = dict(pid=value.get("pid", self.process.pid), task=description,
                       index=index, currentSource=current.get("source"),
                       currentAvailable=current.get("available"),
                       pageAudioComplete=reader.get("pageAudioComplete"),
                       playing=reader.get("playing"), allocated=reader.get("allocated"),
                       planned=reader.get("planned"), preparing=reader.get("preparing"),
                       readySegments=sum(bool(segment.get("available")) for segment in segments),
                       totalSegments=len(segments), snapshot=str(evidence))
        raise AssertionError("Timed out: " + json.dumps(summary, ensure_ascii=False))

    def command(self, name, **fields):
        self.sequence += 1
        write_json(self.root / "command.json", dict(sequence=self.sequence, command=name, **fields))
        return self.wait(lambda value: value.get("sequence") == self.sequence, name, 60)

    def ready(self):
        return self.wait(lambda value: value.get("reader", {}).get("planned") and
                         value.get("workerAvailableProcessors", 0) > 0 and
                         bool(value["reader"]["segments"]) and
                         bool(value["reader"].get("pageAudioRevision")) and
                         value["reader"].get("pageAudioComplete") and
                         all(segment["available"] for segment in value["reader"]["segments"]),
                         "full current page synthesis", 300)

    def inspect(self):
        self.command("flush")
        self.command("inspect")
        return read_json(self.root / "state-observation.json")

    def close(self):
        if self.process and self.process.poll() is None:
            self.sequence += 1
            try:
                write_json(self.root / "command.json", dict(sequence=self.sequence, command="close"))
                self.process.wait(timeout=60)
            except (OSError, subprocess.TimeoutExpired) as failure:
                # This handle belongs to the one process launched above. Its
                # private worker job closes with it; no process-name lookup or
                # personal application is involved in the bounded fallback.
                try:
                    self.process.terminate()
                    self.process.wait(timeout=10)
                except (OSError, subprocess.TimeoutExpired):
                    try:
                        self.process.kill()
                        self.process.wait(timeout=5)
                    except (OSError, subprocess.TimeoutExpired) as cleanup_failure:
                        failure.add_note(f"Owned QA cleanup also failed: {cleanup_failure}")
                raise
            require(self.process.returncode == 0, "Actual application did not close cleanly")


def pcm(path: Path):
    with wave.open(str(path), "rb") as recording:
        require(recording.getframerate() == 24000 and recording.getnchannels() == 1 and recording.getsampwidth() == 2,
                "Kokoro must supply actual mono24kHzPCM16")
        require(any(recording.readframes(recording.getnframes())), "Synthesized PCM is silent")


def manifests(root: Path):
    result = {}
    for path in (root / "profile/cache/kokoro/page-audio").glob("*.json"):
        value = read_json(path)
        page = value.get("page", value.get("pageId", ""))
        result[page] = dict(value=value, path=str(path), sha256=digest(path))
    return result


def page_manifest(rows, index):
    owner = f"page_{index:04d}"
    matches = [row for key, row in rows.items() if owner in key]
    require(len(matches) == 1, f"Expected one current page manifest for {owner}; keys={list(rows)}")
    return matches[0]


def recording_seal(rows, root):
    result = {}
    for row in rows.values():
        for segment in row["value"]["segments"]:
            path = root / "profile/cache/kokoro" / segment["wav"]
            result[str(path)] = dict(sha256=digest(path), modified=path.stat().st_mtime_ns)
    return result


def observations(root):
    payload = (root / "observations.jsonl").read_bytes()
    boundary = payload.rfind(b"\n")
    if boundary < 0:
        return []
    # The native app may currently be appending its next observation. Preserve
    # every complete line, including parse failures; only its unfinished tail
    # is excluded until the terminating newline has actually been written.
    return [json.loads(line) for line in payload[:boundary].split(b"\n")]


def recorded_generation_order(root, publication):
    result = []
    previous = None
    for observation in observations(root):
        if observation["tick"] < publication["tick"]:
            continue
        require(observation["pid"] == publication["pid"], "Generation trace crossed application lifetimes")
        generation = observation.get("generation", {})
        if not generation.get("running"):
            continue
        page, scan = generation["page"], generation["scans"]
        require(page and scan >= publication["generation"]["scans"], "Invalid generation identity or regressed scan")
        identity = (scan, page)
        if identity != previous:
            result.append(dict(pid=observation["pid"], scan=scan, page=page, tick=observation["tick"],
                               obsolete=scan == publication["generation"]["scans"]))
            previous = identity
    return result


def verify_generation_order(trace, publication):
    old = publication["generation"]
    for event in trace:
        if event["obsolete"]:
            require(event["page"] == old["page"], f"Another old-pass page started after publication:{trace}")
    fresh = [event for event in trace if not event["obsolete"]]
    # Adjacent samples/restarts of one page remain one uninterrupted turn.
    # A later A→B→A return is retained and fails this independent order oracle.
    turns = []
    for event in fresh:
        if not turns or turns[-1] != event["page"]:
            turns.append(event["page"])
    page_id = lambda index: f"programming/{LESSON}/{index}|es|ef_dora|KOKORO"
    expected = [page_id(index) for index in (2, 4, 7)]
    require(turns[:3] == expected, f"Changed future pages were not the next exact turns3,5,8:{trace}")
    require(page_id(1) not in turns, f"Background generated the foreground page:{trace}")
    require(all(turns.count(page) == 1 for page in expected), f"Background returned to an already completed urgent turn:{trace}")


def verify_cpu_configuration(root):
    background, foreground = [], []
    for value in observations(root):
        generation = value.get("generation", {})
        capacity = value.get("workerAvailableProcessors", 0)
        if generation.get("synthesisThreads", 0):
            require(generation.get("cpuClass") == "background", "Advance producer used the foreground CPU class")
            require(capacity > 0 and generation["synthesisThreads"] == min(2, capacity), "Advance inference did not configure the exact available two-processor bound")
            background.append(generation["synthesisThreads"])
        threads = value.get("reader", {}).get("foregroundSynthesisThreads", 0)
        if threads:
            require(capacity > 0 and threads == capacity, "Current-page inference did not configure every processor available to its worker")
            foreground.append(threads)
    require(background and max(background) <= 2, "Real advance synthesis did not demonstrate its two-processor bound")
    require(foreground and max(foreground) >= max(background), "Real current-page synthesis did not demonstrate priority resources")


def revise_prose(previous, indices, suffix):
    current = deepcopy(previous)
    for index in indices:
        current["pages"][index]["blocks"][0]["text"] += " " + suffix
    result = layout.prepare_v2(current, "es", {"es": previous}, previous)
    layout.validate_transition(previous, result)
    return result


def add_puzzles(previous):
    plan = deepcopy(previous["stateLayout"]["current"])
    page = plan[1]
    before, after = "qa_puzzle_before_saldo", "qa_puzzle_after_saldo"
    page["blocks"].insert(1, before)
    page["blocks"].insert(3, after)
    additions = [dict(id=owner, storage=dict(page=32, block=index), progressPage=page["id"], block=fill(label))
                 for index, (owner, label) in enumerate(((before, "nuevo anterior"), (after, "nuevo posterior")))]
    result = layout.reorder(previous, plan, additions)
    layout.validate_transition(previous, result)
    return result


def publish_new_lesson(root):
    raw = dict(id=NEW_LESSON, title="Página nueva publicada con la aplicación cerrada",
               pages=[dict(blocks=[paragraph("Esta página nueva también necesita su audio completo.")])])
    added = layout.prepare_v2(raw, "es", {"es": raw})
    unit_root = root / "content/es/programming" / COURSE / "unit"
    write_json(unit_root / "lesson_new_lesson.json", added)
    write_json(unit_root / "unit_unit.json", dict(courseId=COURSE, unitId=UNIT, title="QA", lessons=[
        dict(id=LESSON, title="QA", number=1), dict(id=NEW_LESSON, title=raw["title"], number=2)]))


def playback(app: Application, rate: int, block: int, heading: str | None = None,
             *, answer="saldo", next_block: int | None = None):
    app.command("pause")
    app.command("preferences", values=dict(rate=float(rate)))
    app.command("reset_answer", block=block)
    app.ready()
    app.command("answer", block=block, answer=answer)
    prepared = app.ready()["reader"]
    feedback_indices = [index for index, segment in enumerate(prepared["segments"])
                        if segment["feedback"] and segment["correct"] and segment["block"] == block]
    require(feedback_indices and feedback_indices == list(range(feedback_indices[0], feedback_indices[-1] + 1)),
            "Correct feedback must be one complete contiguous group in the authored page order")
    first_feedback, last_feedback = feedback_indices[0], feedback_indices[-1]
    require(prepared["index"] == first_feedback, "The paused correct answer did not select its first feedback segment")
    for index in feedback_indices:
        pcm(Path(prepared["segments"][index]["recording"]))
    app.command("play")
    before = app.wait(lambda value: value["reader"]["playing"] and
                      value["reader"]["segments"][value["reader"]["index"]]["feedback"] and
                      value["reader"]["segments"][value["reader"]["index"]]["correct"] and
                      value["reader"]["segments"][value["reader"]["index"]]["block"] == block,
                      f"actual correct feedback starts at{rate}X", 180)
    reader = before["reader"]
    require(abs(reader["rate"] - rate) < .001 and abs(reader["playbackRate"] - rate) < .02,
            "Actual MediaPlaybackSession did not adopt the requested speed")
    def group_position(value):
        observation = value["reader"]
        index = observation["index"]
        if index not in feedback_indices:
            return None
        return sum(part["duration"] for part in observation["segments"][first_feedback:index]) + observation["offset"]

    offset = group_position(before)
    app.wait(lambda value: group_position(value) is not None and group_position(value) > offset + 40,
             "real PCM media clock advances", 30)
    duration = sum(prepared["segments"][index]["duration"] for index in feedback_indices)
    reached = app.wait(lambda value: value["reader"]["playing"] and value["reader"]["index"] > last_feedback,
                       f"complete feedback automatically continues at{rate}X", duration / 1000 + 60)
    require(reached["reader"]["index"] == last_feedback + 1, "Automatic continuation skipped the following authored segment")
    next_part = reached["reader"]["segments"][reached["reader"]["index"]]
    if heading is not None:
        require(next_part["source"] == heading, f"Expected next heading, received{next_part['source']!r}")
    if next_block is not None:
        require(next_part["block"] == next_block and not next_part["feedback"],
                f"Expected the instruction of block{next_block}, received{next_part['source']!r}")
    require(all(reached["reader"]["segments"][index]["feedbackFullyPlayed"] for index in feedback_indices),
            "Continuation occurred without fully playing every correct-feedback PCM segment")
    pcm(Path(next_part["recording"]))
    offset = reached["reader"]["offset"]
    app.wait(lambda value: value["reader"]["index"] > reached["reader"]["index"] or value["reader"]["offset"] > offset + 40,
             "next heading actually plays", 30)
    app.command("pause")
    print(f"PASS actual application Kokoro complete feedback(block{block})→next content{rate}X", flush=True)


def cold_play(app, rate=3):
    # Deliberately issue Play before planning/canonical generation finishes.
    # Waiting for ready() here would conceal the reported streaming failure.
    app.command("preferences", values=dict(rate=float(rate)))
    requested = app.command("play")
    write_json(app.root / "cold-play-request.json", requested)
    require(not requested.get("reader", {}).get("pageAudioComplete", False),
            "Cold playback fixture finished before the actual Play request")
    started = app.wait(lambda value: value.get("reader", {}).get("playing"),
                       "early Play starts real PCM on the cold semantic/C++ page", 120)
    reader = started["reader"]
    write_json(app.root / "cold-play-started.json", started)
    require(not reader["pageAudioComplete"],
            "Play waited for the entire canonical page instead of streaming its ready recording")
    require(reader["segments"][reader["index"]]["available"] and reader["playbackRate"] > 0,
            "Cold playback has no actual decoded recording/media rate")
    pcm(Path(reader["segments"][reader["index"]]["recording"]))
    index, offset = reader["index"], reader["offset"]
    advanced = app.wait(lambda value: value["reader"]["playing"] and
                       (value["reader"]["index"] > index or value["reader"]["offset"] > offset + 120),
                       "cold-page PCM media clock actually advances", 30)
    write_json(app.root / "cold-play-advanced.json", advanced)
    def current(value):
        reader = value.get("reader", {})
        return reader["segments"][reader["index"]] if reader.get("segments") else {}

    caption = app.wait(lambda value: value["reader"]["playing"] and current(value).get("block") == 2,
                       "real passive C++ caption playback before the first exercise", 180)
    require(current(caption)["source"] == "Ejemplo de código C más más.",
            f"Unexpected literal C++ caption:{current(caption).get('source')!r}")
    require(not caption["reader"]["pageAudioComplete"], "Cold caption was postponed until the full page completed")
    pcm(Path(current(caption)["recording"]))
    caption_index, caption_offset = caption["reader"]["index"], caption["reader"]["offset"]
    app.wait(lambda value: value["reader"]["playing"] and value["reader"]["index"] == caption_index and
             value["reader"]["offset"] > caption_offset + 80, "passive C++ caption media clock advances", 30)
    following = app.wait(lambda value: value["reader"]["playing"] and current(value).get("block") == 3,
                         "automatic prose immediately after the passive C++ caption", 180)
    require(current(following)["source"].startswith("Al ejecutar if (pedidos > 0)"),
            "The first authentic prose after C++ was skipped")
    require(not following["reader"]["pageAudioComplete"], "Post-C++ prose waited for the complete cold page")
    pcm(Path(current(following)["recording"]))
    prose_index, prose_offset = following["reader"]["index"], following["reader"]["offset"]
    app.wait(lambda value: value["reader"]["playing"] and value["reader"]["index"] == prose_index and
             value["reader"]["offset"] > prose_offset + 120, "post-C++ prose PCM media clock advances", 30)
    write_json(app.root / "cold-play-caption.json", caption)
    write_json(app.root / "cold-play-after-caption.json", following)
    paused = app.command("pause")["reader"]
    require(not paused["playing"] and not paused["allocated"], "Cold playback ignored the voluntary pause")
    print("PASS actual cold early Play, passive C++ caption and immediate following prose before pageAudioComplete", flush=True)


def acceptance(executable: Path, root: Path, scenario="full", rate=3, voice=None):
    require(voice is None or scenario in ("cold-play", "cold-published"),
            "Explicit voice selection is only supported by cold playback scenarios")
    require(voice is None or voice in ("ef_dora", "em_alex", "em_santa"),
            "Select a Spanish voice from the original Kokoro catalogue")
    if scenario == "cold-published":
        token, relative, location, prefix, copies = published_fixture(root)
        current = None
    else:
        token, current = fixture(root, cold=scenario == "cold-play")
        relative, location = LESSON_RELATIVE, LOCATION
    app = Application(executable, root, token)
    try:
        app.start()
        if scenario in ("cold-play", "cold-published"):
            app.command("preferences", values={"kokoro_voice:es": voice or "ef_dora"})
        app.command("open", path=str(relative), location=location)
        if scenario == "cold-published":
            # Resolve only the preceding page by its genuine native radio and
            # check control. No state is fabricated and page two stays cold.
            app.command("answer_choice", block=prefix[0], option=prefix[1]["correctIndex"])
            app.command("page", page=1)
            cold_play(app, rate)
            return
        if scenario == "cold-play":
            cold_play(app, rate)
            return
        app.command("page", page=1)
        app.ready()
        # Baseline lesson is generated completely before changing any audio.
        app.command("preferences", values=dict(kokoro_advance_generation=True))
        app.wait(lambda value: value["generation"]["finished"] and value["generation"]["waiting"],
                 "complete baseline lesson audio", 900)
        baseline = manifests(root)
        require(len(baseline) >= 32, "The full baseline lesson has no committed page audio")
        old_recordings = recording_seal(baseline, root)
        write_json(root / "baseline-manifests.json", baseline)
        for rate in (1, 2, 3):
            playback(app, rate, 1, HEADING)
        preserved_state = app.inspect()
        # Put the already-ahead producer back on page30, then modify2/3/5/8.
        current = revise_prose(current, [29], "La generación anticipada recorre este texto mientras se publica una revisión. " * 12)
        write_json(root / "content" / LESSON_RELATIVE, current)
        app.wait(lambda value: value["generation"]["running"] and "/29|" in value["generation"]["page"],
                 "actual two-core producer working on page30", 180)
        current = revise_prose(current, [1, 2, 4, 7], "Contenido nuevo de esta página.")
        current = add_puzzles(current)
        # Prepare outside the watched curriculum. Creation/truncation of an
        # in-tree .pending file is not the actual publication being tested.
        staged = root / "publication-stage/live-pages.json"
        write_json(staged, current)
        before_revision = app.snapshot()["lesson"]["contentRevision"]
        publication = app.snapshot()
        require(publication["generation"]["running"] and "/29|" in publication["generation"]["page"],
                "The old page30 job ended before the curriculum publication precondition")
        replace_owned_file(staged, root / "content" / LESSON_RELATIVE)
        app.wait(lambda value: value["lesson"]["contentRevision"] > before_revision, "automatic live curriculum adoption", 60)
        revised_reader = app.ready()["reader"]
        new_instruction = next(index for index, segment in enumerate(revised_reader["segments"])
                               if segment["source"] == "Escribe ok en nuevo anterior.")
        require(revised_reader["heardThrough"] <= new_instruction,
                "New exercise was incorrectly marked already heard after publication")
        app.wait(lambda value: value["generation"]["finished"] and value["generation"]["waiting"],
                 "revised foreground/background page inventory", 900)
        revised = manifests(root)
        for index in (1, 2, 4, 7):
            before, after = page_manifest(baseline, index), page_manifest(revised, index)
            require(before["value"]["revision"] != after["value"]["revision"], "Changed page kept old revision")
            old_segments, new_segments = before["value"]["segments"], after["value"]["segments"]
            old_keys = {segment["key"] for segment in old_segments}
            require(old_keys.isdisjoint(segment["key"] for segment in new_segments), "A changed page mixed old and new recording generations")
            for segment in new_segments:
                path = (root / "profile/cache/kokoro" / segment["wav"]).resolve()
                require(path.is_relative_to(root), "A page recording escaped QA storage")
                pcm(path)
                require(digest(path) == segment["audioSha256"], "Manifest references a mismatched recording")
        require(page_manifest(baseline, 3) == page_manifest(revised, 3), "An unchanged page audio manifest was replaced")
        for file, sealed in old_recordings.items():
            path = Path(file)
            require(path.is_file() and digest(path) == sealed["sha256"] and path.stat().st_mtime_ns == sealed["modified"],
                    "Revision replaced an existing recording instead of preserving its generation")
        order = recorded_generation_order(root, publication)
        write_json(root / "generation-order.json", order)
        verify_generation_order(order, publication)
        verify_cpu_configuration(root)
        write_json(root / "revised-manifests.json", revised)
        # Earn each new gate by actual complete feedback playback, in the
        # authored before→saldo→after→heading order at every required speed.
        for rate in (1, 2, 3):
            playback(app, rate, 1, answer="ok", next_block=2)
            playback(app, rate, 2, next_block=3)
            playback(app, rate, 3, HEADING, answer="ok")
        before_close = app.inspect()
        paused = app.snapshot()["reader"]
        before_restart_manifests = manifests(root)
        app.close()
        current = revise_prose(current, [2, 4], "Una publicación mientras la app está cerrada.")
        write_json(root / "content" / LESSON_RELATIVE, current)
        publish_new_lesson(root)
        app.start()
        app.ready()
        app.wait(lambda value: value["generation"]["finished"] and value["generation"]["waiting"],
                 "changed pages and a newly published page detected after ordinary restart", 900)
        after_restart_manifests = manifests(root)
        for index in (2, 4):
            require(page_manifest(before_restart_manifests, index)["value"]["revision"] !=
                    page_manifest(after_restart_manifests, index)["value"]["revision"],
                    "A page changed while the app was closed retained its old audio revision")
        require(page_manifest(before_restart_manifests, 1) == page_manifest(after_restart_manifests, 1),
                "Restart replaced an unchanged paused page's audio generation")
        new_pages = [row for key, row in after_restart_manifests.items() if key.startswith(NEW_LESSON + "/es/")]
        require(len(new_pages) == 1 and new_pages[0]["value"]["segments"], "A page published while the app was closed was not automatically generated")
        for segment in new_pages[0]["value"]["segments"]:
            path = (root / "profile/cache/kokoro" / segment["wav"]).resolve()
            require(path.is_relative_to(root) and digest(path) == segment["audioSha256"], "New page manifest lost its own real PCM generation")
            pcm(path)
        write_json(root / "restart-manifests.json", after_restart_manifests)
        after_open = app.inspect()
        require(before_close["audio_revision_qa"] == after_open["audio_revision_qa"], "Restart changed QA preferences")
        require(before_close["lesson_narration_settings"] == after_open["lesson_narration_settings"], "Restart changed narration preferences")
        for key, value in before_close.get("learning_progress", {}).items():
            if key.startswith("exercise_") or key.endswith("_page") or key.endswith("_started"):
                require(after_open.get("learning_progress", {}).get(key) == value, f"Restart lost valid answer/progress{key}")
        for key, value in preserved_state.get("learning_progress", {}).items():
            if key.startswith("exercise_"):
                require(after_open.get("learning_progress", {}).get(key) == value, "Inserting puzzles lost an earlier correct answer")
        require(app.snapshot()["reader"]["rate"] == 3, "Restart lost the chosen speed")
        require(not app.snapshot()["reader"]["playing"], "Restart ignored the voluntary pause")
        require(app.snapshot()["reader"]["key"] == paused["key"], "An unchanged paused page lost its valid bookmark key")
        require(abs(app.snapshot()["reader"]["offset"] - paused["offset"]) <= 1, "Restart lost exact valid paused PCM position")
        print(f"PASS application revision acceptance; raw QA evidence={root}", flush=True)
    finally:
        try:
            app.close()
        finally:
            if scenario == "cold-published":
                verify_published_copies(copies)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--qa-root", type=Path, default=Path("C:/LGAED-QA") / ("app-audio-revision-" + uuid.uuid4().hex))
    parser.add_argument("--scenario", choices=("full", "cold-play", "cold-published"), default="full")
    parser.add_argument("--rate", type=int, choices=(1, 2, 3), default=3,
                        help="Actual playback rate for the cold streaming scenarios")
    parser.add_argument("--voice", choices=("ef_dora", "em_alex", "em_santa"),
                        help="Spanish Kokoro voice for cold scenarios only (default: ef_dora)")
    arguments = parser.parse_args()
    if arguments.voice is not None and arguments.scenario == "full":
        parser.error("--voice is only supported with --scenario cold-play or cold-published")
    acceptance(arguments.executable, checked_root(arguments.qa_root), arguments.scenario, arguments.rate, arguments.voice)

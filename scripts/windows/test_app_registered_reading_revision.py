"""Adopt two complete registered versions of the tarifas lesson in the Windows app."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import uuid

import test_app_audio_revision as qa


EXERCISE = "u3l4_tarifa_declaraciones_y_vida"
HEADING = "u3l4_opt_tarifas_pregunta"


def locate(document, identity):
    for page, layout in enumerate(document["stateLayout"]["current"]):
        if identity in layout["blocks"]:
            index = layout["blocks"].index(identity)
            return page, index, document["pages"][page]["blocks"][index]
    raise AssertionError(f"Missing registered block {identity}")


def install(source, destination, root):
    staged = root / "registered-publication.json"
    staged.write_bytes(source.read_bytes())
    qa.replace_owned_file(staged, destination)
    qa.require(qa.digest(source) == qa.digest(destination), "Publication changed lesson bytes")


def submit_prerequisite(app, document):
    _, index, compiler = locate(document, "u3l4_alcance_editor")
    _, _, solution = locate(document, "u3l4_alcance_solucion")
    qa.require(compiler["type"] == "cpp_compiler" and solution["type"] == "code",
               "The registered compiler prerequisite or published solution changed")
    # Send ordinary edit/compile actions exclusively to the hidden QA process.
    # System messages below WM_USER marshal their text argument across processes.
    api = ctypes.WinDLL("user32", use_last_error=True)
    visitor = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    api.EnumWindows.argtypes = (visitor, wintypes.LPARAM)
    api.EnumChildWindows.argtypes = (wintypes.HWND, visitor, wintypes.LPARAM)
    api.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
    api.GetDlgCtrlID.argtypes = (wintypes.HWND,)
    api.SendMessageW.argtypes = (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
    api.SendMessageW.restype = wintypes.LPARAM
    source_id, check_id = 1010 + 100 * index, 1011 + 100 * index
    controls = {}

    @visitor
    def child(hwnd, unused):
        control_id = api.GetDlgCtrlID(hwnd)
        if control_id in (source_id, check_id):
            controls[control_id] = hwnd
        return True

    @visitor
    def top(hwnd, unused):
        pid = wintypes.DWORD()
        api.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == app.process.pid:
            api.EnumChildWindows(hwnd, child, 0)
        return True

    api.EnumWindows(top, 0)
    qa.require(set(controls) == {source_id, check_id}, "Missing native prerequisite editor or compile button")
    source = ctypes.create_unicode_buffer(solution["code"].replace("\n", "\r\n"))
    api.SendMessageW(controls[source_id], 0x00B1, 0, -1)  # EM_SETSEL
    api.SendMessageW(controls[source_id], 0x00C2, 1, ctypes.addressof(source))  # EM_REPLACESEL
    api.SendMessageW(controls[check_id], 0x00F5, 0, 0)  # BM_CLICK
    _, question, _ = locate(document, EXERCISE)
    app.wait(lambda s: any(p["block"] == question for p in s.get("reader", {}).get("segments", [])),
             "genuine compiler verification opens the published tariffs exercise", 150)


def observe(app, document, *, expect_heading):
    page, block, exercise = locate(document, EXERCISE)
    page_id = document["stateLayout"]["current"][page]["id"]
    heading = locate(document, HEADING) if expect_heading else None
    if heading:
        qa.require(heading[:2] == (page, block + 1) and heading[2]["type"] == "heading",
                   "The registered title must immediately follow the exercise")
    value = app.wait(lambda s: s.get("lesson", {}).get("pageId") == page_id
                     and s.get("reader", {}).get("planned")
                     and any(part["block"] == block and part["feedback"] and part["correct"]
                             for part in s["reader"]["segments"]),
                     "registered curriculum and the genuinely checked response enter the reading plan", 120)
    parts = value["reader"]["segments"]
    qa.require([p["block"] for p in parts] == sorted(p["block"] for p in parts),
               "The active reader reordered registered blocks")
    feedback = [i for i, p in enumerate(parts) if p["block"] == block and p["feedback"] and p["correct"]]
    qa.require(feedback == list(range(feedback[0], feedback[-1] + 1)), "Feedback was interrupted")
    qa.require(not value["reader"]["playing"] and not any(parts[i]["feedbackFullyPlayed"] for i in feedback),
               "A publication or a restored answer credited feedback the paused learner never heard")
    # The verdict may be generated; every authored word from every paragraph must follow it, in order.
    spoken_source = " ".join(" ".join(parts[i]["source"] for i in feedback).split())
    explanation = " ".join(exercise["explanation"].split())
    qa.require(spoken_source.endswith(explanation), "The plan omitted or reordered authored feedback")
    if heading:
        following = parts[feedback[-1] + 1:feedback[-1] + 2]
        qa.require(following and following[0]["block"] == heading[1]
                   and following[0]["source"] == heading[2]["text"] and not following[0]["feedback"],
                   "The updated exercise does not lead directly to its new title")
    else:
        qa.require(feedback[-1] == len(parts) - 1, "The previous revision did not end at this feedback")
    return value


def acceptance(executable, previous, revised, root):
    old, new = qa.read_json(previous), qa.read_json(revised)
    for document in (old, new):
        qa.layout.validate(document)
    qa.require(old["id"] == new["id"] and old["courseEdition"] == new["courseEdition"]
               and old["stateLayout"]["family"] == new["stateLayout"]["family"],
               "Expected a publication in the same registered course edition and layout family")
    old_page, old_block, exercise = locate(old, EXERCISE)
    qa.require(old_page == 1 and old_block + 1 == len(old["pages"][old_page]["blocks"])
               and not any(HEADING in row["blocks"] for row in old["stateLayout"]["current"]),
               "The previous registered publication must precede the new tariffs section")
    token, relative, location, _, copies = qa.published_fixture(root)
    lesson_path = root / "content" / relative
    qa.require(qa.read_json(lesson_path)["id"] == old["id"], "The copies are not the registered lesson")
    install(previous, lesson_path, root)
    app = qa.Application(executable, root, token)
    try:
        app.start()
        app.command("open", path=str(relative), location=location)
        # Open page two through the authored question and native check button on page one.
        prefix = [(i, b) for i, b in enumerate(old["pages"][0]["blocks"]) if b["type"] == "multiple_choice"]
        qa.require(len(prefix) == 1, "The preceding required exercise changed")
        app.command("answer_choice", block=prefix[0][0], option=prefix[0][1]["correctIndex"])
        app.command("page", page=old_page)
        mounted = app.wait(lambda s: s.get("lesson", {}).get("index") == old_page
                           and s.get("reader", {}).get("planned"),
                           "mounted registered page with its published exercise", 120)
        qa.write_json(root / "registered-mounted.json", mounted)
        app.command("pause")
        submit_prerequisite(app, old)
        app.command("answer_choice", block=old_block, option=exercise["correctIndex"])
        before = observe(app, old, expect_heading=False)
        qa.write_json(root / "registered-before.json", before)
        install(revised, lesson_path, root)
        app.wait(lambda s: s.get("lesson", {}).get("contentRevision", 0) > before["lesson"]["contentRevision"],
                 "automatic adoption while the real page is open", 60)
        live = observe(app, new, expect_heading=True)
        qa.write_json(root / "registered-live.json", live)
        app.close()
        app.start()
        restored = observe(app, new, expect_heading=True)
        qa.write_json(root / "registered-restored.json", restored)
        qa.require(all(live["reader"][field] == restored["reader"][field] for field in ("key", "index", "offset")),
                   "Reopening the adopted publication changed the paused reading position")
        print(f"PASS registered lesson revision, authored feedback, new heading, live adoption and restart: {root}",
              flush=True)
    finally:
        try:
            app.close()
        finally:
            qa.verify_published_copies([copy for copy in copies if copy[1] != lesson_path])
            qa.require(qa.digest(lesson_path) in (qa.digest(previous), qa.digest(revised)),
                       "The application modified an authored publication")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--previous", type=Path, required=True, help="Original lesson JSON before the heading was added")
    parser.add_argument("--revised", type=Path, required=True, help="Original lesson JSON with the new heading")
    parser.add_argument("--qa-root", type=Path, default=Path("C:/LGAED-QA") / ("registered-reader-" + uuid.uuid4().hex))
    args = parser.parse_args()
    acceptance(args.executable, args.previous, args.revised, qa.checked_root(args.qa_root))

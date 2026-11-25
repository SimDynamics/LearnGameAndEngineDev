"""Bounded Win32 QA snapshot transport probes; no application or audio runs."""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import runpy
import threading
import time
import uuid


def check(value, message):
    if not value:
        raise AssertionError(message)


def main(root):
    check(os.name == "nt", "This probe requires actual Windows")
    root = root.resolve()
    check(root.is_relative_to(Path("C:/LGAED-QA").resolve()) and not root.exists(), "Use a fresh isolated QA child")
    root.mkdir(parents=True)
    driver = runpy.run_path(str(Path(__file__).with_name("test_app_audio_revision.py")))
    kernel = driver["_kernel32"]
    kernel.MoveFileExW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD]
    kernel.MoveFileExW.restype = wintypes.BOOL
    kernel.ReplaceFileW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                   wintypes.DWORD, wintypes.LPVOID, wintypes.LPVOID]
    kernel.ReplaceFileW.restype = wintypes.BOOL
    kernel.GetFileAttributesW.argtypes = [wintypes.LPCWSTR]
    kernel.GetFileAttributesW.restype = wintypes.DWORD
    records = []

    def owned(path):
        check(path.resolve().is_relative_to(root), "Probe path escaped its owned QA tree")
        return str(path)

    def opened(path):
        handle = kernel.CreateFileW(owned(path), 0x80000000, 7, None, 3, 0x80, None)
        check(handle != ctypes.c_void_p(-1).value, f"Cannot open probe reader:{ctypes.get_last_error()}")
        return handle

    def bounded_publish(source, target):
        started = time.monotonic()
        failures, operations = [], []
        while True:
            attributes = kernel.GetFileAttributesW(owned(target))
            existing = attributes != 0xFFFFFFFF
            attribute_failure = 0 if existing else ctypes.get_last_error()
            if not existing and attribute_failure not in (2, 3):
                raise ctypes.WinError(attribute_failure)
            operation = "ReplaceFileW" if existing else "MoveFileExW"
            operations.append(operation)
            published = (kernel.ReplaceFileW(owned(target), owned(source), None, 0, None, None) if existing else
                         kernel.MoveFileExW(owned(source), owned(target), 8))
            if published:
                return dict(ok=True, operations=operations, failures=failures, elapsedMs=round((time.monotonic() - started) * 1000, 3))
            failure = ctypes.get_last_error()
            failures.append(failure)
            if failure not in (5, 32) or time.monotonic() - started >= .25:
                return dict(ok=False, operations=operations, failures=failures, elapsedMs=round((time.monotonic() - started) * 1000, 3))
            time.sleep(.005)

    # Match the actual controller: first creation is a move; every subsequent
    # publication is ReplaceFileW with flags=0, including an open shared reader.
    directory = root / "exact-controller-strategy"
    directory.mkdir()
    target, source = directory / "snapshot.json", directory / "snapshot.pending"
    source.write_text('{"version":0}', encoding="utf-8")
    initial = bounded_publish(source, target)
    check(initial["ok"] and initial["operations"] == ["MoveFileExW"], "Initial creation did not use the controller strategy")
    records.append(dict(probe="initialCreation", **initial))
    source.write_text('{"version":1}', encoding="utf-8")
    handle = opened(target)
    try:
        replaced = bounded_publish(source, target)
    finally:
        kernel.CloseHandle(handle)
    check(replaced["ok"] and replaced["operations"] == ["ReplaceFileW"] and not replaced["failures"],
          f"The exact replacement strategy did not work with an open share-delete reader:{replaced}")
    check(json.loads(target.read_text(encoding="utf-8"))["version"] == 1, "Replacement lost its JSON")
    records.append(dict(probe="openShareDeleteReader", **replaced))

    # A deliberately unsupported destination kind supplies a permanent error
    # in this owned QA tree. This checks the deadline and retained payload;
    # it does not claim to reproduce the real application's transient cause.
    directory = root / "permanent-destination-error"
    directory.mkdir()
    target, source = directory / "snapshot.json", directory / "snapshot.pending"
    target.mkdir()
    source.write_text('{"version":1}', encoding="utf-8")
    result = bounded_publish(source, target)
    check(not result["ok"] and result["elapsedMs"] <= 1000 and source.exists(),
          f"Permanent denial was hidden, retried forever or lost its payload:{result}")
    if result["failures"][-1] in (5, 32):
        check(result["elapsedMs"] >= 250, "The retryable permanent error did not retain its fixed deadline")
    records.append(dict(probe="permanentUnsupportedDestination", **result))

    # Exercise the real driver's close-before-parse reader against replacement.
    directory = root / "concurrent-driver-reader"
    directory.mkdir()
    target, source = directory / "snapshot.json", directory / "snapshot.pending"
    target.write_text(json.dumps(dict(version=0, payload="v" * 16000)), encoding="utf-8")
    stop = threading.Event()
    errors, observations, read_retries = [], [], {}

    def reader():
        while not stop.is_set():
            try:
                value = driver["read_json"](target)
                check(value["payload"] == "v" * 16000, "Concurrent reader saw a torn snapshot")
                observations.append(value["version"])
            except OSError as failure:
                code = getattr(failure, "winerror", None)
                if code in (2, 3, 5, 32):
                    # Application.snapshot already waits after these failed
                    # reads. A namespace replacement need not leave the name
                    # openable during every instant of the operation.
                    read_retries[code] = read_retries.get(code, 0) + 1
                else:
                    errors.append(str(failure))
                    stop.set()
            except Exception as failure:
                errors.append(str(failure))
                stop.set()

    thread = threading.Thread(target=reader)
    thread.start()
    attempts = []
    try:
        for version in range(1, 201):
            source.write_text(json.dumps(dict(version=version, payload="v" * 16000)), encoding="utf-8")
            result = bounded_publish(source, target)
            check(result["ok"], f"Concurrent publication exceeded its bounded retry:{result}")
            check(set(result["operations"]) == {"ReplaceFileW"}, "An existing snapshot bypassed the replacement strategy")
            attempts.extend(result["failures"])
    finally:
        stop.set()
        thread.join(timeout=2)
    check(not thread.is_alive() and not errors and observations, f"Concurrent reader failed:{errors}")
    check(driver["read_json"](target)["version"] == 200, "Final published snapshot is incorrect")
    records.append(dict(probe="concurrentRealDriver", publications=200, reads=len(observations),
                        retryWin32=attempts, readRetryWin32=read_retries, finalVersion=200))
    (root / "results.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(status="PASS", qa=str(root), probes=records)), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qa-root", type=Path, default=Path("C:/LGAED-QA") / ("app-audio-transport-" + uuid.uuid4().hex))
    main(parser.parse_args().qa_root)

"""Controlled JSONL transport fixture; no Kokoro, audio or performance simulation.

The original mathematical response stays withheld until the test explicitly
releases it. This proves cancellation/wakeup and late-response ID routing
without relying on a slow model or artificial synthesis timings.
"""
from __future__ import annotations

import json
import sys
import ctypes

sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8")
delayed = None
cancel_observed = False


def respond(request, **fields):
    print(json.dumps({"id": request["id"], "command": request["command"], **fields}), flush=True)


for line in sys.stdin:
    request = json.loads(line)
    command = request["command"]
    if command == "math_calculate":
        if delayed is not None:
            raise RuntimeError("The fixture expects one explicitly controlled calculation")
        delayed = request
    elif command == "prepare":
        if delayed is not None or not request.get("text", "").startswith("cpu-priority-event:"):
            raise RuntimeError("Unexpected controlled background preparation")
        delayed = request
        name = request["text"].removeprefix("cpu-priority-event:")
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenEventW.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_wchar_p]
        kernel.OpenEventW.restype = ctypes.c_void_p
        kernel.SetEvent.argtypes = [ctypes.c_void_p]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        event = kernel.OpenEventW(2, False, name)
        if not event or not kernel.SetEvent(event):
            raise ctypes.WinError(ctypes.get_last_error())
        kernel.CloseHandle(event)
    elif command == "cancel":
        cancel_observed = delayed is not None and request.get("targetId", request["id"]) == delayed["id"]
        if cancel_observed and delayed["command"] == "prepare":
            # The original inference reply is the completion acknowledgement;
            # a mere receipt of the cancel command must not release its lease.
            respond(delayed, error="cancelled-background-preparation")
            delayed = None
        respond(request, cancelled=cancel_observed)
    elif command == "math_editor":
        action = request.get("action")
        if action == "probe":
            respond(request, observed=delayed is not None, cancelled=cancel_observed,
                    pendingCommand=delayed["command"] if delayed is not None else "")
        elif action == "release":
            if delayed is None:
                raise RuntimeError("The delayed request was not received")
            respond(delayed, marker="late-cancelled-result")
            delayed = None
            respond(request, released=True)
        elif action == "echo":
            respond(request, marker=request["marker"])
        else:
            raise RuntimeError("Unexpected fixture action")
    elif command == "curriculum_plan":
        respond(request, segments=[])
    elif command == "shutdown":
        respond(request, shutdown=True)
        break
    else:
        raise RuntimeError("Unexpected fixture command")

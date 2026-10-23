"""Controlled JSONL transport fixture; no Kokoro, audio or performance simulation.

The original mathematical response stays withheld until the test explicitly
releases it. This proves cancellation/wakeup and late-response ID routing
without relying on a slow model or artificial synthesis timings.
"""
from __future__ import annotations

import json
import sys

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
    elif command == "cancel":
        cancel_observed = delayed is not None and request.get("targetId", request["id"]) == delayed["id"]
        respond(request, cancelled=cancel_observed)
    elif command == "math_editor":
        action = request.get("action")
        if action == "probe":
            respond(request, observed=delayed is not None, cancelled=cancel_observed)
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
    elif command == "shutdown":
        respond(request, shutdown=True)
        break
    else:
        raise RuntimeError("Unexpected fixture command")

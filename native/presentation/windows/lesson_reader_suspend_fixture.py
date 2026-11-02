"""Controlled reader transport; its generated PCM is never neural-audio evidence.

The first preparation returns a stable test WAV. The next stays in flight after
cancel until this test's acknowledgement file appears. No models or network.
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import threading
import time
import wave

log_path, cache_path, acknowledge_path = map(Path, sys.argv[1:])
cache_path.mkdir(exist_ok=True)
sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8")
log = log_path.open("x", encoding="utf-8", buffering=1)
output_lock = threading.Lock()
state_lock = threading.Lock()
pending = None
cancelled = set()
held = False
prepared = 0


def record(value):
    with output_lock:
        log.write(json.dumps(value, ensure_ascii=False) + "\n")


def respond(request, **fields):
    with output_lock:
        print(json.dumps({"id": request["id"], "command": request["command"], **fields}), flush=True)


def acknowledge():
    global pending
    while True:
        time.sleep(0.005)
        with state_lock:
            request = pending if pending and pending["id"] in cancelled and acknowledge_path.is_file() else None
            if request:
                pending = None
        if request:
            record({"command": "acknowledged", "targetId": request["id"]})
            respond(request, error="controlled preparation cancellation acknowledged")


threading.Thread(target=acknowledge, daemon=True).start()
for line in sys.stdin:
    request = json.loads(line)
    command = request["command"]
    record(request)
    if command == "plan":
        text = request["text"]
        length = len(text.encode("utf-16-le")) // 2
        respond(request, segments=[{"text": text, "preparedText": text, "spokenText": text,
                                    "sourceStart": 0, "sourceEnd": length}])
    elif command == "identity":
        identity = {key: value for key, value in request.items() if key not in {"id", "command"}}
        respond(request, key=hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest())
    elif command == "prepare":
        with state_lock:
            if prepared and not held:
                pending = request
                held = True
                hold = True
            else:
                prepared += 1
                hold = False
        if hold:
            record({"command": "prepare_blocked", "targetId": request["id"]})
            continue
        text = request["text"]
        key = hashlib.sha256((text + request["language"] + request["voice"]).encode()).hexdigest()
        wav = cache_path / (key + ".wav")
        hit = wav.exists()
        if not hit:
            with wave.open(str(wav), "wb") as output:
                output.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                output.writeframes(b"".join(struct.pack("<h", int(3000 * math.sin(index * math.tau * 220 / 24000)))
                                           for index in range(72000)))
        respond(request, key=key, sourceText=text, spokenText=text, provider="controlled-reader-fixture",
                wav=str(wav), durationMs=3000, cacheHit=hit, timings=[])
    elif command == "cancel":
        with state_lock:
            cancelled.add(request["id"])
    elif command == "curriculum_plan":
        respond(request, marker="background-after-ack", segments=[])
    elif command == "shutdown":
        break
    else:
        raise RuntimeError(f"Unexpected controlled reader command: {command}")

"""Controlled coordinator transport, not speech synthesis or a CPU benchmark.

Every observed preparation stays in flight until cancellation. The real native
coordinator, arbitration, worker process and catalog are exercised; no fabricated
audio is reported as installed or played.
"""
import json
import sys
import hashlib
import wave

sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8")
log = open(sys.argv[1], "w", encoding="utf-8", buffering=1)
pending = None
cached_wave = sys.argv[2] if len(sys.argv) > 2 else None
cached_source = None


def respond(request, **fields):
    print(json.dumps({"id": request["id"], "command": request["command"], **fields}), flush=True)


for line in sys.stdin:
    request = json.loads(line)
    command = request["command"]
    if command == "curriculum_plan":
        lesson = json.loads(request["lessonJson"])
        source = f"generation:{lesson['id']}:{request['page']}"
        log.write(json.dumps({"command": command, "text": source, "language": request["language"], "voice": request["voice"]}) + "\n")
        respond(request, segments=[{"text": source, "preparedText": source, "key": "controlled-transport-only"}])
    elif command == "prepare":
        if pending is not None:
            raise RuntimeError("Overlapping synthesis escaped the native serialized lane")
        log.write(json.dumps(request) + "\n")
        if cached_wave is not None and (cached_source is None or cached_source == request["text"]):
            # A genuine pre-existing Kokoro WAV is borrowed by this controlled
            # transport. This metadata is explicitly a fixture; no new audio is
            # synthesized, and it is never evidence of neural pronunciation.
            cached_source = request["text"]
            with open(cached_wave, "rb") as stream:
                key = hashlib.sha256(stream.read()).hexdigest()
            with wave.open(cached_wave, "rb") as stream:
                duration = stream.getnframes() * 1000.0 / stream.getframerate()
            respond(request, key=key, sourceText=request["text"], spokenText=request["text"],
                    provider="controlled-cache-fixture", wav=cached_wave,
                    durationMs=duration, cacheHit=True, timings=[])
        else:
            pending = request
    elif command == "cancel":
        log.write(json.dumps(request) + "\n")
        if pending is not None and request.get("targetId", request["id"]) == pending["id"]:
            respond(pending, error="controlled-cancelled-preparation")
            pending = None
        respond(request, cancelled=True)
    elif command == "shutdown":
        respond(request, shutdown=True)
        break
    else:
        raise RuntimeError(f"Unexpected coordinator fixture command: {command}")

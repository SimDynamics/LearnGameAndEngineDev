"""Read-only first-unit lesson identities and the historical PCM contract.

Used by the Windows worker export without importing Android ADB/package code.
The WAV checks and expected lesson identifiers preserve the original format.
"""
from __future__ import annotations

import io
import wave

LESSONS = {
    "cpp_fundamentals__intro_programming__what_is_programming",
    "cpp_fundamentals__intro_programming__first_contact_cpp",
    "cpp_fundamentals__intro_programming__program_structure",
}


def inspect_wav(data: bytes) -> str | None:
    """Match the exact first-unit RIFF/PCM16/24k mono format."""
    if len(data) < 44:
        return f"Only {len(data)} bytes received; expected at least 44"
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        excerpt = repr(data[:72])
        return f"Not a RIFF/WAVE stream; first bytes: {excerpt}"
    riff_length = int.from_bytes(data[4:8], "little") + 8
    if riff_length != len(data):
        return f"Truncated/inconsistent RIFF: expected {riff_length} bytes; got {len(data)}"
    if data[12:16] != b"fmt " or data[36:40] != b"data":
        return f"Unexpected WAV chunk layout (first 44 bytes: {data[:44].hex()})"
    audio_length = int.from_bytes(data[40:44], "little")
    if audio_length <= 0 or 44 + audio_length != len(data):
        return f"PCM data length {audio_length}; transferred {len(data)} bytes"
    try:
        with wave.open(io.BytesIO(data), "rb") as source:
            if (source.getnchannels(), source.getframerate(), source.getsampwidth()) != (1, 24000, 2):
                return (
                    "Unexpected audio format: channels="
                    f"{source.getnchannels()}, rate={source.getframerate()}, "
                    f"sample_width={source.getsampwidth()}"
                )
            if source.getnframes() == 0:
                return "No PCM frames"
    except (OSError, EOFError, wave.Error) as error:
        return f"Python WAV decoder failed: {error}"
    return None

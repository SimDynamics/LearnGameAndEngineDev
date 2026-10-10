#!/usr/bin/env python3
"""Pure-Python validation tests. No device, model download or ffmpeg needed."""
from __future__ import annotations

import io
import unittest
import wave

from export_first_unit_from_android import inspect_wav, missing_file_reply


def pcm24khz() -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(24_000)
        wav.writeframes(b"\x00\x00" * 120)
    return output.getvalue()


class AndroidWavExportTest(unittest.TestCase):
    def test_valid_phone_format(self) -> None:
        self.assertIsNone(inspect_wav(pcm24khz()))

    def test_partial_transfer_is_reported(self) -> None:
        recorded = pcm24khz()
        error = inspect_wav(recorded[:-12])
        self.assertIsNotNone(error)
        self.assertIn("RIFF", error)

    def test_remote_shell_text_is_not_treated_as_audio(self) -> None:
        error = inspect_wav(b"run-as: package not debuggable\n" * 4)
        self.assertIsNotNone(error)
        self.assertIn("Not a RIFF", error)

    def test_empty_file_reports_size(self) -> None:
        self.assertIn("0 bytes", inspect_wav(b""))

    def test_nonzero_size_cat_error_with_zero_exit_code_means_missing_audio(self) -> None:
        # Exact class of output encountered on the Honor X8a.
        message = (
            b"cat: files/kokoro-narration-cache/" + b"a" * 64 +
            b".wav: No such file or directory\\n"
        )
        self.assertTrue(missing_file_reply(message))
        self.assertIsNotNone(inspect_wav(message))
        self.assertFalse(missing_file_reply(pcm24khz()))

    def test_other_cat_error_is_not_misclassified_as_missing(self) -> None:
        self.assertFalse(missing_file_reply(
            b"cat: files/kokoro-narration-cache/item.wav: Permission denied"
        ))

    def test_wrong_sample_rate_is_rejected(self) -> None:
        data = bytearray(pcm24khz())
        data[24:28] = (48_000).to_bytes(4, "little")
        self.assertIn("rate=48000", inspect_wav(bytes(data)))


if __name__ == "__main__":
    unittest.main()

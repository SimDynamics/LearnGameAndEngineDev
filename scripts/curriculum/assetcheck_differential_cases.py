"""Deterministic extra stdin/stdout cases for the four curricular AssetCheck editors.

The inputs are generated from the stated exercise domains. Expected output is
computed by independent Python oracles, not copied from the C++ solutions.
These cases never substitute for the separate AssetCheck project or its QA.
"""

from __future__ import annotations

import random

from test_assetcheck_curricular_oracles import (
    EDITOR_ORACLES,
    count_physical_bytes,
)

SEED = 0xA55E7C
CASES_PER_LESSON = 24

SIZE_TOKENS = (
    "0", "1", "00001", "00000", "00009", "30000", "29999",
    "30001", "030000", "000001", "99999", "1x", "+5", "-1",
    "12", "0012", "3.5", "0x10", "1e2", "AB", "a_b",
    "12345", "123456", "999999999999999999", "_", "01",
)

BATCH_LINES = (
    "A malla 1",
    "A sonido 4",
    "A textura 0",
    "A malla 30001",
    "A_B textura 30000",
    "a_b malla 00001",
    "A  malla\t9",
    "1id malla 2",
    "_id malla 1",
    "B" * 12 + " textura 2",
    "B" * 13 + " textura 2",
    "C tipo 3",
    "C malla 30001",
    "C malla 99999",
    "C malla 1a",
    "C malla 5 extra",
    "C malla",
    "C malla 000000",
    "C malla +4",
    "D malla 1" + " " * 71,  # Exactly 80 bytes: admitted.
    "D malla 1" + " " * 72,  # 81 bytes: FORMAT.
    "E malla 2" + " " * 80,
    "",
    "\t",
    "C\tmalla\t3",
    "B sonido 4",
    "E sonido 5\x00",
    "F sonido 7",
    "F sonido 8",
)

FILE_SCENARIOS = (
    "VACIO", "LF", "CRLF", "ULTIMA", "BLANCO",
    "CRSUELTO", "FINALCR", "CIEN", "CIENTOUNO",
)

STATS_TYPES = ("textura", "malla", "sonido")
STATS_SIZES = (1, 2, 7, 12, 999, 29999, 30000)


def differential_inputs(filename: str) -> tuple[str, ...]:
    """Generate enough variation to expose regressions across input streams.

    Deliberately exclude CR from streamed U4L5 input because Windows CRT text
    mode may translate CRLF. U4L6 checks CR as bytes in an in-memory string.
    """
    if filename not in EDITOR_ORACLES:
        raise ValueError(f"No oracle for {filename}")
    lesson_index = list(EDITOR_ORACLES).index(filename)
    rng = random.Random(SEED + lesson_index * 8191)
    cases: list[str] = []
    for index in range(CASES_PER_LESSON):
        if lesson_index == 0:  # U4L4: sizes and textual spelling.
            count = 5 + rng.randrange(35)
            tokens = [rng.choice(SIZE_TOKENS) for _ in range(count)]
            if index % 4 == 0:
                tokens[:5] = ["00001", "30000", "30001", "+1", "000000"]
            cases.append(" \t".join(tokens) + "\n")
        elif lesson_index == 1:  # U4L5: local rejection and first admission.
            count = 7 + rng.randrange(25)
            lines = [rng.choice(BATCH_LINES) for _ in range(count)]
            if index % 4 == 0:
                lines[:3] = ["A malla 0", "A malla 2", "A sonido 3"]
            if index % 4 == 1:
                lines[:3] = ["F sonido 7", "F sonido 8", "F sonido 9"]
            cases.append("\n".join(lines) + ("\n" if index % 3 else ""))
        elif lesson_index == 2:  # U4L6: named in-memory physical-line cases.
            count = 8 + rng.randrange(25)
            names = [rng.choice(FILE_SCENARIOS) for _ in range(count)]
            if index % 3 == 0:
                names[:3] = ["VACIO", "FINALCR", "CIENTOUNO"]
            cases.append(" \t".join(names) + "\n")
        else:  # U4L7: independent totals for multiple admitted groups.
            group_count = 1 + rng.randrange(7)
            tokens = [str(group_count)]
            for group in range(group_count):
                record_count = 0 if (index + group) % 5 == 0 else rng.randrange(15)
                tokens.append(str(record_count))
                for _ in range(record_count):
                    tokens.extend((
                        rng.choice(STATS_TYPES),
                        str(rng.choice(STATS_SIZES)),
                    ))
            cases.append("\n".join(tokens) + "\n")
    return tuple(cases)


def differential_cases(filename: str) -> tuple[tuple[str, str], ...]:
    oracle = EDITOR_ORACLES[filename]
    return tuple((stdin, oracle(stdin)) for stdin in differential_inputs(filename))


def physical_byte_cases() -> tuple[tuple[str, tuple[int, int, int, int]], ...]:
    """Extra U4L6 byte-level cases, including CR not representable by the UI names."""
    samples = (
        "",
        "\n",
        "\r\n",
        "A malla 1\r\n",
        "A malla 1\r\r\n",
        "A malla 1\rX\n",
        "A malla 1\r",
        "A\r\rB\n",
        "A\r\r",
        "A\n\nB",
        "\r\r\n",
        "\rX\n",
        "\x00\n",
        "\x80\n",
        "A" * 80 + "\r\n",
        "A" * 80 + "\r\r\n",
        "A\n" * 100,
        "A\n" * 101,
    )
    return tuple((data, count_physical_bytes(data)) for data in samples)

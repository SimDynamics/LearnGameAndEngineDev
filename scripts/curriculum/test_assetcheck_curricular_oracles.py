"""Independent output oracles for AssetCheck curricular C++ editors (U4L4-L7).

These are *not* a substitute for native C++ compilation, CTest or Windows QA.
The test checks the expected exercise outputs against the written contracts, not
against the reference implementation, so erroneous expected strings can be found.
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

CONTENT = Path(__file__).resolve().parents[2] / (
    "app/src/main/assets/content/es/programming/"
    "programming_practice_tools/proyecto_assetcheck"
)


def size_oracle(stdin: str) -> str:
    rows = []
    for token in stdin.split():
        valid_digits = re.fullmatch(r"[0-9]{1,5}", token) is not None
        size = int(token) if valid_digits else 0
        accepted = valid_digits and 1 <= size <= 30000
        rows.append(f"{token}:{int(accepted)}:{size if accepted else 91}\n")
    return "".join(rows)


def parse_record(line: str) -> tuple[str, tuple[str, str, int] | None]:
    if len(line.encode("utf-8")) > 80 or any(
        c != "\t" and not (32 <= ord(c) <= 126) for c in line
    ):
        return "FORMATO", None
    parts = line.split()
    if len(parts) != 3:
        return "FORMATO", None
    record_id, kind, size_text = parts
    if len(record_id) > 12 or re.fullmatch(
        r"[A-Za-z][A-Za-z0-9_]*", record_id
    ) is None:
        return "ID", None
    if kind not in ("textura", "malla", "sonido"):
        return "TIPO", None
    if re.fullmatch(r"[0-9]{1,5}", size_text) is None:
        return "TAMANO", None
    size = int(size_text)
    if not 1 <= size <= 30000:
        return "TAMANO", None
    return "ACEPTADO", (record_id, kind, size)


def physical_lines(stdin: str) -> list[str]:
    if stdin == "":
        return []
    lines = stdin.split("\n")
    if stdin.endswith("\n"):
        lines.pop()  # Final terminator does not add a phantom line.
    return lines


def batch_oracle(stdin: str) -> str:
    first_admission: dict[str, int] = {}
    output: list[str] = []
    for line_number, line in enumerate(physical_lines(stdin), start=1):
        cause, candidate = parse_record(line)
        if candidate is None:
            output.append(f"R {line_number} {cause}\n")
            continue
        record_id, kind, size = candidate
        if record_id in first_admission:
            output.append(f"R {line_number} DUPLICADO {first_admission[record_id]}\n")
        else:
            first_admission[record_id] = line_number
            output.append(f"A {line_number} {record_id} {kind} {size}\n")
    return "".join(output)


def sample_bytes(name: str) -> str:
    samples = {
        "VACIO": "",
        "LF": "A malla 1\nB sonido 2\n",
        "CRLF": "A malla 1\r\nB sonido 2\r\n",
        "ULTIMA": "A malla 1\nB sonido 2",
        "BLANCO": "A malla 1\n\nB sonido 2\n",
        "CRSUELTO": "A malla\r 1\n",
        "FINALCR": "A malla 1\r",
    }
    if name in ("CIEN", "CIENTOUNO"):
        return "A malla 1\n" * (100 if name == "CIEN" else 101)
    return samples.get(name, "")


def count_physical_bytes(data: str) -> tuple[int, int, int, int]:
    lines = physical_lines(data)
    if len(lines) > 100:
        return 0, 0, 0, 1  # No partial counters on a global failure.
    empty = loose_cr = 0
    for index, line in enumerate(lines):
        has_lf = index < len(lines) - 1 or data.endswith("\n")
        if has_lf and line.endswith("\r"):
            line = line[:-1]  # CRLF is a terminator, not a loose CR.
        empty += line == ""
        loose_cr += "\r" in line  # Count affected lines, not CR bytes.
    return len(lines), empty, loose_cr, 0


def file_lines_oracle(stdin: str) -> str:
    return "".join(
        f"{name}:{':'.join(map(str, count_physical_bytes(sample_bytes(name))))}\n"
        for name in stdin.split()
    )


def statistics_oracle(stdin: str) -> str:
    tokens = iter(stdin.split())
    try:
        groups = int(next(tokens))
        output: list[str] = []
        for _ in range(groups):
            count = int(next(tokens))
            totals = {"textura": 0, "malla": 0, "sonido": 0}
            total_bytes = 0
            for _ in range(count):
                kind = next(tokens)
                size = int(next(tokens))
                if kind not in totals:
                    raise ValueError("Unknown already-admitted type")
                totals[kind] += 1
                total_bytes += size
            output.append(
                "textura={textura} malla={malla} sonido={sonido} bytes={total}\n".format(
                    **totals, total=total_bytes
                )
            )
        return "".join(output)
    except StopIteration as exc:
        raise ValueError("Incomplete curricular input") from exc


EDITOR_ORACLES = {
    "lesson_leer_y_validar_un_registro.json": size_oracle,
    "lesson_procesar_un_lote_y_detectar_duplicados.json": batch_oracle,
    "lesson_leer_manifiesto_y_publicar_informe.json": file_lines_oracle,
    "lesson_disenar_probar_y_presentar_variante_propia.json": statistics_oracle,
}


class AssetCheckCurricularOraclesTest(unittest.TestCase):
    def test_every_declared_output_case(self) -> None:
        checked = 0
        for filename, oracle in EDITOR_ORACLES.items():
            with self.subTest(lesson=filename):
                document = json.loads((CONTENT / filename).read_text(encoding="utf-8"))
                editors = [
                    block
                    for page in document["pages"]
                    for block in page["blocks"]
                    if block["type"] == "cpp_compiler"
                ]
                self.assertEqual(len(editors), 1, "Expected one exercise editor")
                check = editors[0]["outputCheck"]
                self.assertEqual(check["inputComparison"], "exact")
                self.assertGreaterEqual(len(check["cases"]), 4)
                for case_number, case in enumerate(check["cases"], start=1):
                    with self.subTest(lesson=filename, case=case_number):
                        self.assertEqual(
                            len(case["outputs"]), 1,
                            "The exact-output editor must not accept unchecked alternatives",
                        )
                        self.assertEqual(
                            oracle(case["standardInput"]),
                            case["outputs"][0],
                            "Expected output contradicts the independent contract",
                        )
                        checked += 1
        self.assertGreaterEqual(checked, 16)

    def test_oracle_regressions(self) -> None:
        self.assertEqual(size_oracle("00001 +1 00000 30000 30001\n"),
                         "00001:1:1\n+1:0:91\n00000:0:91\n30000:1:30000\n30001:0:91\n")
        self.assertEqual(batch_oracle("A textura 0\nA textura 2\nA malla 3\n"),
                         "R 1 TAMANO\nA 2 A textura 2\nR 3 DUPLICADO 2\n")
        self.assertEqual(batch_oracle("A_B malla 1\na_b malla 1\n"),
                         "A 1 A_B malla 1\nA 2 a_b malla 1\n")
        self.assertEqual(count_physical_bytes(""), (0, 0, 0, 0))
        self.assertEqual(count_physical_bytes("a\r\n"), (1, 0, 0, 0))
        self.assertEqual(count_physical_bytes("a\r"), (1, 0, 1, 0))
        self.assertEqual(count_physical_bytes("a\r\r\n"), (1, 0, 1, 0))
        self.assertEqual(count_physical_bytes("a\n" * 101), (0, 0, 0, 1))
        self.assertEqual(statistics_oracle("2\n0\n2\nmalla 30000\nsonido 1\n"),
                         "textura=0 malla=0 sonido=0 bytes=0\n"
                         "textura=0 malla=1 sonido=1 bytes=30001\n")


    def test_boundary_and_precedence_contracts(self) -> None:
        # An 80-byte ASCII line is valid; the 81st byte invalidates it
        # even when the remaining fields would otherwise be acceptable.
        eighty_bytes = "A malla 1" + " " * 71
        self.assertEqual(len(eighty_bytes.encode("ascii")), 80)
        self.assertEqual(parse_record(eighty_bytes),
                         ("ACEPTADO", ("A", "malla", 1)))
        self.assertEqual(parse_record(eighty_bytes + " "), ("FORMATO", None))
        self.assertEqual(parse_record("A_B malla 30000"),
                         ("ACEPTADO", ("A_B", "malla", 30000)))
        self.assertEqual(parse_record("A_B malla 30001"), ("TAMANO", None))
        self.assertEqual(parse_record("1A malo 0"), ("ID", None))
        self.assertEqual(parse_record("A malo 0"), ("TIPO", None))
        self.assertEqual(parse_record("A malla 0"), ("TAMANO", None))
        self.assertEqual(parse_record("A malla 1\r"), ("FORMATO", None))
        self.assertEqual(parse_record("A malla 1\x00"), ("FORMATO", None))

        # A locally rejected ID is not reserved; a duplicate refers
        # to its first successful admission, not to the first sighting.
        self.assertEqual(
            batch_oracle("A malla 0\nA malla 2\nA sonido 3\n"),
            "R 1 TAMANO\nA 2 A malla 2\nR 3 DUPLICADO 2\n",
        )
        self.assertEqual(
            batch_oracle("A_B malla 30000\na_b malla 30000\n"),
            "A 1 A_B malla 30000\nA 2 a_b malla 30000\n",
        )

        # One CR of CRLF is syntax, but an earlier CR or an EOF CR
        # remains content. The 101st line invalidates all counters.
        expected_counts = {
            "": (0, 0, 0, 0),
            "\n": (1, 1, 0, 0),
            "\r\n": (1, 1, 0, 0),
            "A malla 1\r\n": (1, 0, 0, 0),
            "A malla 1\r\r\n": (1, 0, 1, 0),
            "A malla 1\rX\n": (1, 0, 1, 0),
            "A\r\rB\n": (1, 0, 1, 0),
            "A\r\r": (1, 0, 1, 0),
            "A malla 1\r": (1, 0, 1, 0),
            "A malla 1\n\nB sonido 2": (3, 1, 0, 0),
            "A malla 1\n" * 100: (100, 0, 0, 0),
            "A malla 1\n" * 101: (0, 0, 0, 1),
        }
        for data, expected in expected_counts.items():
            with self.subTest(bytes=repr(data[:20]), length=len(data)):
                self.assertEqual(count_physical_bytes(data), expected)

    def test_wrong_output_mutations_fail(self) -> None:
        # These intentionally wrong outputs represent real regressions:
        # locale-dependent formatting, a missed duplicate, an EOF CR
        # silently trimmed, and a stale total across independent groups.
        mutants = (
            (size_oracle, "30000\n", "30000:1:30,000\n"),
            (batch_oracle, "A malla 1\nA sonido 4\n",
             "A 1 A malla 1\nA 2 A sonido 4\n"),
            (file_lines_oracle, "FINALCR\n", "FINALCR:1:0:0:0\n"),
            (statistics_oracle, "2\n1\nmalla 7\n0\n",
             "textura=0 malla=1 sonido=0 bytes=7\n"
             "textura=0 malla=1 sonido=0 bytes=7\n"),
        )
        for oracle, stdin, incorrect in mutants:
            with self.subTest(oracle=oracle.__name__):
                with self.assertRaises(AssertionError):
                    self.assertEqual(
                        oracle(stdin), incorrect,
                        "Mutated incorrect expected output must not be accepted",
                    )


if __name__ == "__main__":
    unittest.main()

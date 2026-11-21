"""Run the three unchanged U3L9 assertion programs using actual Windows MSVC.

This is pre-migration evidence: it does not change the lesson, its identities,
the production grader, or a student's saved draft. Artifacts stay in temp dirs.
"""
from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / ("app/src/main/assets/content/es/programming/"
    "programming_practice_tools/comprobaciones_repetibles/"
    "lesson_comprobar_acuerdos_internos_con_aserciones.json")
PUBLIC_REJECTION = (
    "Cajas: peticion 1, piezas=-1. "
    "Se admite un entero de 0 a 20. Revise la cantidad.\n")
EXTRA_INPUTS = (
    ("0 1 4 5 20", "Cajas=0\nCajas=1\nCajas=1\nCajas=2\nCajas=5\n", "", 0),
    ("-2 21 8 9", "Cajas=2\nCajas=3\n",
     "Cajas: peticion 1, piezas=-2. Se admite un entero de 0 a 20. Revise la cantidad.\n"
     "Cajas: peticion 2, piezas=21. Se admite un entero de 0 a 20. Revise la cantidad.\n", 1),
)


def compile_and_run(source: str, input_text: str, directory: Path):
    directory.mkdir(parents=True, exist_ok=True)
    original = directory / "main.cpp"
    target = directory / "program.exe"
    original.write_text(source, encoding="utf-8", newline="\n")
    build = subprocess.run([
        "cl", "/nologo", "/std:c++latest", "/EHsc", "/Od",
        "/W4", "/permissive-", "/Zc:__cplusplus", "/utf-8",
        "/Fe:" + str(target), "/Fo:" + str(directory / "program.obj"),
        str(original)], cwd=directory, capture_output=True, text=True, timeout=60)
    if build.returncode != 0:
        raise AssertionError("MSVC build failed: " + build.stdout + build.stderr)
    # Headless Windows CI must never ask a human to dismiss crash dialogs.
    previous = ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    try:
        execution = subprocess.run([str(target)], input=input_text,
            capture_output=True, text=True, timeout=8, cwd=directory,
            creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        ctypes.windll.kernel32.SetErrorMode(previous)
    return execution


class NativeAssertions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        lesson = json.loads(LESSON.read_text(encoding="utf-8"))
        cls.active = lesson["pages"][2]["blocks"][3]
        cls.disabled = lesson["pages"][3]["blocks"][7]
        cls.repaired = lesson["pages"][4]["blocks"][3]
        cls.originals = (cls.active, cls.disabled, cls.repaired)
        cls.root_bytes = LESSON.read_bytes()
        self_check = lambda b: (b["type"] == "cpp_compiler"
                  and b["initialStandardInput"] == "-1 5\n"
                  and b.get("outputCheck") is None)
        assert all(self_check(b) for b in cls.originals)
        assert cls.active["initialCode"] == cls.repaired["initialCode"]

    def test_real_native_assertion_failure_and_ndebug_semantics(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-native-assertion-") as root:
            folder = Path(root)
            active = compile_and_run(self.active["initialCode"], "-1 5\n", folder / "active")
            print("MSVC_ASSERT_NATIVE", json.dumps({
                "exit": active.returncode, "stdout": active.stdout,
                "stderr": active.stderr[:1800]}, ensure_ascii=False))
            self.assertNotEqual(active.returncode, 0)
            self.assertEqual(active.stdout, "")
            self.assertIn(PUBLIC_REJECTION, active.stderr)
            self.assertTrue("Assertion failed" in active.stderr
                            or "assertion failed" in active.stderr)
            self.assertIn("cajas * piezas_por_caja >= piezas", active.stderr)
            disabled = compile_and_run(
                self.disabled["initialCode"], "-1 5\n", folder / "ndebug")
            self.assertEqual((disabled.returncode, disabled.stdout, disabled.stderr),
                             (1, "Cajas=1\n", PUBLIC_REJECTION))

    def test_repaired_formula_and_hidden_inputs(self):
        wrong = "const int cajas = piezas / piezas_por_caja;"
        self.assertEqual(self.repaired["initialCode"].count(wrong), 1)
        repaired = self.repaired["initialCode"].replace(
            wrong, "const int cajas = (piezas + piezas_por_caja - 1) / piezas_por_caja;")
        with tempfile.TemporaryDirectory(prefix="lgaed-native-assertion-") as root:
            folder = Path(root)
            visible = compile_and_run(repaired, "-1 5\n", folder / "visible")
            self.assertEqual((visible.returncode, visible.stdout, visible.stderr),
                             (1, "Cajas=2\n", PUBLIC_REJECTION))
            for i, (source, output, error, code) in enumerate(EXTRA_INPUTS):
                result = compile_and_run(repaired, source, folder / f"private_{i}")
                self.assertEqual((result.returncode, result.stdout, result.stderr),
                                 (code, output, error))
            hardcoded = self.repaired["initialCode"].replace(wrong, "const int cajas = 2;")
            bad = compile_and_run(hardcoded, EXTRA_INPUTS[0][0],
                                  folder / "hardcoded")
            self.assertNotEqual((bad.returncode, bad.stdout, bad.stderr),
                                (EXTRA_INPUTS[0][3], EXTRA_INPUTS[0][1], EXTRA_INPUTS[0][2]))
        self.assertEqual(LESSON.read_bytes(), self.root_bytes)


if __name__ == "__main__":
    unittest.main()

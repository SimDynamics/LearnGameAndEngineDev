"""Compile real P1 frequency programs before/after an in-memory name revision.

This integration QA is separate from the lexical-proof unit suite. It needs
a C++23 compiler and never edits a lesson, learner profile or persistent build.
Compilation and result equivalence are evidence for these specific programs;
they are not a general proof of C++ binding/scope equivalence.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

import identifier_rename_compat as rename


ROOT = Path(__file__).resolve().parents[2]
PATH = (ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json")
LESSON_ID = "cpp_fundamentals__colecciones_y_recorridos__contar_frecuencias_y_conservar_empates"
OWNER_CASES = (
    ("block_0006_0002", "code", "etiquetas_mas_frecuentes"),
    ("recuperacion_entrada_editor", "cpp_compiler", "etiquetas_mas_frecuentes"),
    ("block_0006_0009", "cpp_compiler", "etiquetas_mas_frecuentes"),
    ("block_0007_0002", "output_prediction", "etiquetas_mas_frecuentes"),
    ("block_0007_0004", "output_prediction", "etiquetas_mas_frecuentes"),
    ("frecuencias_numericas_referencia_programa", "code", "valores_mas_frecuentes"),
)
# Additional reference inputs. Compiler exercises use their exact authored
# battery; predictions instead use the literal program data and answer oracle.
EXAMPLE_INPUTS = {
    "block_0006_0002": ("",),
    "frecuencias_numericas_referencia_programa": (
        "0\n", "7\n4 4 1 1 3 3 3\n", "4\n9 4 9 4\n"),
}


def prediction_text_without_one_terminal_newline(value: str) -> str:
    """Normalize only the final line terminator in authored prediction answers.

    The published prediction is entered as text without a terminal newline,
    while the reference C++ program prints one with std::cout. Internal lines,
    trailing spaces, and additional blank output lines remain exact.
    """
    if value.endswith("\r\n"):
        return value[:-2]
    if value.endswith("\n"):
        return value[:-1]
    return value


def authored_sources(lesson: dict) -> dict[str, dict]:
    return {
        identity: block
        for page, record in zip(
                lesson["pages"], lesson["stateLayout"]["current"])
        for identity, block in zip(record["blocks"], page["blocks"])
    }


def propose(original: dict, destination: str) -> dict:
    revised = deepcopy(original)
    slot = "initialCode" if original["type"] == "cpp_compiler" else "code"
    old = original[slot]
    revised[slot] = re.sub(
        r"\bmayores\b", destination,
        re.sub(r"\bmaxima\b", "frecuencia_maxima_encontrada", old))
    proof = rename.require_declared_cpp_renames(
        original, revised,
        {slot: {"maxima": "frecuencia_maxima_encontrada",
                "mayores": destination}},
        {slot: {"maxima": 5, "mayores": 5}})
    if proof != {slot: {"maxima": 5, "mayores": 5}}:
        raise AssertionError("A renamed owner lost its exact lexical proof")
    return revised


class FrequencyRenameRuntime(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        # Do not silently pass CI because the C++ toolchain is unavailable.
        cls.compiler = shutil.which("cl.exe") or shutil.which("cl")
        if cls.compiler is None:
            raise RuntimeError("The real frequency regression requires Windows MSVC C++23")
        cls.lesson_bytes = PATH.read_bytes()
        cls.lesson = json.loads(cls.lesson_bytes)
        if cls.lesson["id"] != LESSON_ID:
            raise AssertionError("A different publication replaced the pinned frequency lesson")
        cls.blocks = authored_sources(cls.lesson)

    def _compile(self, source: str, folder: Path, label: str) -> Path:
        program = folder / (label + ".cpp")
        executable = folder / (label + ".exe")
        program.write_text(source, encoding="utf-8")
        completed = subprocess.run(
            [self.compiler, "/nologo", "/std:c++latest", "/EHsc",
             "/W4", "/permissive-", "/Zc:__cplusplus", "/utf-8",
             "/Fe:" + str(executable), str(program)],
            cwd=folder, capture_output=True, text=True, timeout=75, check=False)
        self.assertEqual(
            completed.returncode, 0,
            f"Compilation of {label} failed:\n{completed.stdout}\n{completed.stderr}")
        return executable

    def _execute(self, binary: Path, standard_input: str) -> tuple[int, str, str]:
        completed = subprocess.run(
            [str(binary)], input=standard_input,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=15, check=False)
        return completed.returncode, completed.stdout, completed.stderr

    def test_cross_file_declared_symbol_remains_linkable_after_rename(self) -> None:
        # A single external C++ symbol is declared in main and defined in a
        # separate translation unit. The lexical proof must permit its same
        # historic-to-current mapping in both sources, then the actual linker
        # must establish that the new program still behaves identically.
        original = {
            "type": "cpp_compiler",
            "initialCode": (
                '#include <iostream>\n'
                'extern int maxima;\n'
                'int main() {\n'
                '    std::cout << "Maxima:" << maxima << "\\n";\n'
                '}\n'
            ),
            "projectFiles": [
                {"path": "src/valor.cpp", "initialCode": "int maxima = 8;\n"},
            ],
            "outputCheck": {"cases": [{"outputs": ["Maxima:8\n"]}]},
        }
        after = deepcopy(original)
        after["initialCode"] = original["initialCode"].replace(
            "maxima", "frecuencia_maxima_encontrada")
        after["projectFiles"][0]["initialCode"] = (
            original["projectFiles"][0]["initialCode"].replace(
                "maxima", "frecuencia_maxima_encontrada"))
        rename_map = {
            "initialCode": {"maxima": "frecuencia_maxima_encontrada"},
            "projectFiles/0/initialCode": {"maxima": "frecuencia_maxima_encontrada"},
        }
        counts = {
            "initialCode": {"maxima": 2},
            "projectFiles/0/initialCode": {"maxima": 1},
        }
        self.assertEqual(
            rename.require_declared_cpp_renames(original, after, rename_map, counts),
            counts)
        with tempfile.TemporaryDirectory(prefix="lgaed-linked-rename-") as tmp:
            directory = Path(tmp)
            results = []
            for version, project in (("old", original), ("new", after)):
                main = directory / (version + "-main.cpp")
                other = directory / (version + "-valor.cpp")
                binary = directory / (version + ".exe")
                main.write_text(project["initialCode"], encoding="utf-8")
                other.write_text(
                    project["projectFiles"][0]["initialCode"], encoding="utf-8")
                built = subprocess.run(
                    [self.compiler, "/nologo", "/std:c++latest", "/EHsc",
                     "/W4", "/permissive-", "/Zc:__cplusplus", "/utf-8",
                     str(main), str(other), "/Fe:" + str(binary)],
                    cwd=directory, capture_output=True, text=True,
                    timeout=75, check=False)
                self.assertEqual(
                    built.returncode, 0,
                    f"Cross-file {version} C++ link failed:\n{built.stderr}")
                results.append(self._execute(binary, ""))
            self.assertEqual(results, [(0, "Maxima:8\n", "")] * 2)

    def test_source_location_renaming_changes_real_msvc_output(self) -> None:
        # The C++20 standard library exposes the current function spelling.
        # This is an actual compiled counterexample, not just a lexical guess.
        previous = (
            "#include <iostream>\n"
            "#include <source_location>\n"
            "void maxima() {\n"
            '    std::cout << std::source_location::current().function_name() << "\\n";\n'
            "}\n"
            "int main() { maxima(); }\n"
        )
        revised = previous.replace(
            "maxima()", "frecuencia_maxima_encontrada()")
        with tempfile.TemporaryDirectory(prefix="lgaed-source-location-rename-") as tmp:
            folder = Path(tmp)
            old = self._execute(self._compile(previous, folder, "old-function"), "")
            new = self._execute(self._compile(revised, folder, "new-function"), "")
        self.assertEqual((old[0], old[2], new[0], new[2]), (0, "", 0, ""))
        self.assertIn("maxima", old[1])
        self.assertIn("frecuencia_maxima_encontrada", new[1])
        self.assertNotEqual(old[1], new[1],
                            "A function-name observable change escaped native verification")

    def test_prediction_normalizes_one_terminal_newline_only(self) -> None:
        # A published answer need not repeat the final std::cout line ending.
        correct = "Frecuencias:\n[]=2\nMaxima:2"
        self.assertEqual(
            prediction_text_without_one_terminal_newline(correct + "\n"), correct)
        self.assertEqual(
            prediction_text_without_one_terminal_newline(correct + "\r\n"), correct)
        self.assertEqual(
            prediction_text_without_one_terminal_newline(correct), correct)
        self.assertNotEqual(
            prediction_text_without_one_terminal_newline(correct + "\n\n"), correct)
        self.assertNotEqual(
            prediction_text_without_one_terminal_newline(correct + " \n"), correct)
        self.assertNotEqual(
            prediction_text_without_one_terminal_newline(
                correct.replace("[]=2", "[]=3") + "\n"), correct)

    def test_six_real_published_owners_compile_with_identical_outputs(self) -> None:
        self.assertEqual(len(OWNER_CASES), 6)
        with tempfile.TemporaryDirectory(prefix="lgaed-frequency-rename-") as temp:
            directory = Path(temp)
            for index, (identity, kind, destination) in enumerate(OWNER_CASES):
                with self.subTest(owner=identity):
                    self.assertIn(identity, self.blocks)
                    before = self.blocks[identity]
                    self.assertEqual(before["type"], kind)
                    after = propose(before, destination)
                    slot = "initialCode" if kind == "cpp_compiler" else "code"
                    self.assertIn('"Maxima:"', before[slot])
                    self.assertIn('"Maxima:"', after[slot])
                    self.assertIn("Mayores:", before[slot])
                    self.assertIn("Mayores:", after[slot])
                    old_binary = self._compile(
                        before[slot], directory, f"owner-{index}-old")
                    new_binary = self._compile(
                        after[slot], directory, f"owner-{index}-new")

                    if kind == "cpp_compiler":
                        # The first case is the progression gate; the remaining
                        # curriculum cases are nonblocking QA observations.
                        cases = before["outputCheck"]["cases"]
                        self.assertGreaterEqual(len(cases), 1)
                        for case_index, case in enumerate(cases):
                            with self.subTest(case=case_index):
                                original_result = self._execute(
                                    old_binary, case.get("standardInput", ""))
                                revised_result = self._execute(
                                    new_binary, case.get("standardInput", ""))
                                self.assertEqual(revised_result, original_result)
                                self.assertEqual(original_result[0], case.get("exitCode", 0))
                                self.assertEqual(original_result[2], "")
                                self.assertIn(original_result[1], case["outputs"])
                    elif kind == "output_prediction":
                        original_result = self._execute(old_binary, "")
                        revised_result = self._execute(new_binary, "")
                        self.assertEqual(revised_result, original_result)
                        self.assertEqual(original_result[0], 0)
                        self.assertEqual(original_result[2], "")
                        self.assertIn(
                            prediction_text_without_one_terminal_newline(original_result[1]),
                            [prediction_text_without_one_terminal_newline(answer)
                             for answer in before["answers"]])
                    else:
                        for given_input in EXAMPLE_INPUTS.get(identity, ("",)):
                            with self.subTest(standard_input=given_input):
                                original_result = self._execute(
                                    old_binary, given_input)
                                revised_result = self._execute(
                                    new_binary, given_input)
                                self.assertEqual(revised_result, original_result)
                                self.assertEqual(original_result[0], 0)
                                self.assertEqual(original_result[2], "")
                                self.assertIn("Maxima:", original_result[1])
                                self.assertIn("Mayores:", original_result[1])
        self.assertEqual(PATH.read_bytes(), self.lesson_bytes)


if __name__ == "__main__":
    unittest.main()

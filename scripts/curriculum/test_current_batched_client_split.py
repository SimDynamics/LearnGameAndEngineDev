"""Original U6L19 batch versus independent one-input Windows C++ sessions.

Only in-memory authoring; never rewrites Work JSON, student drafts or progress.
"""
from copy import deepcopy
import json
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
REL = ("programming/cpp_fundamentals/funciones_y_contratos/"
       "lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json")
ASSETS = ROOT / "app/src/main/assets/content"
OWNER = "capacidad_veinte_bateria"
AMOUNTS = (0, 12, 19, 20, 21, 24, 25, 120)
CLIENT = (
    '#include "sobres.h"\n'
    '#include <iostream>\n'
    '\n'
    'int main() {\n'
    '    int tarjetas{};\n'
    '    if (!(std::cin >> tarjetas)) return 1;\n'
    '    std::cout << "Tarjetas: " << tarjetas << "; "\n'
    '              << descripcion_sobres(tarjetas)\n'
    '              << ": " << sobres_necesarios(tarjetas);\n'
    '    return 0;\n'
    '}\n'
)


class CurrentBatchSplit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = ASSETS / "es" / REL
        cls.bytes = cls.path.read_bytes()
        cls.before = json.loads(cls.bytes)
        cls.family = {
            locale: json.loads((ASSETS / locale / REL).read_bytes())
            for locale in cls.before["stateLayout"]["family"]
        }
        cls.owner = layout._blocks(cls.before)[OWNER]

    def case_list(self):
        old = self.owner["outputCheck"]["cases"]
        self.assertEqual(len(old), 1)
        self.assertEqual(old[0]["standardInput"], "")
        lines = old[0]["outputs"][0].split("\n")
        self.assertEqual(len(lines), len(AMOUNTS))
        old_main = next(file["initialCode"] for file in self.owner["projectFiles"]
                        if file["path"] == "main.cpp")
        self.assertEqual(old_main, self.owner["initialCode"])
        for amount, line in zip(AMOUNTS, lines, strict=True):
            self.assertTrue(line.startswith(f"Tarjetas: {amount}; "))
            self.assertEqual(old_main.count(f"descripcion_sobres({amount})"), 1)
            self.assertEqual(old_main.count(f"sobres_necesarios({amount})"), 1)
        return [
            {"standardInput": f"{amount}\n", "outputs": [line, line + "\n"],
             "exitCode": 0}
            for amount, line in zip(AMOUNTS, lines, strict=True)
        ]

    def reviewed(self):
        return layout.revise_compiler_client_cases(
            self.before, OWNER, {"path": "main.cpp", "source": CLIENT},
            self.case_list(), case_semantics=[])

    def test_eight_separate_cases_keep_history_and_stable_owner(self):
        after = self.reviewed()
        layout.validate_transition(self.before, after)
        self.assertEqual(layout.validate(self.before), layout.validate(after))
        original = self.before["stateLayout"]
        revised = after["stateLayout"]
        for field in ("origin", "current", "family", "additions",
                      "sourceFormatRevisions", "retirementHistory"):
            self.assertEqual(original.get(field), revised.get(field))
        self.assertEqual(revised["editorial"][:len(original["editorial"])],
                         original["editorial"])
        self.assertEqual(len(revised["editorial"]), len(original["editorial"]) + 1)
        previous = original.get("compilerCaseRevisions", [])
        journal = revised["compilerCaseRevisions"]
        self.assertEqual(journal[:len(previous)], previous)
        self.assertEqual(len(journal), len(previous) + 1)
        last = journal[-1]
        self.assertEqual(last["id"], OWNER)
        self.assertEqual(last["block"], self.owner)
        self.assertEqual(last["client"], {"path": "main.cpp", "source": CLIENT})
        previous_marks = [field for field in self.before["textSemantics"]["fields"]
                          if field["block"] == OWNER and
                          field["path"].startswith("/outputCheck/cases/")]
        self.assertEqual(last["semanticBefore"], previous_marks)
        self.assertEqual(last["semanticAfter"], [])
        block = layout._blocks(after)[OWNER]
        self.assertEqual(block["initialCode"], CLIENT)
        for old, new in zip(self.owner["projectFiles"],
                            block["projectFiles"], strict=True):
            self.assertEqual(new["path"], old["path"])
            self.assertEqual(new["initialCode"],
                             CLIENT if old["path"] == "main.cpp" else old["initialCode"])
        self.assertEqual(block["sourceFiles"], self.owner["sourceFiles"])
        self.assertEqual(block["outputCheck"]["cases"], self.case_list())
        self.assertEqual([f for f in after["textSemantics"]["fields"]
                          if f["block"] != OWNER],
                         [f for f in self.before["textSemantics"]["fields"]
                          if f["block"] != OWNER])
        self.assertFalse(any(f["block"] == OWNER
                             for f in after["textSemantics"]["fields"]))
        self.assertEqual(self.path.read_bytes(), self.bytes)

    def test_actual_reference_programs_compile_and_run_each_input(self):
        """Run the original Work-authored reference, not an invented answer.

        Compile its unchanged header and source with both the original client
        and the new single-input client. All builds and executions are isolated
        and no student's source, compiler profile or historical asset is used.
        """
        reference = layout._blocks(self.before)["capacidad_veinte_referencia_codigo"]
        self.assertEqual(reference["type"], "code")
        actual_reference = reference["code"]
        self.assertIn("int sobres_necesarios(int tarjetas)", actual_reference)
        self.assertIn("tarjetas / 20", actual_reference)
        self.assertIn("tarjetas % 20", actual_reference)
        old_files = {file["path"]: file["initialCode"]
                     for file in self.owner["projectFiles"]}
        self.assertEqual(set(old_files), {"sobres.h", "sobres.cpp", "main.cpp"})
        # Use the current published function body as source of truth. Its
        # original batch output defines the independent case expectations.
        expected_original = self.owner["outputCheck"]["cases"][0]["outputs"]
        expected_individual = self.case_list()
        with tempfile.TemporaryDirectory(prefix="lgaed-u6l19-session-") as folder:
            directory = Path(folder)
            (directory / "sobres.h").write_text(old_files["sobres.h"], encoding="utf-8")
            (directory / "sobres.cpp").write_text(actual_reference, encoding="utf-8")
            for label, client, cases in (
                ("batched", old_files["main.cpp"],
                 [{"standardInput": "", "outputs": expected_original, "exitCode": 0}]),
                ("individual", CLIENT, expected_individual),
            ):
                (directory / "main.cpp").write_text(client, encoding="utf-8")
                exe = directory / (label + ".exe")
                command = ["cl", "/nologo", "/std:c++latest", "/EHsc",
                           "/W4", "/permissive-", "/utf-8", "main.cpp",
                           "sobres.cpp", "/Fe:" + str(exe)]
                compiled = subprocess.run(command, cwd=directory,
                                          capture_output=True, timeout=90, check=False)
                self.assertEqual(compiled.returncode, 0,
                    f"Real {label} C++ compilation failed: "
                    + compiled.stdout.decode(errors="replace")[-2500:]
                    + compiled.stderr.decode(errors="replace")[-2500:])
                for index, case in enumerate(cases):
                    with self.subTest(client=label, case=index + 1):
                        observed = subprocess.run(
                            [str(exe)], input=case["standardInput"].encode("utf-8"),
                            cwd=directory, capture_output=True, timeout=20, check=False)
                        output = observed.stdout.replace(b"\r\n", b"\n").decode("utf-8")
                        self.assertEqual(observed.returncode, case["exitCode"])
                        self.assertEqual(observed.stderr, b"")
                        self.assertIn(output, case["outputs"])
        self.assertEqual(self.path.read_bytes(), self.bytes)

    def test_unverified_cases_and_source_changes_are_not_allowed(self):
        valid = self.reviewed()
        for mode in ("oracle", "client", "other-source"):
            with self.subTest(mode=mode):
                wrong = deepcopy(valid)
                current = layout._blocks(wrong)[OWNER]
                if mode == "oracle":
                    current["outputCheck"]["cases"][0]["outputs"][0] += " falso"
                elif mode == "client":
                    wrong["stateLayout"]["compilerCaseRevisions"][-1]["client"]["source"] += " "
                else:
                    current["projectFiles"][0]["initialCode"] += "// no autorizado"
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(self.before, wrong)
        self.assertEqual(self.path.read_bytes(), self.bytes)



class CmakeRepairSessionSplit(unittest.TestCase):
    """Prove that optional individual cases and the original failure coexist.

    The existing engine observes the defective project with empty stdin.
    A revised checker must still execute its complete original batch at EOF;
    the first scored case uses a nonempty selector. Test only disposable
    copies of the two authentic projects, never publish the lesson JSON.
    """
    DIRECTORY = ASSETS / "es/programming/programming_practice_tools/comprobaciones_repetibles"

    def split_checker(self, case, source):
        _, _, _, _, library, _, _ = case
        def replace(text, old, new):
            self.assertEqual(text.count(old), 1, old)
            return text.replace(old, new, 1)

        source = replace(source, "int main() {\n",
            "int main() {\n"
            "    int elegido{};\n"
            "    const bool individual = bool(std::cin >> elegido);\n"
            "    if (!individual && !std::cin.eof()) return 2;\n")
        if library == "cantidades":
            source = replace(source, "    bool todo_bien = true;\n",
                "    bool todo_bien = true;\n"
                "    bool encontrado = !individual;\n")
            source = replace(source, "    for (int i = 0; i < 4; ++i) {\n",
                "    for (int i = 0; i < 4; ++i) {\n"
                "        if (individual && datos[i] != elegido) continue;\n"
                "        encontrado = true;\n")
            source = replace(source, '    std::cout << (todo_bien ?',
                '    if (!encontrado) return 2;\n'
                '    std::cout << (todo_bien ?')
        else:
            source = replace(source, "    int fallos{0};\n",
                "    if (individual && (elegido < 1 || elegido > 11)) return 2;\n"
                "    int fallos{0};\n")
            for number in range(1, 12):
                source = replace(source, f"    if (!comprobar({number},",
                    f"    if ((!individual || elegido == {number}) && !comprobar({number},")
        return source

    def run_tool(self, command, directory):
        process = subprocess.run(command, cwd=directory, capture_output=True,
                                 timeout=120, check=False)
        self.assertEqual(process.returncode, 0,
            "Real MSVC/CMake operation failed:\n" +
            (process.stdout + process.stderr).decode(errors="replace")[-4000:])
        return process

    def verify_execution(self, executable, directory, stdin, stdout, code):
        process = subprocess.run([str(executable)], cwd=directory,
            input=stdin.encode("utf-8"), capture_output=True, timeout=20, check=False)
        self.assertEqual(process.returncode, code, stdin)
        self.assertEqual(process.stdout.replace(b"\r\n", b"\n"), stdout.encode("utf-8"), stdin)
        self.assertEqual(process.stderr, b"", stdin)

    def test_real_cmake_repairs_keep_complete_failure_and_independent_cases(self):
        from test_cmake_graph_checks import EXPRESSION_LESSONS, initial_observation_declaration

        for case in EXPRESSION_LESSONS:
            name, identity, implementation, defect, library, checker, driver = case
            with self.subTest(lesson=name):
                path = self.DIRECTORY / name
                original_bytes = path.read_bytes()
                publication = json.loads(original_bytes)
                layout.validate(publication)
                # Follow any authentic already-published history rather than
                # treating the mutable current source as the initial batch.
                before = publication
                if any(x["id"] == identity for x in
                       before["stateLayout"].get("compilerCaseRevisions", [])):
                    # This QA exercises the original-to-reviewed transition.
                    # Once Work has published it, its authenticated predecessor
                    # is still checked by the normal curriculum validator.
                    self.assertTrue(layout._blocks(before)[identity]["outputCheck"]["cases"])
                    continue
                original = layout._blocks(before)[identity]
                files = {item["path"]: item["initialCode"] for item in original["projectFiles"]}
                old_cases = original["outputCheck"]["cases"]
                self.assertEqual(len(old_cases), 1)
                self.assertEqual(old_cases[0]["standardInput"], "")
                complete = old_cases[0]["outputs"][0]
                lines = complete.splitlines(keepends=True)
                selectors = (-1, 0, 20, 21) if library == "cantidades" else tuple(range(1, 12))
                self.assertEqual(len(lines), len(selectors) + 1)
                first = -1 if library == "cantidades" else 3
                individual_cases = [
                    {"standardInput": f"{selector}\n", "outputs": [line + lines[-1]], "exitCode": 0}
                    for selector, line in zip(selectors, lines[:-1], strict=True)
                ]
                individual_cases.sort(key=lambda entry: entry["standardInput"] != f"{first}\n")
                self.assertEqual(individual_cases[0]["standardInput"], f"{first}\n")
                # Preserve the exact historical complete report as an
                # optional contrast too; no extra case gates navigation.
                individual_cases.append(deepcopy(old_cases[0]))
                candidate = deepcopy(before)
                graph = initial_observation_declaration(case)
                existing = candidate.setdefault("compilerCmakeGraphChecks", [])
                match = [entry for entry in existing if entry["activity"] == identity]
                if match:
                    self.assertEqual(len(match), 1)
                    self.assertEqual(match[0]["initialObservation"], graph["initialObservation"])
                else:
                    existing.append(graph)
                layout.validate_transition(before, candidate)
                if library == "cantidades":
                    targets = (library,)
                    repair = "dato >= 0 && dato <= 20"
                else:
                    targets = (library, "consultar_ajustes", checker)
                    repair = "superior - valor"
                recipe = files["CMakeLists.txt"]
                for target in targets:
                    legacy = f"target_compile_options({target} PRIVATE -Wall -Wextra -Wpedantic)"
                    self.assertEqual(recipe.count(legacy), 1)
                    recipe = recipe.replace(legacy,
                        f"target_compile_options({target} PRIVATE /W4 /permissive-)", 1)
                adapted = layout.revise_compiler_cmake_recipe(candidate, identity,
                    {"path": "CMakeLists.txt", "source": recipe})
                client = self.split_checker(case, files[driver])
                # The retained aggregate guide has the same text but a new
                # case path. Move its authored descriptor, not its offsets.
                marks = layout._compiler_case_order_fields(adapted, identity)
                for mark in marks:
                    self.assertTrue(mark["path"].startswith("/outputCheck/cases/0/"))
                    mark["path"] = mark["path"].replace(
                        "/outputCheck/cases/0/", f"/outputCheck/cases/{len(selectors)}/", 1)
                reviewed = layout.revise_compiler_client_cases(adapted, identity,
                    {"path": driver, "source": client}, individual_cases, case_semantics=marks)
                layout.validate_transition(before, reviewed)
                self.assertEqual(layout.validate(before), layout.validate(reviewed))
                self.assertEqual(before["stateLayout"]["origin"], reviewed["stateLayout"]["origin"])
                self.assertEqual(before["stateLayout"]["family"], reviewed["stateLayout"]["family"])
                self.assertEqual(before["stateLayout"]["current"], reviewed["stateLayout"]["current"])
                self.assertEqual(layout._blocks(
                    layout.align_for_previous_validation(before, reviewed))[identity], original)
                current = layout._blocks(reviewed)[identity]
                journal = [x for x in reviewed["stateLayout"]["compilerCaseRevisions"]
                           if x["id"] == identity]
                self.assertEqual(len(journal), 2)
                self.assertEqual(journal[0]["block"], original)
                self.assertEqual(journal[1]["block"], layout._blocks(adapted)[identity])
                self.assertEqual(current["outputCheck"]["cases"], individual_cases)
                self.assertEqual(current.get("initialStandardInput"), original.get("initialStandardInput"))
                for entry in current["projectFiles"]:
                    expected = recipe if entry["path"] == "CMakeLists.txt" else (
                        client if entry["path"] == driver else files[entry["path"]])
                    self.assertEqual(entry["initialCode"], expected)
                observation = next(item["initialObservation"]
                    for item in reviewed["compilerCmakeGraphChecks"] if item["activity"] == identity)
                self.assertEqual(observation["input"], "")
                self.assertEqual(observation["exitCode"], 1)

                # Run the *revised* authenticated checker, not an alternative
                # QA executable with fabricated output. CMake builds the
                # actual protected library and checker in an isolated tree.
                with tempfile.TemporaryDirectory(prefix="lgaed-cmake-split-") as folder:
                    directory = Path(folder)
                    for entry in current["projectFiles"]:
                        destination = directory / entry["path"]
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        destination.write_bytes(entry["initialCode"].encode("utf-8"))
                    build = directory / "build"
                    self.run_tool(["cmake", "-S", str(directory), "-B", str(build),
                                   "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
                                   "-DCMAKE_CXX_COMPILER=cl.exe"], directory)
                    compiled = self.run_tool(["cmake", "--build", str(build),
                                              "--target", checker], directory)
                    if library == "cantidades":
                        self.assertIn(b"C4804", compiled.stdout + compiled.stderr)
                    exe = build / (checker + ".exe")
                    self.verify_execution(exe, directory, observation["input"],
                        observation["output"], observation["exitCode"])
                    first_bad = next(line for line, selector in
                        zip(observation["output"].splitlines(keepends=True)[:-1], selectors, strict=True)
                        if selector == first)
                    bad_summary = "Bateria: ERROR\n" if library == "cantidades" else "Fallos=1\n"
                    self.verify_execution(exe, directory, f"{first}\n", first_bad + bad_summary, 1)

                    # Only the protected expression changes between phases;
                    # the aggregate and every individual oracle are checked.
                    self.assertEqual(files[implementation].count(defect), 1)
                    corrected = files[implementation].replace(defect, repair, 1)
                    (directory / implementation).write_bytes(corrected.encode("utf-8"))
                    self.run_tool(["cmake", "--build", str(build), "--target", checker], directory)
                    for item in individual_cases:
                        self.verify_execution(exe, directory, item["standardInput"],
                            item["outputs"][0], item.get("exitCode", 0))
                    if library == "cantidades":
                        (directory / implementation).write_bytes(
                            corrected.replace("dato <= 20", "dato < 20", 1).encode("utf-8"))
                        self.run_tool(["cmake", "--build", str(build), "--target", checker], directory)
                        self.verify_execution(exe, directory, "20\n",
                            "dato=20 esperado=1 obtenido=0 ERROR\nBateria: ERROR\n", 1)
                self.assertEqual(path.read_bytes(), original_bytes)
                print(f"PASS {name}: original failure, independent first case, all optional cases, "
                      "authentic MSVC/CMake source history and unchanged lesson")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--export-native-qa":
        # Emit only disposable native QA data, not the Work lesson.
        directory = Path(sys.argv[2])
        directory.mkdir(parents=True, exist_ok=True)
        CurrentBatchSplit.setUpClass()
        candidate = CurrentBatchSplit().reviewed()
        layout.validate_transition(CurrentBatchSplit.before, candidate)
        for name, document in (("before", CurrentBatchSplit.before), ("after", candidate)):
            (directory / (name + ".json")).write_text(
                json.dumps(document, ensure_ascii=False), encoding="utf-8")
        print("PASS isolated U6L19 Windows case fixture; curriculum intact")
    else:
        unittest.main()

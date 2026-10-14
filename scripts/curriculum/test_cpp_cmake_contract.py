"""Optional CMake content contract; no curriculum fixtures are published."""
import copy
import unittest

import validar_json_programacion as contract


class CmakeContractTest(unittest.TestCase):
    def setUp(self):
        contract.ERRORS.clear()
        self.block = {
            "type": "cpp_compiler", "language": "cpp", "initialCode": "",
            "projectFiles": [{"path": "CMakeLists.txt", "initialCode": "project(Example LANGUAGES CXX)"}],
            "cmake": {"target": "consulta", "options": {"USAR_COMPARACION_ESTRICTA": "OFF"}},
        }

    def validate(self, block):
        contract.ERRORS.clear()
        contract.validate_cpp_compiler(block, "fixture")
        return list(contract.ERRORS)

    def test_cmake_owns_sources_and_accepts_incomplete_editable_projects(self):
        self.assertEqual([], self.validate(self.block))
        self.block["projectFiles"] = [{"path": "include/data.h", "initialCode": "// Draft"}]
        self.assertEqual([], self.validate(self.block))
        self.block["sourceFiles"] = []
        self.assertTrue(self.validate(self.block))

    def test_legacy_source_selection_remains_required(self):
        self.block.pop("cmake")
        self.assertTrue(self.validate(self.block))
        self.block["projectFiles"].append({"path": "main.cpp", "initialCode": "int main() {}"})
        self.assertEqual([], self.validate(self.block))
        self.block["sourceFiles"] = ["include/data.h"]
        self.assertTrue(self.validate(self.block))

    def test_requires_files_and_an_object_with_explicit_target(self):
        for config in (None, True, "consulta", {}, {"target": "--help"},
                       {"target": True}, {"target": 123}, {"target": []}, {"target": {}},
                       {"target": "consulta", "other": 1}):
            with self.subTest(config=config):
                self.block["cmake"] = config
                self.assertTrue(self.validate(self.block))
        self.block["cmake"] = {"target": "consulta"}
        self.block.pop("projectFiles")
        self.assertTrue(self.validate(self.block))

    def test_project_options_preserve_text_and_reject_runtime_overrides(self):
        for options in ({"LABEL": "  a=b  "}, {}, {"EXTRA": "😀" * 2048}):
            self.block["cmake"]["options"] = options
            self.assertEqual([], self.validate(self.block))
        for options in (None, [], {"CMAKE_CXX_COMPILER": "wrong"}, {"lgp_tools": "wrong"},
                        {"flag": True}, {"a": "a\tb"}, {"a": "a\nb"}, {"a": "😀" * 2049},
                        {str(i): "x" for i in range(65)}):
            with self.subTest(options=options):
                self.block["cmake"]["options"] = options
                self.assertTrue(self.validate(self.block))

    def test_safe_files_and_build_types_match_runtime(self):
        for path in ("../main.cpp", "/main.cpp", "a/../main.cpp", "C:/main.cpp"):
            with self.subTest(path=path):
                self.block["projectFiles"][0]["path"] = path
                self.assertTrue(self.validate(self.block))
        self.block["projectFiles"][0]["path"] = "CMakeLists.txt"
        for value in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"):
            self.block["cmake"]["buildType"] = value
            self.assertEqual([], self.validate(self.block))
        for value in ("custom", [], 1):
            self.block["cmake"]["buildType"] = value
            self.assertTrue(self.validate(self.block))

    def test_optional_extension_does_not_change_existing_fields_or_identity(self):
        old = {"type": "cpp_compiler", "language": "cpp", "initialCode": "int main() {}"}
        current = dict(copy.deepcopy(old), projectFiles=self.block["projectFiles"], cmake=self.block["cmake"])
        self.assertTrue(contract.preserves_previous_compiler("fixture.json", 0, 0, old, current, {}))
        current["initialCode"] = "int main() { return 1; }"
        self.assertFalse(contract.preserves_previous_compiler("fixture.json", 0, 0, old, current, {}))


if __name__ == "__main__":
    unittest.main()

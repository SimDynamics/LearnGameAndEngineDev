"""Real executable checks for the one Windows curricular C++23 toolchain."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from windows_cpp_ci import msvc_cpp23_command, windows_executable


class WindowsCppCompiler(unittest.TestCase):
    def test_compiles_original_cpp23_without_writing_into_repository(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-native-msvc-command-") as root:
            folder = Path(root)
            source = folder / "original.cpp"
            target = folder / "run-original"
            source.write_text('#include <iostream>\nint main() { std::cout << "OK\\n"; }\n',
                              encoding="utf-8")
            command = msvc_cpp23_command(target, source)
            self.assertIn("/Fe:" + str(windows_executable(target)), command)
            self.assertIn("/Fo:" + str(windows_executable(target).with_suffix(".obj")),
                          command)
            result = subprocess.run(command, cwd=folder, capture_output=True,
                                    text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(windows_executable(target).is_file())
            completed = subprocess.run([str(windows_executable(target))],
                                       cwd=folder, capture_output=True,
                                       text=True, timeout=15)
            self.assertEqual((completed.returncode, completed.stdout, completed.stderr),
                             (0, "OK\n", ""))

    def test_compiles_two_files_into_same_isolated_build_directory(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-native-msvc-link-") as root:
            folder = Path(root)
            main = folder / "main.cpp"
            implementation = folder / "value.cpp"
            target = folder / "linked"
            main.write_text("int value(); int main() { return value()==7 ? 0 : 1; }\n")
            implementation.write_text("int value() { return 7; }\n")
            command = msvc_cpp23_command(target, main, implementation, include=folder)
            self.assertIn("/I", command)
            self.assertIn(str(folder) + os.sep, command[-1])
            built = subprocess.run(command, cwd=folder, capture_output=True,
                                   text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            executed = subprocess.run([str(windows_executable(target))],
                                      capture_output=True, text=True, timeout=15)
            self.assertEqual(executed.returncode, 0)

    def test_preserves_real_compilation_failures(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-native-msvc-reject-") as root:
            folder = Path(root)
            source = folder / "broken.cpp"
            target = folder / "invalid"
            source.write_text("int main( { return 0; }\n")
            built = subprocess.run(msvc_cpp23_command(target, source), cwd=folder,
                                   capture_output=True, text=True, timeout=60)
            self.assertNotEqual(built.returncode, 0)
            self.assertFalse(windows_executable(target).exists())


if __name__ == "__main__":
    unittest.main()

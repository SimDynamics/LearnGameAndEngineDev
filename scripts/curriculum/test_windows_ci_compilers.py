"""Mantiene solamente la cadena nativa Windows y la ejecución WASI requerida.

Una prueba editorial de Python puede ejecutarse sin compilador. Las pruebas
C++ del motor usan MSVC en Windows; Clang se usa exclusivamente dentro del
WASI SDK para compilar WebAssembly ejecutado localmente por Wasmtime.
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"


class WindowsCompilerPolicy(unittest.TestCase):
    def test_no_obsolete_android_build_workflows(self):
        for name in ("android-code-format-compat.yml", "build-clang23-android.yml"):
            self.assertFalse((WORKFLOWS / name).exists(), name)

    def test_active_workflows_do_not_invoke_gcc_or_desktop_clang(self):
        # Match executable/runner tokens, not words like the curricular
        # explanation of a compiler or the "Windows" product description.
        unused = re.compile(r"(?<![A-Za-z0-9_])(?:g\+\+|clang\+\+|gcc)(?![A-Za-z0-9_])",
                            re.IGNORECASE)
        workflows = sorted(WORKFLOWS.glob("*.yml"))
        self.assertTrue(workflows)
        for path in workflows:
            with self.subTest(workflow=path.name):
                self.assertNotRegex(path.read_text(encoding="utf-8"), unused)

    def test_native_pins_and_separate_wasi_runner_remain(self):
        manifest = json.loads((ROOT / "toolchains/windows.versions.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["cpp"]["standard"], 23)
        self.assertTrue(manifest["visualStudio"]["msvcToolset"])
        self.assertTrue(manifest["visualStudio"]["windowsSdk"])
        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        self.assertIn("windows-x64-debug", {p["name"] for p in presets["configurePresets"]})
        wasi = (WORKFLOWS / "windows-wasi-execution.yml").read_text(encoding="utf-8")
        self.assertIn("Install-WasiTools.ps1", wasi)
        self.assertIn("Test-NativeCurricularCI.ps1", wasi)

    def test_real_rename_runtime_uses_msvc_not_unneeded_compilers(self):
        source = (ROOT / "scripts/curriculum/test_identifier_rename_execution.py").read_text(
            encoding="utf-8")
        self.assertIn('shutil.which("cl.exe")', source)
        self.assertIn('"/std:c++latest"', source)
        self.assertNotIn('shutil.which("g++")', source)
        self.assertNotIn('shutil.which("clang++")', source)


if __name__ == "__main__":
    unittest.main()

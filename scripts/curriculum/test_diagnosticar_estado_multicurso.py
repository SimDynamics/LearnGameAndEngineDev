"""Casos normales, varias fallas y conservación del diagnóstico por curso."""
from __future__ import annotations

from contextlib import redirect_stdout
from copy import deepcopy
import hashlib
from io import StringIO
import json
from pathlib import Path
import tempfile
import unittest

import diagnosticar_estado_multicurso as diagnostic
import stable_state_layout as layout
from test_stable_state_layout import fixture


class MultiCourseStateDiagnosticsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="state-diagnostic-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.paths = []
        for course in diagnostic.COURSES:
            for index in (1, 2):
                path = (self.root / diagnostic.RELATIVE / course
                        / "unidad" / f"lesson_ficticia_{index}.json")
                source = fixture()
                source["id"] = f"{course}__unidad__ficticia_{index}"
                prepared = layout.prepare_v2(source, "es", {"es": source})
                path.parent.mkdir(parents=True, exist_ok=True)
                self.write(path, prepared)
                self.paths.append(path)

    @staticmethod
    def write(path: Path, document: dict):
        path.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")

    @staticmethod
    def digest(path: Path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def test_two_courses_all_four_lessons_have_no_problems(self):
        result = diagnostic.inspect(self.root)
        self.assertEqual(result["checked"], 4)
        self.assertEqual(result["errors"], 0)
        self.assertEqual([x["course"] for x in result["courses"]],
                         list(diagnostic.COURSES))
        self.assertEqual([x["lessonsChecked"] for x in result["courses"]], [2, 2])
        self.assertFalse(result["globallyValidated"])
        self.assertFalse(result["filesChanged"])

    def test_collects_failures_from_both_courses_without_stopping_at_first(self):
        mutated = [self.paths[1], self.paths[2]]
        for path in mutated:
            doc = json.loads(path.read_text(encoding="utf-8"))
            doc["stateLayout"]["family"]["es"]["origin"][0]["blocks"][0]["sha256"] = "0" * 64
            self.write(path, doc)
        before = {p.as_posix(): self.digest(p) for p in self.paths}
        result = diagnostic.inspect(self.root)
        self.assertEqual(result["checked"], 4)
        self.assertEqual(result["errors"], 2)
        self.assertEqual([x["errors"] for x in result["courses"]], [1, 1])
        self.assertEqual([x["course"] for x in result["issues"]],
                         list(diagnostic.COURSES))
        for issue in result["issues"]:
            self.assertIn("This locale must keep its own historical origin", issue["message"])
            self.assertIn("p1 b1", issue["message"])
            self.assertIn("family.es.origin=", issue["message"])
        self.assertEqual(before, {p.as_posix(): self.digest(p) for p in self.paths})

    def test_corrupt_json_and_missing_course_are_not_false_successes(self):
        self.paths[0].write_text("{invalid-json", encoding="utf-8")
        result = diagnostic.inspect(self.root, ("cpp_fundamentals",))
        self.assertEqual(result["errors"], 1)
        self.assertEqual(result["checked"], 2)
        self.assertEqual(result["issues"][0]["path"],
                         self.paths[0].relative_to(self.root).as_posix())
        self.assertIn("JSON", result["issues"][0]["exception"].upper())
        result = diagnostic.inspect(self.root / "missing", ("programming_practice_tools",))
        self.assertEqual(result["errors"], 1)
        self.assertEqual(result["checked"], 0)
        self.assertEqual(result["issues"][0]["category"], "missing-content")

    def test_report_has_correct_exit_status_and_never_writes_assets(self):
        report_file = self.root / "reports" / "state-diagnostics.json"
        stdout = StringIO()
        with redirect_stdout(stdout):
            rc = diagnostic.main(["--root", str(self.root), "--output-json", str(report_file)])
        self.assertEqual(rc, 0)
        report = json.loads(report_file.read_text(encoding="utf-8"))
        self.assertEqual(report["checked"], 4)
        self.assertEqual(report["errors"], 0)
        self.assertIn("sin certificar", stdout.getvalue())
        bad = json.loads(self.paths[3].read_text(encoding="utf-8"))
        bad["stateLayout"]["family"]["es"]["origin"][0]["blocks"][0]["sha256"] = "0" * 64
        self.write(self.paths[3], bad)
        with redirect_stdout(StringIO()):
            rc = diagnostic.main(["--root", str(self.root),
                                  "--course", "programming_practice_tools",
                                  "--output-json", str(report_file)])
        self.assertEqual(rc, 1)
        self.assertEqual(json.loads(report_file.read_text(encoding="utf-8"))["errors"], 1)
        forbidden = (self.root / "app/src/main/assets/content/es/"
                     "programming/cpp_fundamentals/diagnostic.json")
        with self.assertRaises(SystemExit):
            diagnostic.main(["--root", str(self.root),
                             "--output-json", str(forbidden)])
        self.assertFalse(forbidden.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)

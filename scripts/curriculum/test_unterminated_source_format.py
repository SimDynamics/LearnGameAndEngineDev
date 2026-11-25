"""Strict lexers, conservative malformed tails and authenticated U1L4 history."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import code_format_compat as fmt
import stable_state_layout as state
import unterminated_source_format_fixture_source as fixture


class IncompleteLiteralLexicalTest(unittest.TestCase):
    def accepts(self, before, after):
        return fmt.require_source_format({"type": "code", "language": "cpp", "code": before},
                                         {"type": "code", "language": "cpp", "code": after})

    def rejects(self, before, after):
        with self.assertRaises(fmt.SourceFormatError):
            self.accepts(before, after)

    def test_every_ordinary_prefix_freezes_tail_and_preserves_strict_api(self):
        for prefix in ('"', "'", 'u8"', 'u"', 'U"', 'L"', "u8'", "u'", "U'", "L'"):
            for tail in ("unfinished;\n    return 0;\n}\n", "unfinished", "unfinished\\"):
                with self.subTest(prefix=prefix, tail=tail):
                    old = "int main(){auto text=" + prefix + tail
                    new = "int main() {\n    auto text = " + prefix + tail
                    self.assertTrue(self.accepts(old, new))
                    self.assertTrue(self.accepts(new, old))
                    self.assertEqual(fmt._cpp_format_tokens(old)[-1],
                                     "incomplete-literal-tail:" + prefix + tail)
                    with self.assertRaises(fmt.SourceFormatError):
                        fmt._cpp_tokens(old)

    def test_physical_newline_before_later_quote_freezes_all_remaining_bytes(self):
        old = 'int main(){auto text="broken\nreturn "later";}\n'
        new = 'int main() {\n    auto text = "broken\nreturn "later";}\n'
        self.assertTrue(self.accepts(old, new))
        self.assertEqual(fmt._cpp_format_tokens(old)[-1],
                         'incomplete-literal-tail:"broken\nreturn "later";}\n')
        self.rejects(old, new.replace('return "later"', 'return  "later"'))

    def test_incomplete_suffix_cannot_be_changed_extended_or_repaired(self):
        old = 'int main(){std::cout << "Listo;\n    return 0;\n}\n'
        safe = old.replace("int main(){", "int main() {\n")
        for new in (safe.replace('"Listo;', '"Listo ;'),
                    safe.replace("\n    return", "\n\n    return"),
                    safe.replace('"Listo;', '"Listo";'),
                    safe.replace("\n", "\r\n"), safe + " ", safe[:-1]):
            with self.subTest(new=new):
                self.rejects(old, new)
                self.rejects(new, old)

    def test_prefix_tokens_and_literal_adjacency_remain_protected(self):
        for old, new in (
                ('int x=0; L "broken', 'int x = 0; L"broken'),
                ("int x=0; 1 'broken", "int x = 0; 1'broken"),
                ('int x=0; // note\n"broken', 'int x = 0; // note "broken'),
                ('int x=0; "broken', 'int x = 1; "broken'),
                ('int x=0; "broken', 'int x = 0; u8"broken')):
            with self.subTest(old=old):
                self.rejects(old, new)

    def test_raw_strings_block_comments_and_cmake_do_not_gain_recovery(self):
        for suffix in ('R"tag(unfinished', 'u8R"(unfinished', '/* unfinished'):
            with self.subTest(suffix=suffix):
                self.rejects("int x=0;" + suffix, "int x = 0;\n" + suffix)
        with self.assertRaises(fmt.SourceFormatError):
            fmt.require_source_format(
                {"type": "code", "language": "cmake", "code": 'message("unfinished'},
                {"type": "code", "language": "cmake", "code": 'message ( "unfinished'})

    def test_complete_literals_keep_the_existing_tokens(self):
        for source in ('int x=0; "complete"_tag;', "int x=0; L'c';",
                       'int x=0; R"tag(line one\nline two)tag";',
                       'int x=0; "escaped \\" quote";'):
            with self.subTest(source=source):
                self.assertEqual(fmt._cpp_format_tokens(source), fmt._cpp_tokens(source))
                self.assertTrue(self.accepts(source, source.replace("int x=0;", "int x = 0;\n")))

    def test_splicing_and_live_ucn_boundaries_are_not_relaxed(self):
        for ending in ("\\\n", "\\\r\n"):
            old = 'int x=0; "broken' + ending + "tail"
            self.rejects(old, "\n" + old)
        self.rejects('int a\\u03B1=0; "broken', '\nint a\\u03B1=0; "broken')

    def test_opaque_tail_cannot_hide_preprocessing_or_position_queries(self):
        for name in ("__LINE__", "source_location", "__builtin_LINE",
                     "__builtin_COLUMN", "__builtin_source_location"):
            for old in ('int x=' + name + '; "broken',
                        'int x=0; "broken\nreturn ' + name + ";"):
                with self.subTest(source=old):
                    self.rejects(old, "\n" + old)
        for old in ('#if 0\n"broken\n#endif\nint main(){return __LINE__;}',
                    '#if 0\n"broken\n#endif\n#define X __LI ## NE__\nint main(){return X;}',
                    '%:if 0\n"broken\n%:endif\nint main(){}',
                    'int x=0; "broken # token',
                    'int x=0; "broken %: token'):
            with self.subTest(source=old):
                self.rejects(old, "\n" + old)


class IncompleteLiteralPublicationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.export_payloads()
        cls.before = state.parse_json(cls.payloads["before.json"].decode("utf-8"))
        cls.after = state.parse_json(cls.payloads["after.json"].decode("utf-8"))

    def test_published_bytes_owner_and_format_archive_are_exact(self):
        self.assertEqual(len(self.payloads), 11)
        self.assertEqual(fixture.verify_published_bytes("es", self.payloads["before.json"]),
                         self.payloads["before.json"])
        fixture.verify_format_only(self.before, self.after)
        old, new = state._blocks(self.before)[fixture.OWNER], state._blocks(self.after)[fixture.OWNER]
        self.assertEqual(old["type"], "code")
        self.assertNotIn("compileCheck", old)
        self.assertEqual(old["code"][old["code"].index('"'):],
                         new["code"][new["code"].index('"'):])
        self.assertEqual(fmt.source_projection(new, old), old)
        self.assertEqual(state.validate(self.before), state.validate(self.after))
        # The displayed old diagnostic remains author-owned; QA does not
        # fabricate updated line coordinates in a curricular block.
        for identity, block in state._blocks(self.before).items():
            if identity != fixture.OWNER:
                self.assertEqual(block, state._blocks(self.after)[identity])

    def test_resealed_negative_documents_fail_both_validation_paths(self):
        manifest = json.loads(self.payloads["manifest.json"])
        self.assertEqual({item["name"] for item in manifest["negative"]},
                         set(fixture.NEGATIVE_NAMES))
        for item in manifest["negative"]:
            with self.subTest(case=item["name"]):
                payload = self.payloads[item["path"]]
                self.assertEqual(item["sha256"], hashlib.sha256(payload).hexdigest())
                invalid = state.parse_json(payload.decode("utf-8"))
                self.assertEqual(invalid["stateLayout"]["editorial"][-1]["afterSha256"],
                                 state.fingerprint(state._blocks(invalid)[fixture.OWNER]))
                with self.assertRaises(state.LayoutError):
                    state.validate(invalid)
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(self.before, invalid)

    def test_further_prefix_formatting_keeps_the_original_archive(self):
        family = {locale: state.parse_json(fixture.published_bytes(locale).decode("utf-8"))
                  for locale in ("es", "en", "fr")}
        changed = deepcopy(self.after)
        state._blocks(changed)[fixture.OWNER]["code"] = (
            state._blocks(changed)[fixture.OWNER]["code"].replace("int main() {", "int main () {", 1))
        revised = state.prepare_v2(changed, "es", family, self.after)
        state.validate_transition(self.before, revised)
        self.assertEqual(revised["stateLayout"]["sourceFormatRevisions"],
                         self.after["stateLayout"]["sourceFormatRevisions"])
        self.assertEqual(state.validate(self.before), state.validate(revised))

    def test_incomplete_or_corrupt_publications_create_no_export(self):
        for locale in fixture.PUBLICATIONS:
            original = fixture.published_bytes(locale)
            with self.subTest(locale=locale), self.assertRaises(ValueError):
                fixture.verify_published_bytes(locale, original + b" ")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "fixtures"
            with mock.patch.object(fixture.subprocess, "check_output", side_effect=OSError("missing")):
                with self.assertRaises(RuntimeError):
                    fixture.export(output)
            self.assertFalse(output.exists())

    def test_existing_different_output_is_preserved_before_any_write(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "fixtures"
            conflict = output / "negative" / fixture.NEGATIVE_NAMES[-1]
            conflict = conflict.with_suffix(".json")
            conflict.parent.mkdir(parents=True)
            conflict.write_bytes(b"keep existing QA bytes")
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                with self.assertRaises(FileExistsError):
                    fixture.export(output)
            self.assertEqual(conflict.read_bytes(), b"keep existing QA bytes")
            self.assertEqual({p.relative_to(output).as_posix()
                              for p in output.rglob("*") if p.is_file()},
                             {"negative/borrowed_owner.json"})


if __name__ == "__main__":
    unittest.main()

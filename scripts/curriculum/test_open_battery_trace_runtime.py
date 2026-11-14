"""Compile the production-generated witness; preserve the caller's stderr.

All sources and executables live in a temporary QA directory. No lessons,
student drafts, dependencies or progress profiles are modified.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

NATIVE_ROOT = Path(__file__).resolve().parents[2] / "native"

GENERATOR = r'''
#include "core/open_battery_check.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
using namespace lgaed::curriculum;
constexpr std::string_view library=R"cpp(
bool intentar_ajuste(int valor, int inferior, int superior, int& ajuste) {
    if(valor<-20||valor>20||inferior<-20||inferior>20||
       superior<-20||superior>20||inferior>superior)return false;
    ajuste=valor<inferior?inferior-valor:valor>superior?superior-valor:0;
    return true;
}
)cpp";
int main(int argc,char** argv) {
    if(argc==4&&std::string_view(argv[1])=="decode") {
        std::ifstream input(argv[2],std::ios::binary);
        if(!input)return 2;
        const std::string raw{std::istreambuf_iterator<char>{input},{}};
        const auto calls=open_battery_parse_calls(raw);
        const auto visible=open_battery_student_stderr(raw);
        std::ofstream output(argv[3],std::ios::binary);
        output.write(visible.data(),static_cast<std::streamsize>(visible.size()));
        if(!output)return 3;
        if(!calls) {std::cout<<"INVALID\n";return 0;}
        for(const auto& call:*calls)
            std::cout<<call.value<<','<<call.lower<<','<<call.upper<<','<<call.initial<<'\n';
        return 0;
    }
    if(argc!=2)return 2;
    const std::string_view mode=argv[1];
    if(mode=="plain") {std::cout<<library;return 0;}
    const auto mutation=mode=="baseline"?OpenBatteryMutation::baseline:
        mode=="destination"?OpenBatteryMutation::corrupt_destination:
        OpenBatteryMutation::invert_acceptance;
    const std::optional<OpenBatteryCall> target=mode=="baseline"?std::nullopt:
        std::optional<OpenBatteryCall>{mode=="old"?OpenBatteryCall{2,3,7,77}:
                                                     OpenBatteryCall{3,3,7,59}};
    const auto result=open_battery_mutated_library(library,"intentar_ajuste",mutation,target);
    if(!result)return 4;
    std::cout<<*result;
    return 0;
}
'''

DRIVER = r'''
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <string>
bool intentar_ajuste(int valor, int inferior, int superior, int& ajuste);
struct Grouped final:std::numpunct<char> {
    char do_thousands_sep() const override {return '_';}
    std::string do_grouping() const override {return "\1";}
};
int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"default";
    if(mode=="limits") {
        int result=std::numeric_limits<int>::min();
        const bool accepted=intentar_ajuste(std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max(),-20,result);
        return accepted||result!=std::numeric_limits<int>::min()?2:0;
    }
    int failures=0;
    bool preserved=true;
    for(int index=0;index<2;++index) {
        if(mode=="prefix")std::cerr<<"Caso: ";
        if(mode=="format")
            std::cerr<<std::hex<<std::showbase<<std::showpos<<std::uppercase
                     <<std::setfill('*')<<std::setw(30)<<std::setprecision(3);
        if(mode=="locale")std::cerr.imbue(std::locale(std::locale::classic(),new Grouped));
        const auto flags=std::cerr.flags();
        const auto width=std::cerr.width();
        const auto precision=std::cerr.precision();
        const auto fill=std::cerr.fill();
        const auto locale=std::cerr.getloc();
        int result=index==0?77:59;
        const bool accepted=intentar_ajuste(index==0?2:3,3,7,result);
        preserved=preserved&&flags==std::cerr.flags()&&width==std::cerr.width()&&
            precision==std::cerr.precision()&&fill==std::cerr.fill()&&locale==std::cerr.getloc();
        const bool correct=accepted&&result==(index==0?1:0);
        if(!correct)++failures;
        std::cerr<<(correct?"OK\n":"FALLO\n");
    }
    std::cout<<"Estado="<<preserved<<";Fallos="<<failures<<'\n';
    return !preserved?3:failures?1:0;
}
'''


class OpenBatteryTraceRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workspace = tempfile.TemporaryDirectory(prefix="lgaed-trace-qa-")
        cls.addClassCleanup(cls.workspace.cleanup)
        cls.root = Path(cls.workspace.name)
        cls.cxx = os.environ.get("CXX", "cl" if os.name == "nt" else "g++")
        if shutil.which(cls.cxx) is None:
            raise RuntimeError(f"Required C++ compiler not found: {cls.cxx}")
        cls.msvc = Path(cls.cxx).name.lower() in {"cl", "cl.exe"}
        generator = cls.root / "generator.cpp"
        generator.write_text(GENERATOR, encoding="utf-8")
        cls.generator = cls.compile([generator], "generator")
        driver = cls.root / "driver.cpp"
        driver.write_text(DRIVER, encoding="utf-8")
        cls.binaries = {}
        for name in ("plain", "baseline", "old", "acceptance", "destination"):
            generated = cls.run_tool([str(cls.generator), name])
            library = cls.root / f"{name}.cpp"
            library.write_bytes(generated.stdout)
            cls.binaries[name] = cls.compile([library, driver], name)

    @classmethod
    def run_tool(cls, args: list[str]) -> subprocess.CompletedProcess:
        result = subprocess.run(args, cwd=cls.root, capture_output=True, timeout=60)
        if result.returncode:
            raise RuntimeError(f"Command failed: {args!r}\n" +
                               (result.stdout + result.stderr).decode("utf-8", errors="replace"))
        return result

    @classmethod
    def compile(cls, sources: list[Path], name: str) -> Path:
        exe = cls.root / (name + (".exe" if os.name == "nt" else ""))
        if cls.msvc:
            flags = ["/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
                     "/permissive-", "/utf-8", "/I", str(NATIVE_ROOT)]
            args = [cls.cxx, *flags, *map(str, sources), f"/Fe:{exe}"]
        else:
            args = [cls.cxx, "-std=c++23", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-I", str(NATIVE_ROOT), *map(str, sources), "-o", str(exe)]
        cls.run_tool(args)
        return exe

    def execute(self, variant: str, mode: str) -> subprocess.CompletedProcess:
        return subprocess.run([str(self.binaries[variant]), mode], cwd=self.root,
                              capture_output=True, timeout=10)

    def decode(self, raw: bytes) -> tuple[list[tuple[int, ...]] | None, bytes]:
        trace, visible = self.root / "trace.bin", self.root / "student.bin"
        trace.write_bytes(raw)
        result = self.run_tool([str(self.generator), "decode", str(trace), str(visible)])
        text = result.stdout.decode("ascii").strip()
        calls = None if text == "INVALID" else [tuple(map(int, line.split(",")))
                                                 for line in text.splitlines()]
        return calls, visible.read_bytes()

    def check_preserved(self, mode: str) -> None:
        original = self.execute("plain", mode)
        wrapped = self.execute("baseline", mode)
        self.assertEqual(original.returncode, 0)
        self.assertEqual(wrapped.returncode, 0, wrapped.stdout)
        calls, visible = self.decode(wrapped.stderr)
        self.assertEqual(calls, [(2, 3, 7, 77), (3, 3, 7, 59)])
        self.assertEqual(visible, original.stderr)
        self.assertEqual(wrapped.stdout, original.stdout)

    def test_default_reporting_is_unchanged(self):
        self.check_preserved("default")

    def test_message_without_a_newline_does_not_hide_a_real_call(self):
        self.check_preserved("prefix")

    def test_hex_showbase_showpos_and_width_do_not_change_private_integers(self):
        self.check_preserved("format")

    def test_locale_grouping_does_not_change_private_integers(self):
        self.check_preserved("locale")

    def test_int_extremes_are_exact_in_private_evidence(self):
        response = self.execute("baseline", "limits")
        self.assertEqual(response.returncode, 0)
        calls, visible = self.decode(response.stderr)
        self.assertEqual(calls, [(-(2**31), 2**31 - 1, -20, -(2**31))])
        self.assertEqual(visible, b"")

    def test_all_mutations_remain_observable_after_a_partial_message(self):
        baseline = self.execute("baseline", "prefix")
        _, clean = self.decode(baseline.stderr)
        for variant in ("old", "acceptance", "destination"):
            with self.subTest(variant=variant):
                response = self.execute(variant, "prefix")
                self.assertEqual(response.returncode, 1, response.stdout)
                calls, visible = self.decode(response.stderr)
                self.assertEqual(calls, [(2, 3, 7, 77), (3, 3, 7, 59)])
                self.assertNotEqual(visible, clean)
                self.assertEqual(visible.count(b"FALLO"), 1)

    def test_crlf_and_no_final_newline_preserve_every_student_byte(self):
        raw = (b"Aviso: LGAED_OPEN_BATTERY_CALL:3,3,7,59\r\n"
               b"OK\r\nFin sin salto")
        calls, visible = self.decode(raw)
        self.assertEqual(calls, [(3, 3, 7, 59)])
        self.assertEqual(visible, b"Aviso: OK\r\nFin sin salto")

    def test_malformed_marker_cannot_erase_a_student_message(self):
        for data in (b"3,3,7", b"3,3,7,no", b"2147483648,3,7,59", b"3,3,7,59,0"):
            with self.subTest(data=data):
                raw = b"LGAED_OPEN_BATTERY_CALL:" + data + b"\nFin\n"
                calls, visible = self.decode(raw)
                self.assertIsNone(calls)
                self.assertEqual(visible, raw)


if __name__ == "__main__":
    unittest.main()

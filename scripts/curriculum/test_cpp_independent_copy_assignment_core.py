"""Exercise the reusable native U6L7 authoring and independent runtime probe.

The C++ core must accept different correct method bodies, reject all changes
outside the original operator body, and reject semantic lookalikes at runtime.
All tests compile the original published source with a private main, not a
copied training example. No Windows profile or historical state is accessed.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import tempfile

import test_cpp_copy_assignment_independence as qa


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--windows", action="store_true")
    args = parser.parse_args()

    lesson = json.loads(qa.LESSON.read_text(encoding="utf-8"))
    locations = [(i, j) for i, page in enumerate(lesson["stateLayout"]["current"])
                 for j, owner in enumerate(page["blocks"]) if owner == qa.OWNER]
    assert len(locations) == 1
    i, j = locations[0]
    original = lesson["pages"][i]["blocks"][j]["initialCode"]
    signature = "Celda& Celda::operator=(const Celda& otra) {"
    opening = original.index(signature) + len(signature)
    closing = original.index("\n}\nbool Celda::poner(int valor)", opening) + 1
    prefix = original[:opening]
    suffix = original[closing:]

    with tempfile.TemporaryDirectory(prefix="lgaed-u6l7-native-core-") as folder:
        directory = Path(folder)
        exe = ".exe" if args.windows else ""
        generator = directory / ("copy-core-generator" + exe)
        compiled = qa.compile_source(args.compiler, qa.ROOT /
            "native/core/cpp_independent_copy_assignment_check_tests.cpp",
            generator, args.windows)
        if compiled.returncode:
            raise AssertionError(
                "Cannot compile the reusable native U6L7 source gate:\n" +
                compiled.stdout + compiled.stderr
            )

        original_path = directory / "published-original.cpp"
        original_path.write_bytes(original.encode("utf-8"))
        correct = {**qa.VARIANTS,
                   "equivalent_formatting": qa.VARIANTS["candidate_swap"],
                   "quoted_guard_examples": (
                       '\n    // std::exit(0), __FILE__, stderr and Watch are only comments.\n'
                       '    const char* text = R"memo(lgaed_copy_qa::Watch '
                       'std::terminate() __COUNTER__)memo";\n'
                       '    (void)text;\n' + qa.VARIANTS["candidate_swap"])}
        candidates = {**correct, **qa.REJECTIONS}
        for name, body in candidates.items():
            candidate = prefix + body + suffix
            if name == "equivalent_formatting":
                candidate = candidate.replace("class Celda {",
                                              "class Celda   {", 1)
            source_path = directory / (name + "-student.cpp")
            source_path.write_bytes(candidate.encode("utf-8"))
            private_path = directory / (name + "-probe.cpp")
            checked = qa.run([str(generator), str(original_path),
                              str(source_path), str(private_path)])
            if checked.returncode:
                raise AssertionError(
                    (name, "legitimate method edit rejected structurally",
                     checked.stdout, checked.stderr)
                )
            binary = directory / (name + "-runtime" + exe)
            built = qa.compile_source(args.compiler, private_path,
                                       binary, args.windows)
            if built.returncode:
                raise AssertionError(
                    (name, "private compiled program invalid",
                     built.stdout, built.stderr)
                )
            executed = qa.run([str(binary)], seconds=15)
            accepted = (executed.returncode == 0 and
                        executed.stdout == "" and executed.stderr == qa.MARKER)
            if accepted != (name in correct):
                raise AssertionError(
                    (name, "unexpected native copy semantic verdict",
                     executed.returncode, executed.stdout, executed.stderr)
                )
            print(f"{args.compiler}: native {name}: "
                  f"{'PASS' if accepted else 'REJECT'}", flush=True)

        positive = prefix + qa.VARIANTS["candidate_swap"] + suffix
        protected_rejections = {
            "changed_initializer": positive.replace(
                "std::make_unique<int>(4)", "std::make_unique<int>(5)", 1),
            "changed_original_copy_constructor": positive.replace(
                "Celda(const Celda& otra);", "Celda(const Celda&) = delete;", 1),
            "forged_main_stdout": positive.replace(
                'std::cout << "Inicio:";', 'std::cout << "Fingido:";', 1),
            "changed_assignment_signature": positive.replace(
                signature, "Celda& Celda::operator=(Celda& otra) {", 1),
            "preprocessor_within_student_body": (
                prefix + "\n#define private public\n" +
                qa.VARIANTS["candidate_swap"] + suffix),
        }
        # Source-side spoofing must fail before a private binary is compiled.
        # Do not execute intentionally hostile student code or read freed memory.
        hostile_body = {
            "spoof_exit": 'std::fputs("' + qa.MARKER +
                          '",stderr); std::exit(0);',
            "spoof_quick_exit": "::quick_exit(0);",
            "spoof_function_pointer": "auto done=&std::exit; done(0);",
            "spoof_counter_reset": "lgaed_copy_qa::Watch::created=0;",
            "spoof_failure_suppression": "lgaed_copy_qa::Watch::fail_next=false;",
            "spoof_stderr": 'std::cerr << "' + qa.MARKER + '";',
            "spoof_stdout": 'std::printf("' + qa.MARKER + '");',
            "spoof_stdout_redirect": 'std::freopen("out.txt","w",stderr);',
            "spoof_longjmp": "std::longjmp(jump,1);",
            "spoof_position": "const auto where=__FILE__; (void)where;",
            "spoof_build_counter": "const auto n=__COUNTER__; (void)n;",
            "spoof_preprocessor_digraph": "%:define OVERRIDE 1",
        }
        for name, body in hostile_body.items():
            protected_rejections[name] = prefix + "\n    " + body + "\n" + suffix
        for name, candidate in protected_rejections.items():
            assert candidate != positive, name
            path = directory / (name + "-student.cpp")
            path.write_bytes(candidate.encode("utf-8"))
            output = directory / (name + "-probe.cpp")
            checked = qa.run([str(generator), str(original_path),
                              str(path), str(output)])
            if checked.returncode == 0 or output.exists():
                raise AssertionError(
                    (name, "modified protected C++ source was approved",
                     checked.stdout, checked.stderr)
                )
            print(f"{args.compiler}: native {name}: PROTECTED", flush=True)

    print("PASS U6L7 reusable lexical owner, independent C++ compiler probe, "
          "four equivalent variants, five defective variants and "
          "blocked private witness/source edits. Published lesson unchanged.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL U6L7 native authoring and semantic probe: {error}",
              file=sys.stderr)
        sys.exit(1)

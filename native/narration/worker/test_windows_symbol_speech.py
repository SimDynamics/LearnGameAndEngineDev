"""Compile and run the actual Windows speech frontend without loading Kokoro.

Usage: python native/narration/worker/test_windows_symbol_speech.py --kotlinc PATH
Uses temporary production-generated sources, never alters Android or recordings.
"""
from pathlib import Path
import argparse
import importlib.util
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
WORKER = ROOT / 'native/narration/worker'
APP = ROOT / 'app/src/main/java/com/learngraphicsprogramming/app/narration'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--kotlinc', default=shutil.which('kotlinc'))
    parser.add_argument('--java', default=shutil.which('java'))
    args = parser.parse_args()
    if not args.kotlinc or not args.java:
        raise RuntimeError('Kotlin compiler and Java are required; no sources were changed')
    spec = importlib.util.spec_from_file_location('windows_speech_build', WORKER / 'build_worker.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory(prefix='lgaed-symbol-speech-') as temporary:
        folder = Path(temporary)
        # Use the application's actual platform adapters, including its real
        # mathematical parser. Generate into this disposable directory only.
        module.OUT = folder / 'host'
        generated = module.host_sources()
        names = {'SpanishTechnicalSpeech.kt', 'SpanishMathSpeech.kt',
                 'ManualEnglishTerms.kt', 'ManualEnglishSpeech.kt',
                 'MathExpression.kt', 'MathReadingAtoms.kt', 'MathTextSemantics.kt',
                 'MathLatexSerializer.kt', 'MathLatexParser.kt', 'MathUiMessage.kt',
                 'WindowsNarrationSpeechRules.kt'}
        dependencies = [path for path in generated if path.name in names]
        if {path.name for path in dependencies} != names:
            raise RuntimeError('Missing production frontend dependency')
        # The semicolon helper shares a source file with unrelated multilingual
        # presentation code. Compile its complete original object unchanged.
        original = (APP / 'ProgrammingTechnicalRanges.kt').read_text(encoding='utf-8-sig')
        boundary = '/** Presentation-only C++ ranges over the original text, shared by all programming lessons. */'
        if original.count(boundary) != 1:
            raise RuntimeError('Canonical semicolon helper boundary changed')
        semicolon = folder / 'TechnicalSemicolonContext.kt'
        semicolon.write_text(original.split(boundary)[0], encoding='utf-8')
        fixture = folder / 'WindowsSymbolSpeechTest.kt'
        fixture.write_text((WORKER / 'test_windows_symbol_speech.kt.in').read_text(encoding='utf-8-sig'), encoding='utf-8')
        jar = folder / 'symbol-speech.jar'
        command = [args.kotlinc, '-jvm-target', '17', '-include-runtime', '-d', str(jar),
                   *map(str, dependencies), str(semicolon), str(fixture)]
        subprocess.run(command, check=True, cwd=ROOT)
        subprocess.run([args.java, '-jar', str(jar)], check=True, cwd=ROOT)


if __name__ == '__main__':
    main()

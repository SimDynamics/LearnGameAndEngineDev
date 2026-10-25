"""Compile and run the actual Windows speech frontend without loading Kokoro.

Usage: python native/narration/worker/test_windows_symbol_speech.py --kotlinc PATH
Uses temporary generated sources, never alters Android or recordings.
"""
from pathlib import Path
import argparse
import importlib.util
import re
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
    canonical = APP / 'SpanishTechnicalSpeech.kt'
    generated = module.windows_reading_source('SpanishTechnicalSpeech', canonical.read_text(encoding='utf-8-sig'))
    dependencies = [APP / 'ManualEnglishTerms.kt', APP / 'ManualEnglishSpeech.kt']
    # The helper's canonical filename is not part of the pronunciation contract.
    matches = [path for path in APP.glob('*.kt') if re.search(r'\bobject\s+TechnicalSemicolonContext\b', path.read_text(encoding='utf-8-sig'))]
    if len(matches) != 1:
        raise RuntimeError('Cannot uniquely locate canonical TechnicalSemicolonContext')
    dependencies += matches
    with tempfile.TemporaryDirectory(prefix='lgaed-symbol-speech-') as temporary:
        folder = Path(temporary)
        source = folder / 'SpanishTechnicalSpeech.kt'
        source.write_text(generated, encoding='utf-8')
        jar = folder / 'symbol-speech.jar'
        command = [args.kotlinc, '-jvm-target', '17', '-include-runtime', '-d', str(jar),
                   str(source), *map(str, dependencies), str(WORKER / 'WindowsNarrationSpeechRules.kt'),
                   str(WORKER / 'test_windows_symbol_speech.kt')]
        subprocess.run(command, check=True, cwd=ROOT)
        subprocess.run([args.java, '-jar', str(jar)], check=True, cwd=ROOT)


if __name__ == '__main__':
    main()

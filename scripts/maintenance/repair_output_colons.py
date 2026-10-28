"""One-time guarded repair; removed after production frontend verification."""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / 'native/narration/worker'

def once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError('Changed/ambiguous anchor: ' + old[:100])
    return source.replace(old, new)

def save(path, text):
    path.write_text(text, encoding='utf-8', newline='\n')

def tests():
    path = WORKER / 'test_windows_symbol_speech.kt.in'
    code = path.read_text(encoding='utf-8-sig')
    extra = r'''
    // A backtick is display decoration, not proof that an output label is code.
    for (source in listOf(
        "Pulsa «Compilar, ejecutar y comprobar». El objetivo de esta actividad es `Llaves: 11`, con salto final opcional.",
        "La salida es `Llaves: 11`.",
        "El programa debe mostrar `Puntos: 3`.",
        "La salida esperada es `Objetos: 8`.")) {
        val output = SpanishTechnicalSpeech.prepare(source).text
        verify("dos puntos" !in output && Regex("(?:Llaves|Puntos|Objetos), ").containsMatchIn(output),
            "Output colon was named instead of paused: $source => $output")
        val pieces = mutableListOf<Triple<Int,Int,String>>()
        val traced = SpanishTechnicalSpeech.prepareObserved(source) { first, last, value ->
            pieces += Triple(first,last,value)
        }
        verify(normalized(traced.text) == normalized(output) &&
            normalized(pieces.joinToString("") { it.third }) == normalized(output),
            "Output colon changed observed speech: $source; $pieces")
        val colon = source.indexOf(':')
        verify(pieces.any { it.first == colon && it.second == colon + 1 && it.third == ", " } &&
            pieces.all { it.first in 0..source.length && it.second in it.first..source.length },
            "Output colon lost exact source ownership: $source; $pieces")
        val reading = SpanishTechnicalSpeech.prepareForReading(source) { _,_,_ -> }.text
        verify(reading == SpanishTechnicalSpeech.prepareForKokoro(source).text && "dos puntos" !in reading,
            "Foreground/background output-label speech diverged: $source")
    }
    for (source in listOf("El signo `:` separa los campos.", "La etiqueta `salida:` pertenece al código.",
        "`std::cout << \"Llaves: 11\";`", "`condicion ? uno : dos`")) {
        val spoken = SpanishTechnicalSpeech.prepare(source).text
        verify("dos puntos" in spoken, "A genuinely technical colon lost its name: $source => $spoken")
    }
    val namespaced = SpanishTechnicalSpeech.prepare("El objetivo es `std::cout << 11;`.").text
    verify("operador de resolución de ámbito" in namespaced,
        "Output context wrongly turned a real C++ expression into prose: $namespaced")
'''
    save(path, once(code, '    check(failures.isEmpty())', extra + '\n    check(failures.isEmpty())'))

def fix():
    path = WORKER / 'WindowsNarrationSpeechRules.kt'
    code = path.read_text(encoding='utf-8-sig')
    helper = r'''
    /** Only a prose-introduced displayed output value, never a code statement,
     * symbol explanation, URL, type qualification or a fenced source listing. */
    private val outputValueCue = Regex(
        "(?iu)(?:\\bsalida(?:\\s+(?:esperada|exacta|solicitada|del programa))?\\s*(?:es|será|:|=)|" +
        "\\bobjetivo(?:\\s+de\\s+(?:esta\\s+)?actividad)?\\s+es|" +
        "\\b(?:debe\\s+)?(?:mostrar|muestra|imprimir|imprime)|\\bresultado\\s+(?:esperado\\s+)?es)\\s*$")

    fun isDisplayedOutputValue(text: String, opening: Int, value: String): Boolean {
        if (!value.any(Char::isLetter) || ':' !in value || "::" in value || "://" in value ||
                value.any { it in "\\\"'`{}[]();#<>=?" }) return false
        return outputValueCue.containsMatchIn(text.substring((opening - 160).coerceAtLeast(0), opening))
    }
'''
    save(path, once(code, 'internal object WindowsNarrationSpeechRules {',
                   'internal object WindowsNarrationSpeechRules {' + helper))
    path = WORKER / 'build_worker.py'
    code = path.read_text(encoding='utf-8-sig')
    insertion = '''                    val code = text.substring(index + delimiter.length, end)
                    if (delimiter.length == 1 && WindowsNarrationSpeechRules.isDisplayedOutputValue(text, index, code)) {
                        // Visible output is prose/data. Keep each original UTF-16
                        // owner; only replace colon punctuation for a short pause.
                        output.append(' ')
                        onPiece?.invoke(index, index + delimiter.length, " ")
                        code.forEachIndexed { offset, character ->
                            val spoken = if (character == ':') ", " else character.toString()
                            output.append(spoken)
                            onPiece?.invoke(index + delimiter.length + offset, index + delimiter.length + offset + 1, spoken)
                        }
                        output.append(' ')
                        onPiece?.invoke(end, end + delimiter.length, " ")
                        observedChildren = onPiece != null
                        index = end + delimiter.length
                        changed = true
                        continue
                    }'''
    patch = '        # Display styling does not make an expected output label source code.\n'
    patch += '        code = exact(code, ' + repr('                    val code = text.substring(index + delimiter.length, end)') + ', ' + repr(insertion) + ')\n'
    save(path, once(code, '    if name == "SpanishTechnicalSpeech":\n',
                   '    if name == "SpanishTechnicalSpeech":\n' + patch))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('stage', choices=['tests','fix'])
    stage = parser.parse_args().stage
    (tests if stage == 'tests' else fix)()
    print('Guarded output-colon stage:', stage)

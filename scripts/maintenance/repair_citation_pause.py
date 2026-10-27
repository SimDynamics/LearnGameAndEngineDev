"""One-time, guarded Windows-only narration repair; removed after verification."""
from pathlib import Path
import argparse

ROOT = Path.cwd()
WORKER = ROOT / 'native/narration/worker'


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('Adaptation anchor changed: ' + before[:100])
    return text.replace(before, after, 1)


CITATION_RULE = r'''
    // A bounded prose citation is not a function argument list. Require an
    // author name and a bibliographical date, rather than hiding all brackets.
    private val citationEntry = Regex(
        """[ \t]*[\p{Lu}\p{Lt}][\p{L}\p{M}]+[\p{L}\p{M} \t.,'’&\-–]*,[ \t]*(?:[12][0-9]{3}[a-z]?|s\.[ \t]*f\.)(?:[ \t]*,[ \t]*(?:pp?\.|cap\.)[ \t]*[0-9]+(?:[–-][0-9]+)?)?[ \t]*"""
    )

    /** Exclusive end, only in prose. Call after explicit code/math/URL handling.
     * Adjacent calls, single-letter coordinates, unfinished groups and explicit
     * statement tails retain the canonical technical reading. */
    fun proseCitationEnd(text: String, index: Int): Int? {
        if (text.getOrNull(index) != '(' ||
            (index > 0 && !text[index - 1].isWhitespace())) return null
        val limit = minOf(text.length, index + 513)
        var closing = index + 1
        while (closing < limit && text[closing] != ')') {
            if (text[closing] in "(\r\n`\\\"{}[]") return null
            closing++
        }
        if (closing == limit) return null
        // A following C++ statement delimiter/body is not an author citation.
        var after = closing + 1
        while (after < text.length && text[after] in " \t") after++
        if (text.getOrNull(after)?.let { it in ";{[=+*/%<>" } == true) return null
        val entries = text.substring(index + 1, closing).split(';')
        if (entries.isEmpty() || entries.any { !citationEntry.matches(it) }) return null
        return closing + 1
    }

'''

CITATION_PASS = r'''            val citationEnd = WindowsNarrationSpeechRules.proseCitationEnd(text, index)
            if (citationEnd != null) {
                // Spoken punctuation only: a short opening pause, no bracket
                // names. Keep the author's words/year and their source owners.
                val content = text.substring(index + 1, citationEnd - 1)
                output.append(", ").append(content)
                if (onPiece != null) {
                    onPiece(index, index + 1, ", ")
                    onPiece(index + 1, citationEnd - 1, content)
                    onPiece(citationEnd - 1, citationEnd, "")
                    observedChildren = true
                }
                index = citationEnd
                changed = true
                continue
            }
            val technicalEnd = codeEnd(text, index)'''

ADAPTER = '''        # Recognize author/year citations before local technical detection.
        # The same production pass serves ordinary speech and word ownership.
        code = exact(code, "            val technicalEnd = codeEnd(text, index)", CITATION_PASS_VALUE)
        code = exact(code, "            if (text.getOrNull(next) in listOf('(', '{', '[')) {",
                     "            if (text.getOrNull(next) in listOf('(', '{', '[') &&\\n"
                     "                WindowsNarrationSpeechRules.proseCitationEnd(text, next) == null) {")
'''.replace('CITATION_PASS_VALUE', repr(CITATION_PASS))

REGRESSION = r'''
    // Real U1L5 feedback: the trailing reference follows a technical name.
    // It must not be consumed as main(author, year) by codeEnd().
    val feedback = "#include <iostream> permite usar los recursos de entrada y salida de esa cabecera; no muestra por sí misma un mensaje. En estos programas, la salida la producen las instrucciones con std::cout dentro de main (Stroustrup, 2024)."
    val citations = listOf(
        feedback,
        "main (Stroustrup, 2024).",
        "std::cout (Lippman, Lajoie y Moo, 2013).",
        "Texto (Stroustrup, 2024a, pp. 12–14).",
        "Texto (García et al., 2020; Stroustrup, 2024).",
        "Texto (Microsoft, s. f.).",
        "𐐀 En main (Stroustrup, 2024) continúa la explicación."
    )
    fun cleaned(value: String) = normalized(value).replace(Regex(" +([,.;:!?])"), "$1")
    for (citation in citations) {
        val opening = citation.indexOf('(')
        val closing = citation.indexOf(')', opening)
        val body = citation.substring(opening + 1, closing)
        val plain = SpanishTechnicalSpeech.prepare(citation).text
        verify("paréntesis" !in plain && '(' !in plain && ')' !in plain,
            "Citation delimiters were spoken instead of paused: $citation => $plain")
        verify(", $body" in plain, "Lost short citation pause, author or date: $plain")
        val observedPieces = mutableListOf<Triple<Int,Int,String>>()
        val observedCitation = SpanishTechnicalSpeech.prepareObserved(citation) { a,b,s ->
            observedPieces += Triple(a,b,s)
        }
        verify(observedCitation.text == plain, "Observed citation reading diverged: $citation")
        verify(cleaned(observedPieces.joinToString("") { it.third }) == plain,
            "Citation source ownership changed the prepared words: $citation")
        verify(observedPieces.any { it.first == opening && it.second == opening + 1 && it.third == ", " } &&
            observedPieces.any { it.first == opening + 1 && it.second == closing && it.third == body } &&
            observedPieces.any { it.first == closing && it.second == closing + 1 && it.third.isEmpty() },
            "Citation punctuation or author/year lost their original UTF-16 owners: $citation")
        verify(observedPieces.all { it.first in 0..citation.length && it.second in it.first..citation.length },
            "Citation introduced an invalid source range: $citation")
        val livePieces = mutableListOf<Triple<Int,Int,String>>()
        val live = SpanishTechnicalSpeech.prepareForReading(citation) { a,b,s -> livePieces += Triple(a,b,s) }
        val queued = SpanishTechnicalSpeech.prepareForKokoro(citation)
        verify(live.text == queued.text && "paréntesis" !in live.text && ", $body" in live.text,
            "Foreground/background citation preparation diverged: $citation => ${live.text}")
        verify(cleaned(livePieces.joinToString("") { it.third }) == live.text,
            "Kokoro citation owners no longer reconstruct prepared speech: $citation")
    }
    // Actual technical parentheses must remain audible, including a call whose
    // argument spelling looks like the author/year pair but belongs to code.
    for (technical in listOf("`main()`", "main (argc, argv);", "main(Stroustrup, 2024);",
            "main (Stroustrup, 2024);", "`main (Stroustrup, 2024)`", "`\"(Stroustrup, 2024)\"`")) {
        val result = SpanishTechnicalSpeech.prepare(technical).text
        verify("abre paréntesis" in result && "cierra paréntesis" in result,
            "Citation handling suppressed technical parentheses: $technical => $result")
    }
    for (mode in listOf("inline", "block")) {
        val formula = "(X, 2024)"
        verify(SpanishTechnicalSpeech.prepare(formula, mode).text == formula,
            "Citation handling changed a forced mathematical group")
    }
    for (untouched in listOf("(X, 2024)", "Texto (Stroustrup, sin fecha)", "Texto (Stroustrup, 2024",
            "https://example.org/(Stroustrup,2024)")) {
        verify(SpanishTechnicalSpeech.prepare(untouched).text == untouched,
            "Citation handling changed a non-citation or unfinished group: $untouched")
    }
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage', choices=('tests', 'fix'), required=True)
    args = parser.parse_args()
    if args.stage == 'tests':
        path = WORKER / 'test_windows_symbol_speech.kt.in'
        text = path.read_text(encoding='utf-8-sig')
        text = replace_once(text, '    check(failures.isEmpty())', REGRESSION + '\n    check(failures.isEmpty())')
        path.write_text(text, encoding='utf-8', newline='\n')
    else:
        path = WORKER / 'WindowsNarrationSpeechRules.kt'
        rules = path.read_text(encoding='utf-8-sig')
        rules = replace_once(rules, '    /** Called only after technical/math preparation.',
                             CITATION_RULE + '    /** Called only after technical/math preparation.')
        path.write_text(rules, encoding='utf-8', newline='\n')
        path = WORKER / 'build_worker.py'
        code = path.read_text(encoding='utf-8-sig')
        code = replace_once(code, '        # Windows-only labels; retain canonical parsing and source ownership.\n',
                            ADAPTER + '        # Windows-only labels; retain canonical parsing and source ownership.\n')
        path.write_text(code, encoding='utf-8', newline='\n')
    print('Applied guarded citation stage:', args.stage)


if __name__ == '__main__':
    main()

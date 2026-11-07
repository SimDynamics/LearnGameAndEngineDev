package lgaed.narration.windows

import com.learngraphicsprogramming.app.narration.*

/** Bookmark authentication only: exact projection from 9355c1e, never synthesis or curriculum planning. */
internal object WindowsLegacyTextSemantics {
    fun projection(field: WindowsTextSemantics.Field, language: NarrationLanguage): NarrationSourceProjection.Projection {
        val output = StringBuilder().append(ManualEnglishSpeech.PREPARED)
        val owners = mutableListOf(ReadingTextRange(0,0))
        fun append(start: Int, end: Int, role: String, reading: String) {
            if (start == end) return
            val text = field.source.substring(start,end)
            val technical = reading == "technical" || reading == "auto" && role in setOf("cpp","string")
            val math = reading == "auto" && role == "math"
            val piece = when {
                technical -> NarrationSourceProjection.semanticCode(text,language)
                math -> NarrationSourceProjection.forLanguage(text,"inline",language) ?: error("Unprojectable mathematical semantic span")
                else -> {
                    val shown = StringBuilder(); val origins = mutableListOf<ReadingTextRange>(); var cursor = 0
                    while (cursor < text.length) {
                        // Existing inline delimiters are hidden presentation,
                        // never a reason to re-enter technical inference.
                        if (text[cursor] == '`' || text.startsWith("**",cursor)) {
                            cursor += if (text[cursor] == '`') 1 else 2; continue
                        }
                        val width = Character.charCount(text.codePointAt(cursor)); val source = ReadingTextRange(cursor,cursor+width)
                        repeat(width) { shown.append(text[cursor+it]); origins += source }; cursor += width
                    }
                    NarrationSourceProjection.Projection(shown.toString(),origins)
                }
            }
            val prefix = if (piece.text.startsWith(ManualEnglishSpeech.PREPARED)) 1 else 0
            output.append(piece.text.substring(prefix))
            owners += piece.owners.drop(prefix).map { if (it.endExclusive > it.start) ReadingTextRange(it.start+start,it.endExclusive+start) else it }
        }
        var cursor = 0
        for (span in field.spans) {
            append(cursor,span.start,"prose","natural");append(span.start,span.end,span.role,span.reading);cursor=span.end
        }
        append(cursor,field.source.length,"prose","natural")
        return NarrationSourceProjection.Projection(output.toString(),owners)
    }
}

package com.learngraphicsprogramming.app.narration

/** Scoped code pronunciation, observed over the original narration source. */
internal object EnglishTechnicalSpeech {
    data class QualifiedOutput(
        val start: Int,
        val endExclusive: Int,
        val stdStart: Int,
        val stdEndExclusive: Int,
        val coutStart: Int,
        val coutEndExclusive: Int
    )

    // These are Kokoro/Misaki's existing native phoneme symbols, not replacement text.
    // The reviewed C + out reading remains shared with Spanish and French.
    const val STD_PHONES = "ˈɛs tˈi dˈi"
    val coutPhones: String get() = ManualEnglishPronunciations.phones.getValue("cout")
    const val CACHE_REVISION = "english-qualified-cout-letter-names-v1"

    private val qualified = Regex("(?<![\\p{L}\\p{M}\\p{N}_])(?<std>std)\\s*::\\s*(?<cout>cout)(?![\\p{L}\\p{M}\\p{N}_])")
    private val urls = Regex("(?:https?://|www\\.)\\S+", RegexOption.IGNORE_CASE)

    fun qualifiedOutputMatches(text: String): List<QualifiedOutput> {
        val protected = urls.findAll(text).map { it.range }.toList()
        return qualified.findAll(text).filter { match -> protected.none { match.range.first in it } }.map { match ->
            val std = match.groups["std"]!!.range
            val cout = match.groups["cout"]!!.range
            QualifiedOutput(match.range.first, match.range.last + 1,
                std.first, std.last + 1, cout.first, cout.last + 1)
        }.toList()
    }
}

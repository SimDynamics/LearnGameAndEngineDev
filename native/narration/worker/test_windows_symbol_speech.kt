package com.learngraphicsprogramming.app.narration

/** Exercise the real Windows-generated normalizer, not replacement-string stubs. */
fun main() {
    fun normalized(text: String) = text.replace(Regex("\\s+"), " ").trim()
    val cases = listOf(
        "`\"\"`" to "comilla doble comilla doble",
        "\"\"" to "comilla doble comilla doble",
        "`//`" to "doble diagonal",
        "//" to "doble diagonal",
        "`\"//\"`" to "comilla doble doble diagonal comilla doble",
        "`'a'`" to "abre comilla simple a cierra comilla simple"
    )
    for ((source, expected) in cases) {
        check(normalized(SpanishTechnicalSpeech.prepare(source).text) == expected) { "Wrong speech for $source: ${SpanishTechnicalSpeech.prepare(source).text}" }
        val owners = mutableListOf<Triple<Int,Int,String>>()
        val observed = SpanishTechnicalSpeech.prepareObserved(source) { start, end, spoken ->
            check(start in 0..source.length && end in start..source.length)
            owners += Triple(start,end,spoken)
        }
        check(normalized(observed.text) == expected)
        check(normalized(owners.joinToString("") { it.third }) == expected) { "Observed and spoken output diverged: $source $owners" }
    }
    val statement = "std::cout << \"Hola\";"
    val spoken = SpanishTechnicalSpeech.prepare(statement).text
    check(Regex("comilla doble").findAll(spoken).count() == 2 && "abre comilla doble" !in spoken && "cierra comilla doble" !in spoken)
    val prose = "Fuera de un literal, // comienza un comentario que llega hasta el final de la línea."
    check("doble diagonal" in SpanishTechnicalSpeech.prepare(prose).text)
    val url = "https://example.org/a"
    check(SpanishTechnicalSpeech.prepare(url).text == url)
    check(SpanishTechnicalSpeech.prepare("a / b", "inline").text == "a / b")
    check("doble diagonal" !in SpanishTechnicalSpeech.prepare("`/`").text)
    check("ene:" in SpanishTechnicalSpeech.prepare("la letra n: juntas").text)
    val source = "`\"//\"`"
    val owners = mutableListOf<Triple<Int,Int,String>>()
    val prepared = SpanishTechnicalSpeech.prepareForReading(source) { a,b,s -> owners += Triple(a,b,s) }
    check("doble diagonal" in prepared.text && owners.all { it.first in 0..source.length && it.second in it.first..source.length })
    check(prepared.text == SpanishTechnicalSpeech.prepareForKokoro(source).text) { "Foreground/background preparation diverged" }
    println("PASS Windows technical speech: delimiters, empty literals, double diagonal in prose/code/strings, single quotes/slashes, URLs, math, named letters, provenance and Kokoro preparation")
}

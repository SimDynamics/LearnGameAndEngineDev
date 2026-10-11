package com.learngraphicsprogramming.app.narration.phonemes

import java.io.File
import java.text.Normalizer
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** Goldens retained from FluidAudio SpanishG2PTests.swift at the pinned Apache-2.0 revision. */
class SpanishFluidG2PTest {
    @Test fun upstreamSentenceGoldensMatchExactlyIncludingPhraseAllophones() {
        val cases = mapOf(
            "Por ello, cuando se inventó, el lápiz ganó muchos amigos." to "poɾ ˈeʎo, kwˌando se ˌimbentˈo, el lˈapiθ ɣanˈo mˈuʧos amˈiɣos.",
            "Estas parejas podrían elegir planificar la adopción de un bebé." to "ˈestas paɾˈexas poðɾˈian ˌelexˈiɾ plˌanifikˈaɾ la ˌaðopθjˈon de ˈum beβˈe.",
            "¿Dónde está el capítulo? Llegó al río con mucho tiempo." to "¿dˈonde estˈa el kapˈitulo? ʎeɣˈo al rˈio kon mˈuʧo tjˈempo.",
            "Hoy es muy fácil, generalmente." to "ˈoɪ ˈes mˈuj fˈaθil, xˌeneɾˈalmˈente."
        )
        cases.forEach { (text, expected) -> assertEquals(text, expected, SpanishFluidG2P.phonemizeNormalized(text)) }
    }

    @Test fun upstreamSpellingStressAndDiphthongGoldensMatchExactly() {
        val cases = mapOf("casa" to "kˈasa", "papel" to "papˈel", "árbol" to "ˈaɾbol", "tiempo" to "tjˈempo",
            "causa" to "kˈWsa", "rey" to "rˈA", "cliente" to "kliˈɛnte", "prohibido" to "pɾˌoibˈido")
        cases.forEach { (text, expected) -> assertEquals(text, expected, SpanishFluidG2P.phonemizeWord(text)) }
    }

    @Test fun upstreamFunctionWordAndAcronymGoldensMatchExactly() {
        val cases = mapOf("la vida de los gatos" to "la βˈiða ðe los ɣˈatos",
            "un vaso, con dos gatos" to "ˈum bˈaso, kon dˈos ɣˈatos", "tengo que." to "tˈɛŋɡo kˈe.",
            "la BBC" to "la βˌeβˌeθˈe", "¡NO, HOLA!" to "¡nˈo, ˈola!", "la ONU" to "la ˈonu",
            "Él" to "ˈel", "la administración" to "la ˌadminˌistɾaθjˈon")
        cases.forEach { (text, expected) -> assertEquals(text, expected, SpanishFluidG2P.phonemizeNormalized(text)) }
    }

    @Test fun lexiconPreservesInternalPhonesAndOnlyAdaptsWordInitialAllophones() {
        val lexicon = mapOf("web" to "wˈeb", "bueno" to "bwˈeno")
        assertEquals("la wˈeb", SpanishFluidG2P.phonemize("la web", lexicon))
        assertEquals("mˈuj βwˈeno", SpanishFluidG2P.phonemize("muy bueno", lexicon))
    }

    @Test fun productionFrontendVerbalizesNumbersAndMathInsteadOfDroppingDigits() {
        assertEquals(SpanishFluidG2P.phonemize("doce casas"), SpanishFluidG2P.phonemize("12 casas"))
        assertEquals(SpanishFluidG2P.phonemize("cero coma cero cinco litros"), SpanishFluidG2P.phonemize("0,05 litros"))
        assertEquals(SpanishFluidG2P.phonemize("menos tres"), SpanishFluidG2P.phonemize("-3"))
        assertEquals(SpanishFluidG2P.phonemize("tres cuartos"), SpanishFluidG2P.phonemize("3/4"))
        assertEquals(SpanishFluidG2P.phonemize("equis al cuadrado"), SpanishFluidG2P.phonemize("x²"))
        assertEquals(SpanishFluidG2P.phonemize("raíz cuadrada de nueve"), SpanishFluidG2P.phonemize("√9"))
    }

    @Test fun temperatureScalesAndAnglesAreSpokenWithoutDroppingTheirUnits() {
        assertEquals(SpanishFluidG2P.phonemize("menos tres grados Celsius"),
            SpanishFluidG2P.phonemize("-3 °C"))
        assertEquals(SpanishFluidG2P.phonemize("cinco grados Celsius"),
            SpanishFluidG2P.phonemize("5°C"))
        assertEquals(SpanishFluidG2P.phonemize("treinta y dos grados Fahrenheit"),
            SpanishFluidG2P.phonemize("32 °F"))
        assertEquals(SpanishFluidG2P.phonemize("noventa grados"),
            SpanishFluidG2P.phonemize("90°"))
        assertTrue(SpanishFluidG2P.normalizeText("Una temperatura de -3 °C describe un estado.")
            .contains("menos tres grados Celsius"))
    }

    @Test fun unicodeSubtractionAndParenthesizedNegativesKeepTheirMathematicalMeaning() {
        val expected = "Contrasta menos cuatro menos tres y menos cuatro menos (menos tres)."
        val unicode = "Contrasta −4 − 3 y −4 − (−3)."
        assertEquals(SpanishFluidG2P.phonemize(expected), SpanishFluidG2P.phonemize(unicode))
        assertEquals(SpanishFluidG2P.phonemize(unicode),
            SpanishFluidG2P.phonemize("Contrasta -4 - 3 y -4 - (-3)."))
        assertFalse("Subtraction is not a punctuation pause",
            SpanishFluidG2P.normalizeText(unicode).contains('—'))
    }

    @Test fun comparisonsInAuthoredProseKeepTheirDirection() {
        assertEquals(SpanishFluidG2P.phonemize("La segunda, porque un medio es mayor que un tercio."),
            SpanishFluidG2P.phonemize("La segunda, porque un medio > un tercio."))
        assertEquals(SpanishFluidG2P.phonemize("Dos sextos es menor que tres sextos."),
            SpanishFluidG2P.phonemize("Dos sextos < tres sextos."))
    }

    @Test fun partiallySpokenBasesStillReadTheirNumericAndVariableExponents() {
        assertEquals(SpanishFluidG2P.phonemize("En (dos al cubo) al cuadrado se repite un bloque."),
            SpanishFluidG2P.phonemize("En (dos al cubo)² se repite un bloque."))
        assertEquals(SpanishFluidG2P.phonemize("Calcula menos tres al cuadrado y (menos tres) al cuadrado."),
            SpanishFluidG2P.phonemize("Calcula − tres al cuadrado y (−3)²."))
        val normalized = SpanishFluidG2P.normalizeText("Para exponentes positivos, aᵐ × aⁿ = aᵐ⁺ⁿ.")
        assertTrue(normalized.contains("elevado a eme"))
        assertTrue(normalized.contains("elevado a ene"))
        assertTrue(normalized.contains("eme más ene"))
        assertFalse(normalized.any { it in "ᵐⁿ⁺" })
        assertTrue(SpanishFluidG2P.normalizeText("La identidad k⁻¹ por k se conserva.").contains("menos uno"))
        assertTrue(SpanishFluidG2P.phonemize("Completa (tres al cuadrado)² = 3ᵉ.").isNotBlank())
    }

    @Test fun visibleSquaredAndCubedCentimetresRemainUnitsRatherThanUnknownGlyphs() {
        assertEquals(SpanishFluidG2P.phonemize("centímetros cuadrados"), SpanishFluidG2P.phonemize("cm²"))
        assertEquals(SpanishFluidG2P.phonemize("centímetros cúbicos"), SpanishFluidG2P.phonemize("cm³"))
        assertEquals(SpanishFluidG2P.phonemize("La unidad gramos barra centímetros cúbicos."),
            SpanishFluidG2P.phonemize("La unidad gramos/cm³."))
    }

    @Test fun realFactorizationPromptsKeepEveryVariableExponent() {
        val samples = listOf(
            "Completa 90 = 2ᵃ por 3ᵇ por 5ᶜ." to
                "Completa noventa es igual a dos elevado a a por tres elevado a be por cinco elevado a ce.",
            "Escribe los exponentes que completan 72 = 2ᵃ por 3ᵇ." to
                "Escribe los exponentes que completan setenta y dos es igual a dos elevado a a por tres elevado a be.",
            "La otra dirección construye el decimal: si q = 2ᵃ por 5ᵇ, elige n igual al mayor de a y b." to
                "La otra dirección construye el decimal: si q es igual a dos elevado a a por cinco elevado a be, elige n igual al mayor de a y b."
        )
        samples.forEach { (raw, spoken) ->
            assertEquals(raw, SpanishFluidG2P.phonemize(spoken), SpanishFluidG2P.phonemize(raw))
            assertFalse(SpanishFluidG2P.normalizeText(raw).any { it in "ᵃᵇᶜ" })
        }
        assertEquals("dos elevado a a por be", SpanishFluidG2P.normalizeText("dosᵃᵇ"))
    }

    @Test fun spanishPrioritySentenceAndTerminologyProduceCompatibleStressAndPauses() {
        val phones = SpanishFluidG2P.phonemize("Las fracciones equivalentes representan la misma cantidad. Tres cuartos es igual a seis octavos.")
        assertEquals(2, phones.count { it == '.' })
        assertTrue(phones.contains("fɾakθjˈones"))
        assertTrue(phones.contains("kwˈaɾtos"))
        assertTrue(phones.contains("sˈAs"))
        assertFalse(phones.any(Char::isDigit))
        assertFalse(phones.contains("fracciones"))
        assertTrue(SpanishFluidG2P.phonemize("álgebra, cálculo, raíz y ecuación").contains("ˈalxeβɾa"))
    }

    @Test fun knownProgrammingLanguageNameUsesTheLetterNameRatherThanAConsonant() {
        val spoken = SpanishFluidG2P.phonemize("Ejemplo de código ce más más.")
        assertEquals(spoken, SpanishFluidG2P.phonemize("Ejemplo de código C++ ."))
        assertEquals(spoken, SpanishFluidG2P.phonemize("Ejemplo de código C más más."))
        assertTrue(spoken.contains("θˈe"))
    }

    @Test fun unicodeNormalizationAndKokoroOpeningPunctuationAreHandledExplicitly() {
        val canonical = "¿Dónde está el capítulo?"
        assertEquals(SpanishFluidG2P.phonemize(canonical),
            SpanishFluidG2P.phonemize(Normalizer.normalize(canonical, Normalizer.Form.NFD)))
        assertEquals("dˈonde estˈa el kapˈitulo?", SpanishFluidG2P.phonemize(canonical))
        assertEquals("nˈo, ˈola!", SpanishFluidG2P.phonemize("¡NO, HOLA!"))
        assertEquals("", SpanishFluidG2P.phonemize("   "))
    }

    @Test fun lexiconLoaderJoinsOfficialCharacterArraySchemaAndRejectsUnknownPhones() {
        val file = File.createTempFile("spanish-fluid-lexicon", ".tmp")
        try {
            val payload = JSONObject().put("lower", JSONObject().put("web", JSONArray(listOf("w", "ˈ", "e", "b"))))
                .put("caseSensitive", JSONObject())
            file.writeText(payload.toString())
            assertEquals("la wˈeb", SpanishFluidG2P.phonemize("la web", SpanishFluidG2P.loadLexicon(file)))
            payload.getJSONObject("lower").put("web", JSONArray(listOf("UNKNOWN")))
            file.writeText(payload.toString())
            assertThrows(IllegalArgumentException::class.java) { SpanishFluidG2P.loadLexicon(file) }
        } finally { file.delete() }
    }

    @Test fun provisionalMathTitleSeparatorIsAValidKokoroPause() {
        val title = "Prueba de navegación 1 · Contenido provisional de desarrollo"
        val expected = "Prueba de navegación 1, Contenido provisional de desarrollo"
        assertEquals(SpanishFluidG2P.phonemize(expected), SpanishFluidG2P.phonemize(title))
        // Do not confuse a multiplication operator with a title separator.
        assertEquals(SpanishFluidG2P.phonemize("tres por cuatro"),
            SpanishFluidG2P.phonemize("3 · 4"))
        assertEquals(SpanishFluidG2P.phonemize("tres por cuatro"),
            SpanishFluidG2P.phonemize("3·4"))
    }

    @Test fun unsupportedPunctuationAndResourceLimitsFailWithoutSilentTokenDropping() {
        assertThrows(IllegalArgumentException::class.java) { SpanishFluidG2P.phonemize("hola ☃") }
        assertThrows(IllegalArgumentException::class.java) { SpanishFluidG2P.phonemize("hola Ж") }
        assertThrows(IllegalArgumentException::class.java) { SpanishFluidG2P.phonemizeNormalized("dato_1") }
        assertThrows(IllegalArgumentException::class.java) { SpanishFluidG2P.phonemizeNormalized("quedan 12") }
        assertThrows(IllegalArgumentException::class.java) { SpanishFluidG2P.phonemize("a".repeat(32_769)) }
        assertThrows(IllegalArgumentException::class.java) { SpanishFluidG2P.phonemize("hola", mapOf("hola" to "ɬ")) }
    }
}

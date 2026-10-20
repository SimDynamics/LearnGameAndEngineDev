package com.learngraphicsprogramming.app

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** The published lab declares its final construction, independently of its initial prediction. */
class CppOutputCheckPublishedLessonTest {
    private data class Translation(
        val language: String,
        val expected: String,
        val previous: String,
        val initial: String,
        val action: String
    )

    private val translations = listOf(
        Translation("es", "Caja recibida", "Zona segura", "Zona inicial", "Compilar, ejecutar y comprobar"),
        Translation("en", "Box received", "Safe zone", "Initial zone", "Compile, run and check"),
        Translation("fr", "Boîte reçue", "Zone sécurisée", "Zone initiale", "Compiler, exécuter et vérifier")
    )

    @Test fun finalOutputIsDeclaredByEachLocalizedTaskAndItsLaterReference() {
        for (translation in translations) {
            val lesson = lesson(translation.language)
            val page = lesson.getJSONArray("pages").getJSONObject(4).getJSONArray("blocks")
            val block = page.getJSONObject(2)
            assertEquals("cpp_compiler", block.getString("type"))
            assertFalse(block.has("id")) // Existing draft/progress keys remain page/block based.
            val specification = requireNotNull(CppOutputCheck.fromBlock(block))
            assertEquals("exact", specification.inputComparison)
            val target = specification.cases.single()
            assertEquals("", target.standardInput)
            assertEquals(listOf(translation.expected), target.outputs)
            assertTrue(page.getJSONObject(1).getString("text").contains(translation.expected))
            val reference = lesson.getJSONArray("pages").getJSONObject(5).getJSONArray("blocks")
                .getJSONObject(2).getString("code")
            assertTrue(reference.contains("std::cout << \"${translation.expected}\";"))
            assertTrue(block.getString("initialCode").contains("std::cout << \"${translation.initial}\";"))
        }
    }

    @Test fun firstTwoExecutionsRemainEditableExperimentsAndCannotPassTheFinalConstruction() {
        for (translation in translations) {
            val specification = requireNotNull(CppOutputCheck.fromBlock(compiler(translation.language)))
            assertFalse(verdict(specification, translation.initial).correct)
            assertNull(verdict(specification, translation.initial).explanation)
            val previous = verdict(specification, translation.previous)
            assertFalse(previous.correct)
            assertFalse(previous.explanation.isNullOrBlank())
            val final = verdict(specification, translation.expected)
            assertTrue(final.correct)
            assertFalse(final.explanation.isNullOrBlank())
        }
    }

    @Test fun localizedContractsRejectAddedQuotesWhitespaceAndOtherLanguagesWithoutInventedReasons() {
        for (translation in translations) {
            val specification = requireNotNull(CppOutputCheck.fromBlock(compiler(translation.language)))
            val wrong = listOf(" ${translation.expected}", "${translation.expected} ",
                "${translation.expected}\n", "\"${translation.expected}\"", "another result") +
                translations.filter { it != translation }.map { it.expected }
            for (output in wrong) {
                val result = verdict(specification, output)
                assertFalse("${translation.language}: $output", result.correct)
                assertNull(result.explanation)
            }
        }
    }

    @Test fun initialPredictionRetainsItsIndependentOriginalAnswer() {
        val blocks = lesson("es").getJSONArray("pages").getJSONObject(4).getJSONArray("blocks")
        val prediction = blocks.getJSONObject(3)
        assertEquals("output_prediction", prediction.getString("type"))
        assertEquals("prediccion_zona_inicial", prediction.getString("id"))
        assertEquals(listOf("Zona inicial"), (0 until prediction.getJSONArray("answers").length())
            .map { prediction.getJSONArray("answers").getString(it) })
        assertEquals("exact", prediction.getString("outputComparison"))
        assertEquals("cambiar_mensaje", blocks.getJSONObject(4).getString("id"))
        assertEquals(2, blocks.getJSONObject(4).getInt("correctIndex"))
    }

    @Test fun localizedInstructionsNameTheSingleActionAndOutputChecksRemainExplicitAndOptional() {
        for (translation in translations) {
            val lesson = lesson(translation.language)
            val blocks = lesson.getJSONArray("pages").getJSONObject(4).getJSONArray("blocks")
            val instructions = blocks.getJSONObject(1).getString("text")
            assertTrue(instructions.contains(translation.action))
            if (translation.language == "es") {
                assertFalse(instructions.contains("pulsa Comprobar salida"))
                assertFalse(instructions.contains("inmediatamente debajo de esa ventana"))
            }
            val ordinary = JSONObject(File(assets(), "content/${translation.language}/programming/" +
                "cpp_fundamentals/programas_y_ejecucion/lesson_texto_secuencia_y_formato.json").readText())
                .getJSONArray("pages").getJSONObject(6).getJSONArray("blocks").getJSONObject(2)
            assertEquals("cpp_compiler", ordinary.getString("type"))
            if (translation.language == "es") {
                val specification = requireNotNull(CppOutputCheck.fromBlock(ordinary))
                assertEquals("exact", specification.inputComparison)
                val target = specification.cases.single()
                assertEquals("", target.standardInput)
                assertEquals(listOf("Informe\nZona Este\nFin", "Informe\nZona Este\nFin\n"), target.outputs)
            } else {
                assertFalse(ordinary.has("outputCheck"))
                assertNull(CppOutputCheck.fromBlock(ordinary))
            }
            // An authored output contract is optional in every language. Keep that guarantee
            // independent of the Spanish lab, which now declares its own valid contract.
            val withoutOutputCheck = JSONObject(ordinary.toString()).apply { remove("outputCheck") }
            assertNull(CppOutputCheck.fromBlock(withoutOutputCheck))
        }
    }

    private fun assets() = listOf(File("src/main/assets"), File("app/src/main/assets"))
        .first { it.isDirectory }

    private fun lesson(language: String) = JSONObject(File(assets(), "content/$language/programming/" +
        "cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json").readText())

    private fun compiler(language: String) = lesson(language).getJSONArray("pages")
        .getJSONObject(4).getJSONArray("blocks").getJSONObject(2)

    private fun verdict(specification: CppOutputCheck.Specification, output: String): CppOutputCheck.Verdict {
        val session = CppOutputCheck.Session(specification)
        val snapshot = CppOutputCheck.snapshot(mapOf("main.cpp" to "any valid implementation"),
            listOf("main.cpp"), "clang23")
        val ticket = session.begin(snapshot, "")
        assertTrue(session.complete(ticket, snapshot, "", output))
        return requireNotNull(session.check())
    }
}

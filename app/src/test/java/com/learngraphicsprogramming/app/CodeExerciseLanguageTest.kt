package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.narration.LessonNarrationScript
import com.learngraphicsprogramming.app.narration.LessonNarrationText
import com.learngraphicsprogramming.app.narration.NarrationLanguage
import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class CodeExerciseLanguageTest {
    @Test fun publishedCmakeOrderingGetsItsLabelFromVisibleFragmentsWithoutReadingTheAnswer() {
        val block = published("lesson_datos_y_listas_de_objetivos.json", "dato_lista_antes_de_consumir")
        val original = block.toString()
        assertFalse(block.has("language"))
        assertEquals(CodeExerciseLanguage.Language.CMAKE, CodeExerciseLanguage.order(block))
        assertEquals(original, block.toString())
        block.put("correctOrder", JSONObject()).put("explanation", false).put("reviewFeedback", JSONArray())
        assertEquals(CodeExerciseLanguage.Language.CMAKE, CodeExerciseLanguage.order(block))
        val fragments = block.getJSONArray("fragments")
        block.put("fragments", JSONArray((0 until fragments.length()).reversed().map { fragments.getString(it) }))
        assertEquals(CodeExerciseLanguage.Language.CMAKE, CodeExerciseLanguage.order(block))
    }

    @Test fun legacyCppAndAmbiguousFragmentsRetainCpp() {
        for (fragments in listOf(emptyList(), listOf("#include <iostream>", "int main() {", "return 0;", "}"),
            listOf("set(x)"), listOf("project(x);"), listOf("std::cout << \"set_target_properties(x)\";"),
            listOf("// add_executable(app main.cpp)"), listOf("\"add_executable(app main.cpp)\""),
            listOf("set(x y)", "unrecognized(x)"))) {
            assertEquals(fragments.toString(), CodeExerciseLanguage.Language.CPP,
                CodeExerciseLanguage.order(order(fragments)))
        }
    }

    @Test fun explicitLanguageOverridesInferenceButUnsupportedMetadataFailsTechnically() {
        for (value in listOf("cpp", "c++", " CPP ")) {
            assertEquals(CodeExerciseLanguage.Language.CPP,
                CodeExerciseLanguage.order(order(listOf("add_executable(app main.cpp)")).put("language", value)))
        }
        assertEquals(CodeExerciseLanguage.Language.CMAKE,
            CodeExerciseLanguage.order(order(emptyList()).put("language", " CMake ")))
        for (value in listOf<Any>(JSONObject.NULL, "", "java", 2, false, JSONArray(), JSONObject())) {
            try {
                CodeExerciseLanguage.order(order(emptyList()).put("language", value))
                fail("Unsupported language $value must not silently label an exercise")
            } catch (_: IllegalArgumentException) { }
        }
    }

    @Test fun narrationAndVisibleOrderingShareLanguageResolutionInEveryNarrationLocale() {
        val block = published("lesson_datos_y_listas_de_objetivos.json", "dato_lista_antes_de_consumir")
        for (language in NarrationLanguage.entries) {
            val narrated = LessonNarrationText.page(page(block), 0, language).joinToString(" ")
            assertTrue("$language: $narrated", narrated.contains("CMake"))
            assertFalse(narrated.contains("C plus plus"))
            assertFalse(narrated.contains("C más más"))
        }
    }

    @Test fun dragExplanationIsReservedForItsExistingSuccessGateAndNeverReplacesTheReview() {
        val block = published("lesson_nombrar_reglas_y_separar_acuerdos.json", "regla_recuperar_los_papeles")
            .put("reviewFeedback", "REVISION_EXCLUSIVA_DEL_INTENTO")
            .put("explanation", "Esta explicación completa sólo sigue al acierto.")
        val original = block.toString()
        for (language in NarrationLanguage.entries) {
            val script = LessonNarrationScript.page(page(block), 0, language)
            val gate = script.gates.single()
            assertFalse(script.texts.take(gate.start).joinToString(" ").contains("explicación completa"))
            assertTrue(script.texts.subList(gate.start, gate.endExclusive).joinToString(" ").contains("explicación completa"))
            assertFalse(script.texts.joinToString(" ").contains("REVISION_EXCLUSIVA_DEL_INTENTO"))
        }
        assertEquals(original, block.toString())
    }

    @Test fun publishedLegacyDragNeedsNoCurricularChangeAndKeepsItsOriginalResponse() {
        val block = published("lesson_nombrar_reglas_y_separar_acuerdos.json", "regla_recuperar_los_papeles")
        val original = block.toString()
        assertFalse(block.has("explanation"))
        assertFalse(block.has("reviewFeedback"))
        assertEquals("safe", CppExerciseFeedback.review(block, fallback = "safe"))
        assertEquals("", CppExerciseFeedback.dragSuccessExplanation(block))
        assertEquals("[2,0]", block.getJSONArray("correctOrder").toString())
        assertEquals(original, block.toString())
    }

    private fun order(fragments: List<String>) = JSONObject().put("type", "order_code")
        .put("question", "Order.").put("fragments", JSONArray(fragments))

    private fun page(block: JSONObject) = JSONObject().put("pages", JSONArray().put(
        JSONObject().put("blocks", JSONArray().put(block))))

    private fun published(fileName: String, id: String): JSONObject {
        val root = File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile.parentFile!!
        val lesson = JSONObject(root.resolve("assets/content/es/programming/programming_practice_tools/" +
            "comprobaciones_repetibles/$fileName").readText())
        val pages = lesson.getJSONArray("pages")
        for (p in 0 until pages.length()) {
            val blocks = pages.getJSONObject(p).getJSONArray("blocks")
            for (b in 0 until blocks.length()) {
                val block = blocks.getJSONObject(b)
                if (block.optString("id") == id) return block
            }
        }
        error("Missing published fixture: $id")
    }
}

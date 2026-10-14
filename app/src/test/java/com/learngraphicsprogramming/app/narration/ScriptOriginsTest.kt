package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class ScriptOriginsTest {
    private fun paragraph(text: String) = JSONObject().put("type", "paragraph").put("text", text)
    private fun lesson(vararg blocks: JSONObject) = JSONObject().put("title", "Geometry.")
        .put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray(blocks.toList()))))
    private fun parts(texts: List<String>, engine: NarrationEngine) =
        if (engine == NarrationEngine.ANDROID) NarrationSegments.prepare(texts, 250)
        else NarrationSegments.sentences(texts)

    @Test fun existingSpeechAndFeedbackGatesRemainExactlyTheSame() {
        val choice = JSONObject().put("type", "multiple_choice").put("question", "Choose.")
            .put("options", JSONArray(listOf("First choice.", "Second choice.")))
            .put("correctAnswer", 1).put("explanation", "A consequence.")
        val original = lesson(paragraph("Before."), choice, paragraph("After."))
        val before = original.toString()
        val script = LessonNarrationScript.page(original, 0, NarrationLanguage.AMERICAN_ENGLISH)
        assertEquals(listOf("Geometry.", "Before.", "Choose.", "Option 1.", "First choice.",
            "Option 2.", "Second choice.", "Correct.", "A consequence.", "After."), script.texts)
        assertEquals(listOf(NarrationTextGate(1, 7, 9)), script.gates)
        assertEquals(listOf(NarrationTextOrigin(-1), NarrationTextOrigin(0),
            NarrationTextOrigin(1), NarrationTextOrigin(1), NarrationTextOrigin(1),
            NarrationTextOrigin(1), NarrationTextOrigin(1), NarrationTextOrigin(1, true),
            NarrationTextOrigin(1, true), NarrationTextOrigin(2)), script.origins)
        assertEquals(before, original.toString())
    }

    @Test fun engineSegmentationExpandsOriginsWithoutShiftingFeedbackOrDuplicatingText() {
        val long = "The existing paragraph contains " + "ordinary words ".repeat(45) +
            ". The second sentence remains intact."
        val block = JSONObject().put("type", "math_puzzle").put("prompt", "Compare the values.")
            .put("responses", JSONArray()).put("feedback", JSONObject().put("correct", "Equality is preserved."))
        val script = LessonNarrationScript.page(lesson(paragraph(long), block), 0,
            NarrationLanguage.AMERICAN_ENGLISH)
        val originalTexts = script.texts.toList()
        val signatures = mutableSetOf<String>()
        for (engine in listOf(NarrationEngine.ANDROID, NarrationEngine.KOKORO)) {
            val plan = requireNotNull(script.readingPlan(engine))
            assertEquals(parts(script.texts, engine).size, plan.origins.size)
            val expected = script.texts.indices.flatMap { index ->
                List(parts(listOf(script.texts[index]), engine).size) { script.origins[index] }
            }
            assertEquals(expected, plan.origins)
            val gate = NarrationScriptSegments.gates(script.texts, engine, script.gates).single()
            assertTrue(plan.origins.subList(gate.start, gate.endExclusive).all { it.feedback && it.blockIndex == 1 })
            assertFalse(plan.origins[gate.start - 1].feedback)
            assertEquals(plan, script.readingPlan(engine))
            signatures += plan.signature
        }
        assertEquals(2, signatures.size)
        assertEquals(originalTexts, script.texts)
    }

    @Test fun missingOrInconsistentProvenanceFallsBackInsteadOfGuessing() {
        assertNull(LessonNarrationScript(listOf("Visible."), emptyList()).readingPlan(NarrationEngine.KOKORO))
        assertNull(LessonNarrationScript(listOf("Visible.", "Later."), emptyList(),
            listOf(NarrationTextOrigin(0))).readingPlan(NarrationEngine.ANDROID))
        assertNull(LessonNarrationScript(listOf("Visible."), emptyList(),
            listOf(NarrationTextOrigin(-2))).readingPlan(NarrationEngine.KOKORO))
        assertNull(LessonNarrationScript(listOf("Visible."), emptyList(),
            listOf(NarrationTextOrigin(-1, true))).readingPlan(NarrationEngine.KOKORO))
        assertNull(LessonNarrationScript(listOf("   "), emptyList(),
            listOf(NarrationTextOrigin(0))).readingPlan(NarrationEngine.ANDROID))
    }

    @Test fun futureLessonsAndTranslationsNeedNoExtraFieldsAndNeverExposeAnswers() {
        val translations = listOf(NarrationLanguage.SPANISH to "Las fuerzas permanecen equilibradas.",
            NarrationLanguage.AMERICAN_ENGLISH to "The forces remain balanced.",
            NarrationLanguage.FRENCH to "Les forces restent équilibrées.")
        translations.forEach { (language, text) ->
            val puzzle = JSONObject().put("type", "math_puzzle").put("prompt", text)
                .put("responses", JSONArray()).put("representations", JSONArray().put(JSONObject()
                    .put("type", "math_formula").put("config", JSONObject().put("latex", "x + 2 = 5"))))
                .put("validator", JSONObject().put("expected", "SECRET_EXPECTED"))
                .put("feedback", JSONObject().put("correct", text).put("incorrect", "SECRET_WRONG"))
            val original = lesson(paragraph(text), JSONObject().put("type", "code")
                .put("language", "cpp").put("code", "int value = 3;"), puzzle)
            val before = original.toString()
            val script = LessonNarrationScript.page(original, 0, language)
            assertEquals(script.texts.size, script.origins.size)
            assertFalse(script.texts.joinToString().contains("SECRET"))
            assertEquals(setOf(-1, 0, 1, 2), script.origins.filterNot { it.feedback }.map { it.blockIndex }.toSet())
            for (engine in listOf(NarrationEngine.ANDROID, NarrationEngine.KOKORO)) {
                assertNotNull(script.readingPlan(engine))
            }
            assertEquals(before, original.toString())
            assertEquals(LessonNarrationText.page(original, 0, language),
                script.texts.filterIndexed { index, _ -> !script.origins[index].feedback })
        }
    }

    @Test fun existingProgrammingAndMathematicsAssetsGainOriginsWithoutCurricularEdits() {
        val paths = listOf(
            "src/main/assets/content/es/programming/cpp_fundamentals/intro_programming/lesson_first_contact_cpp.json",
            "src/main/assets/content/es/math_physics/math_algebra_elemental/recuperacion_y_transferencia/lesson_elegir_relaciones_y_recuperar_el_razonamiento_algebraico.json")
        paths.forEach { path ->
            val file = listOf(File(path), File("app", path)).firstOrNull { it.isFile }
            assertNotNull("Existing curriculum fixture: $path", file)
            val bytes = requireNotNull(file).readBytes()
            val lesson = JSONObject(String(bytes, Charsets.UTF_8))
            val before = lesson.toString()
            val pages = lesson.getJSONArray("pages")
            for (page in 0 until pages.length()) {
                val script = LessonNarrationScript.page(lesson, page, NarrationLanguage.SPANISH)
                assertEquals(script.texts.size, script.origins.size)
                assertEquals(LessonNarrationText.page(lesson, page, NarrationLanguage.SPANISH),
                    script.texts.filterIndexed { index, _ -> !script.origins[index].feedback })
                if (script.texts.isNotEmpty()) {
                    assertNotNull(script.readingPlan(NarrationEngine.ANDROID))
                    assertNotNull(script.readingPlan(NarrationEngine.KOKORO))
                }
                if (page > 0) assertTrue(script.origins.none { it.blockIndex == -1 })
            }
            assertEquals(before, lesson.toString())
            assertArrayEquals(bytes, file.readBytes())
        }
    }

    @Test fun everyPublishedLessonAndTranslationUsesTheOriginalTextWithoutPreparatoryMetadata() {
        val root = listOf(File("src/main/assets/content"), File("app/src/main/assets/content"))
            .firstOrNull { it.isDirectory }
        assertNotNull("The existing curriculum assets must be available", root)
        val content = requireNotNull(root)
        var lessons = 0
        var pagesChecked = 0
        var plans = 0
        val groups = mutableSetOf<String>()
        content.walkTopDown().filter { it.isFile && it.extension == "json" && it.name.startsWith("lesson_") }
            .sortedBy { it.path }.forEach { file ->
            val bytes = file.readBytes()
            val lesson = JSONObject(String(bytes, Charsets.UTF_8))
            val pages = lesson.optJSONArray("pages") ?: return@forEach
            val source = lesson.toString()
            val relative = file.relativeTo(content).invariantSeparatorsPath
            val group = relative.substringBefore('/')
            val fallback = requireNotNull(NarrationLanguage.fromTag(group)) { "Unknown language: $relative" }
            groups += group
            lessons++
            for (page in 0 until pages.length()) {
                pagesChecked++
                val language = NarrationLanguage.forLesson(lesson, page, fallback)
                val blockCount = pages.getJSONObject(page).optJSONArray("blocks")?.length() ?: 0
                for (engine in listOf(NarrationEngine.ANDROID, NarrationEngine.KOKORO)) {
                    val label = "$relative page=$page engine=$engine"
                    val forKokoro = engine == NarrationEngine.KOKORO
                    val script = LessonNarrationScript.page(lesson, page, language, forKokoro)
                    assertEquals("One runtime origin per original spoken fragment: $label", script.texts.size, script.origins.size)
                    assertEquals("Visible curriculum extraction is byte-for-byte unchanged: $label",
                        LessonNarrationText.page(lesson, page, language, forKokoro),
                        script.texts.filterIndexed { index, _ -> !script.origins[index].feedback })
                    assertTrue("Origins refer only to existing blocks: $label", script.origins.all {
                        it.blockIndex in 0 until blockCount || (it.blockIndex == -1 && page == 0 && !it.feedback)
                    })
                    if (script.texts.isNotEmpty()) {
                        val plan = script.readingPlan(engine)
                        assertNotNull("Existing text synchronizes without new JSON fields: $label", plan)
                        assertEquals("Exactly the existing engine's segmentation: $label",
                            parts(script.texts, engine).size, requireNotNull(plan).origins.size)
                        plans++
                    }
                }
            }
            assertEquals("No metadata or translated text was added to $relative", source, lesson.toString())
            assertArrayEquals("The curriculum file remains untouched: $relative", bytes, file.readBytes())
        }
        assertTrue("The actual existing curriculum, rather than just a small fixture, is covered", lessons > 100)
        assertEquals(setOf("es", "en", "fr"), groups)
        assertTrue(pagesChecked > lessons)
        assertTrue(plans >= pagesChecked)
        println("Original curriculum compatibility: $lessons lessons, $pagesChecked pages, $plans engine plans, languages=$groups")
    }

    @Test fun signatureChangesForTextOriginFeedbackOrEngineButNotForRebuildingTheSamePlan() {
        fun script(text: String = "Visible.", origin: NarrationTextOrigin = NarrationTextOrigin(0)) =
            LessonNarrationScript(listOf(text), emptyList(), listOf(origin))
        val original = requireNotNull(script().readingPlan(NarrationEngine.KOKORO))
        assertEquals(original, script().readingPlan(NarrationEngine.KOKORO))
        assertNotEquals(original.signature, script("Translated.").readingPlan(NarrationEngine.KOKORO)?.signature)
        assertNotEquals(original.signature, script(origin = NarrationTextOrigin(1)).readingPlan(NarrationEngine.KOKORO)?.signature)
        assertNotEquals(original.signature, script(origin = NarrationTextOrigin(0, true)).readingPlan(NarrationEngine.KOKORO)?.signature)
        assertNotEquals(original.signature, script().readingPlan(NarrationEngine.ANDROID)?.signature)
    }
}

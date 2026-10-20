package com.learngraphicsprogramming.app.narration

import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Real Spanish assets exercise both the page reader and the gated success reader. */
class InlineCodeCurricularNarrationTest {
    private data class Field(
        val file: File,
        val relativePath: String,
        val lesson: JSONObject,
        val block: JSONObject,
        val page: Int,
        val position: Int,
        val name: String
    ) {
        val text: String get() = block.getString(name)
        val mode: String get() = block.optString("mathMode", "auto")
        val key: String get() = "$relativePath:p${page + 1}b${position + 1}:$name"
    }

    @Test fun reportedLiteralAndOperatorSuccessReadsTheOperatorWithoutAnnouncingFormatting() {
        val field = fields().single { it.relativePath.endsWith("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json") &&
            it.page == 4 && it.position == 1 && it.name == "explanation" }
        assertEquals("literal_y_operador", field.block.getString("id"))
        assertEquals("inline_fill_code", field.block.getString("type"))
        assertTrue(field.text.contains("`<<`"))
        val source = field.lesson.toString()
        val bytes = field.file.readBytes()
        for (kokoro in listOf(false, true)) {
            val script = LessonNarrationScript.page(page(field.block), 0, NarrationLanguage.SPANISH, kokoro)
            val gate = script.gates.single()
            val success = script.texts.subList(gate.start, gate.endExclusive)
            assertEquals("Correcto.", ManualEnglishSpeech.strip(success.first()))
            val spoken = success.drop(1).joinToString(" ", transform = ManualEnglishSpeech::strip)
            assertTrue(spoken, spoken.contains("El primer hueco contiene doble menor que, el operador"))
            assertFalse(spoken, spoken.contains("fin de código"))
            assertFalse(spoken, spoken.contains("código, doble menor que"))
            assertTrue("The explanation is still gated", script.origins.take(gate.start).none { it.feedback })
            assertEquals("The whole success explanation keeps feedback provenance",
                (gate.start until gate.endExclusive).toList(), script.origins.indices.filter { script.origins[it].feedback })
        }
        assertEquals(source, field.lesson.toString())
        assertArrayEquals(bytes, field.file.readBytes())
    }

    @Test fun allCurrentInlineFieldsUseTheirPageOrGatedFeedbackProductionPathsWithoutChangingAssets() {
        val fields = fields()
        assertTrue("Discover authored paragraphs without imposing a curriculum size", fields.any { it.name == "text" })
        assertTrue("Include authored successful feedback", fields.any { it.name == "explanation" })
        val documents = fields.distinctBy { it.file }.associate { it.file to (it.lesson.toString() to it.file.readBytes()) }
        for (field in fields) {
            for (kokoro in listOf(false, true)) {
                val regular = if (kokoro) SpanishTechnicalSpeech.prepareForKokoro(field.text, field.mode)
                    else SpanishTechnicalSpeech.prepare(field.text, field.mode)
                val pieces = mutableListOf<String>()
                val observed = if (kokoro) SpanishTechnicalSpeech.prepareForReading(field.text, field.mode) { start, end, text ->
                    assertTrue(field.key, start in 0..field.text.length && end in start..field.text.length)
                    if (end == start + 1 && field.text[start] == '`') assertTrue(field.key, text.isBlank())
                    pieces += text
                } else SpanishTechnicalSpeech.prepareObserved(field.text, field.mode) { start, end, text ->
                    assertTrue(field.key, start in 0..field.text.length && end in start..field.text.length)
                    if (end == start + 1 && field.text[start] == '`') assertTrue(field.key, text.isBlank())
                    pieces += text
                }
                assertEquals("Observed preparation must be the production preparation: ${field.key}", regular, observed)
                val joined = pieces.joinToString("")
                val reconstructed = if (ManualEnglishSpeech.isPrepared(joined))
                    ManualEnglishSpeech.PREPARED + normalize(joined.removePrefix(ManualEnglishSpeech.PREPARED.toString()))
                    else normalize(joined)
                assertEquals("Source mapping retains every spoken fragment: ${field.key}", regular.text, reconstructed)
                val asParagraph = JSONObject().put("type", "paragraph").put("text", field.text).put("mathMode", field.mode)
                val expected = LessonNarrationText.page(page(asParagraph), 0, NarrationLanguage.SPANISH, kokoro)
                val actual = if (field.name == "text") {
                    LessonNarrationText.page(page(field.block), 0, NarrationLanguage.SPANISH, kokoro)
                } else {
                    val script = LessonNarrationScript.page(page(field.block), 0, NarrationLanguage.SPANISH, kokoro)
                    val gate = script.gates.single()
                    assertEquals(field.key, "Correcto.", ManualEnglishSpeech.strip(script.texts[gate.start]))
                    script.texts.subList(gate.start + 1, gate.endExclusive)
                }
                assertEquals("The authored paragraph/complete success reaches its real reader: ${field.key}", expected, actual)
            }
        }
        for ((file, before) in documents) {
            assertEquals("JSON objects stay unchanged: $file", before.first, fields.first { it.file == file }.lesson.toString())
            assertArrayEquals("Asset bytes stay unchanged: $file", before.second, file.readBytes())
        }
    }

    private fun fields(): List<Field> {
        val main = requireNotNull(File(System.getProperty("translation.resources") ?: "src/main/res").absoluteFile.parentFile)
        val root = main.resolve("assets/content/es")
        return root.walkTopDown().filter { it.isFile && it.extension == "json" }.sortedBy { it.relativeTo(root).invariantSeparatorsPath }
            .flatMap { file ->
                val source = file.readText().removePrefix("\uFEFF").trimStart()
                if (!source.startsWith("{")) return@flatMap emptySequence()
                val lesson = JSONObject(source)
                val pages = lesson.optJSONArray("pages") ?: return@flatMap emptySequence()
                sequence {
                    for (page in 0 until pages.length()) {
                        val blocks = pages.getJSONObject(page).optJSONArray("blocks") ?: continue
                        for (index in 0 until blocks.length()) {
                            val block = blocks.getJSONObject(index)
                            for (name in listOf("text", "explanation")) {
                                val text = block.opt(name) as? String ?: continue
                                if ('`' !in text) continue
                                yield(Field(file, file.relativeTo(root).invariantSeparatorsPath, lesson, block, page, index, name))
                            }
                        }
                    }
                }
            }.toList()
    }

    private fun page(block: JSONObject) = JSONObject().put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))
    private fun normalize(text: String) = text.replace(Regex("[ \\t]+"), " ").replace(Regex(" +([,.;:!?])"), "$1").trim()
}

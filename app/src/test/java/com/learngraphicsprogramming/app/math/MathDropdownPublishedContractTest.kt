package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.security.MessageDigest

/** Host checks protect the published values/progress; native tests check all rendered text. */
class MathDropdownPublishedContractTest {
    @Test fun thePublishedLessonAndOptionTextsRemainUnmodified() {
        val file = source()
        // Selected-option reviews are appended presentation metadata. Remove just those
        // JSON property lines to keep the exact pre-existing publication guard, including
        // every other text, option, answer and validator; do not replace its expected SHA.
        val originalContent = file.readText().replace("\r\n", "\n")
            .replace(Regex(",\\n\\s*\"reviewFeedback\": \"(?:\\\\.|[^\"\\\\])*\""), "")
        val digest = MessageDigest.getInstance("SHA-256")
            .digest(originalContent.toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it) }
        assertEquals("Do not shorten labels or change the content fingerprint to fix drawing",
            "d623dd0b0b7ecbc4368d1f85703945811919ca0d6a6aec166cb6663b51473629", digest)
        val block = block()
        assertEquals("conservar_el_significado_de_nombres_y_datos", block.getString("id"))
        val response = MathPuzzleParser.parse(block).responses.single()
        assertEquals("conservar_el_significado_de_nombres_y_datos__lecturas", response.id)
        val fields = response.config.getJSONArray("fields")
        assertEquals(listOf("mismo", "distintos", "pertenencia"),
            (0 until fields.length()).map { fields.getJSONObject(it).getString("id") })
        for (i in 0 until fields.length()) {
            val options = fields.getJSONObject(i).getJSONArray("options")
            assertEquals(3, options.length())
            for (j in 0 until options.length()) {
                val label = options.getJSONObject(j).getString("label")
                assertFalse("A published option is not an abbreviated UI string", label.contains('…'))
                assertTrue(label.length > options.getJSONObject(j).getString("id").length)
            }
        }
    }

    @Test fun labelsDoNotReplaceTheStableOptionIdsInAnswers() {
        val response = MathPuzzleParser.parse(block()).responses.single()
        val correct = mapOf("mismo" to "un_punto", "distintos" to "dos_puntos", "pertenencia" to "pertenece")
        val answer = MathAnswer.Fields(correct)
        assertTrue(MathValidators.validate(response.validator, answer).correct)
        val fields = response.config.getJSONArray("fields")
        for (i in 0 until fields.length()) {
            val field = fields.getJSONObject(i)
            val id = field.getString("id")
            val options = field.getJSONArray("options")
            for (j in 0 until options.length()) {
                val option = options.getJSONObject(j)
                val values = correct + (id to option.getString("id"))
                val checked = MathValidators.validate(response.validator, MathAnswer.Fields(values))
                assertTrue("Every full option remains evaluable", checked.evaluable)
                assertEquals(option.getString("id") == correct.getValue(id), checked.correct)
                assertFalse("The display label must not become the stored/evaluated ID",
                    MathValidators.validate(response.validator, MathAnswer.Fields(correct + (id to option.getString("label")))).correct)
            }
        }
    }

    @Test fun correctAndIncorrectSelectionsRoundTripWithoutChangingTheBlockIdentity() {
        val block = block()
        val response = MathPuzzleParser.parse(block).responses.single()
        val correct = MathAnswer.Fields(mapOf("mismo" to "un_punto", "distintos" to "dos_puntos", "pertenencia" to "pertenece"))
        val wrong = MathAnswer.Fields(correct.values + ("pertenencia" to "fuera"))
        for (answer in listOf(correct, wrong)) {
            val checked = MathValidators.validate(response.validator, answer)
            val encoded = MathAnswerJson.encode(answer)
            val decoded = MathAnswerJson.decode(JSONObject(encoded.toString()))
            assertEquals(answer, decoded)
            val state = JSONObject().put("correct", checked.correct)
                .put("contentFingerprint", MathPuzzleState.fingerprint(block))
                .put("responses", JSONObject().put(response.id, encoded))
            assertEquals(checked.correct, MathPuzzleState.isCurrentCorrect(block, JSONObject(state.toString())))
            assertEquals(MathPuzzleState.fingerprint(block), state.getString("contentFingerprint"))
        }
    }

    private fun source() = File("src/main/assets/content/es/math_physics/math_geometria_plana/fundamentos_geometricos/lesson_del_trazo_a_la_figura_geometrica.json")
    private fun block() = JSONObject(source().readText()).getJSONArray("pages").getJSONObject(1).getJSONArray("blocks").getJSONObject(4)
}

package com.learngraphicsprogramming.app.math

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.security.MessageDigest

/** Host checks protect the published values/progress; native tests check all rendered text. */
class MathDropdownPublishedContractTest {
    @Test fun thePublishedDropdownContractAndOptionTextsRemainUnmodified() {
        val block = block()
        // This entire dropdown block is unchanged since 5046030 except for the
        // supported option reviews. Work legitimately finalized the lesson title,
        // revised another puzzle's feedback and added three pages in 650459f.
        // Guard this exercise, rather than pinning unrelated curricular development.
        val originalContent = canonicalJson(MathPuzzleState.withoutReviewFeedback(block))
        val digest = MessageDigest.getInstance("SHA-256")
            .digest(originalContent.toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it) }
        assertEquals("Preserve the original dropdown's full texts, IDs, responses and validators",
            "c7a3c9dbe4c69dc6687f60da1a9ca6b3b7d429ac8cf5047eca27b0df7008c551", digest)
        assertEquals("conservar_el_significado_de_nombres_y_datos", block.getString("id"))
        val response = MathPuzzleParser.parse(block).responses.single()
        assertEquals("conservar_el_significado_de_nombres_y_datos__lecturas", response.id)
        val fields = response.config.getJSONArray("fields")
        assertEquals(listOf("mismo", "distintos", "pertenencia"),
            (0 until fields.length()).map { fields.getJSONObject(it).getString("id") })
        val positionOptions = listOf(
            "un_punto" to "Se ha identificado una sola posición geométrica.",
            "dos_puntos" to "Se han identificado dos posiciones geométricas distintas.",
            "sin_decidir" to "Los datos no permiten decidir si las posiciones son distintas.")
        val expectedOptions = listOf(positionOptions, positionOptions, listOf(
            "pertenece" to "E pertenece a r, como indica el dato.",
            "fuera" to "E está fuera de r porque su letra se imprimió aparte.",
            "cerca" to "Sólo sabemos que E está cerca de r; la pertenencia indicada no cuenta."))
        for (i in 0 until fields.length()) {
            val options = fields.getJSONObject(i).getJSONArray("options")
            assertEquals(3, options.length())
            assertEquals(expectedOptions[i], (0 until options.length()).map {
                options.getJSONObject(it).let { option -> option.getString("id") to option.getString("label") }
            })
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

    private fun canonicalJson(value: Any): String = when (value) {
        is JSONObject -> value.keys().asSequence().toList().sorted().joinToString(",", "{", "}") {
            "${JSONObject.quote(it)}:${canonicalJson(value.get(it))}"
        }
        is org.json.JSONArray -> (0 until value.length()).joinToString(",", "[", "]") { canonicalJson(value.get(it)) }
        is String -> JSONObject.quote(value)
        else -> value.toString()
    }

    private fun source() = File("src/main/assets/content/es/math_physics/math_geometria_plana/fundamentos_geometricos/lesson_del_trazo_a_la_figura_geometrica.json")
    private fun block(): JSONObject {
        val pages = JSONObject(source().readText()).getJSONArray("pages")
        for (p in 0 until pages.length()) {
            val blocks = pages.getJSONObject(p).getJSONArray("blocks")
            for (b in 0 until blocks.length()) {
                val block = blocks.getJSONObject(b)
                if (block.optString("id") == "conservar_el_significado_de_nombres_y_datos") return block
            }
        }
        error("Missing published dropdown fixture")
    }
}

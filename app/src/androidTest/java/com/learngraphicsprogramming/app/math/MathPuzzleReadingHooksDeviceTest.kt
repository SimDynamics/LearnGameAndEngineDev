package com.learngraphicsprogramming.app.math

import android.content.Context
import android.graphics.Color
import android.view.ContextThemeWrapper
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CompoundButton
import android.widget.EditText
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.R
import com.learngraphicsprogramming.app.narration.ReadingTextView
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

/** Native component checks. No Activity/audio is started and no curricular asset is written. */
@RunWith(AndroidJUnit4::class)
class MathPuzzleReadingHooksDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private var savedProgress: Map<String, *>? = null
    private data class Event(val view: TextView, val role: MathPuzzleRenderer.TextRole, val text: String, val color: Int)

    @Before fun preserveIsolatedQaProgress() {
        assertTrue("Use an isolated QA package", context.packageName in setOf(
            "com.learngraphicsprogramming.app.curricularqa", "com.learngraphicsprogramming.app.realdownloadsqa"))
        savedProgress = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE).all.mapValues { (_, value) ->
            if (value is Set<*>) value.toSet() else value
        }
    }

    @After fun restoreProgress() {
        val original = savedProgress ?: return
        val preferences = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE)
        val editor = preferences.edit().clear()
        original.forEach { (key, value) ->
            when (value) {
                is String -> editor.putString(key, value)
                is Int -> editor.putInt(key, value)
                is Long -> editor.putLong(key, value)
                is Float -> editor.putFloat(key, value)
                is Boolean -> editor.putBoolean(key, value)
                is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                else -> error("Unsupported QA preference value")
            }
        }
        assertTrue(editor.commit())
        assertEquals(original, preferences.all)
    }

    @Test fun authoredRichChoicesAreExposedWithoutReplacingTheirClickableCardsOrControls() = onMain {
        val block = choiceBlock()
        val source = block.toString()
        val events = mutableListOf<Event>()
        val renderer = renderer(block, events)
        val bodies = events.filter { it.role == MathPuzzleRenderer.TextRole.BODY }
        assertEquals(listOf("Elige la propiedad de std::cout.", "Representación original.", "Compara las opciones.",
            "Primera alternativa.", "x + 1", "Explicación visible.", "Segunda alternativa."), bodies.map { it.text })
        assertEquals(bodies.map { it.view }, renderer.narrationTextViews)
        assertTrue(bodies.all { it.view is ReadingTextView && it.view !is Button && it.view !is EditText })
        val choice = bodies.single { it.text == "Primera alternativa." }.view
        val card = generateSequence(choice.parent as? View) { it.parent as? View }.first { it.isClickable }
        assertTrue("The existing rich-body card remains the gesture owner", card is MathTouchCard)
        val nativeButtons = tree(renderer.view).filterIsInstance<CompoundButton>()
        assertEquals(2, nativeButtons.size)
        assertTrue(nativeButtons.all { it.text.isEmpty() && it !in renderer.narrationTextViews })
        assertTrue(card.performClick())
        assertTrue(nativeButtons.first().isChecked)
        assertEquals("View hooks cannot rewrite educational content", source, block.toString())
    }

    @Test fun reusedFeedbackReportsItsRoleOnlyAfterTheOriginalTextAndCorrectnessColorAreReady() = onMain {
        val events = mutableListOf<Event>()
        val renderer = renderer(choiceBlock(), events)
        val buttons = tree(renderer.view).filterIsInstance<CompoundButton>()
        val check = tree(renderer.view).filterIsInstance<Button>().single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
        buttons[1].isChecked = true
        check.performClick()
        val incorrect = events.last()
        assertEquals(MathPuzzleRenderer.TextRole.INCORRECT_FEEDBACK, incorrect.role)
        assertTrue(incorrect.text.isNotBlank())
        assertEquals(Color.rgb(240, 166, 166), incorrect.color)
        assertFalse(renderer.isCorrect)

        buttons[0].isChecked = true
        assertEquals(MathPuzzleRenderer.TextRole.CLEARED, events.last().role)
        assertEquals("", events.last().text)
        check.performClick()
        val correct = events.last()
        assertSame(incorrect.view, correct.view)
        assertEquals(MathPuzzleRenderer.TextRole.CORRECT_FEEDBACK, correct.role)
        assertTrue(correct.text.contains("Conclusión comprobada."))
        assertEquals(Color.rgb(180, 220, 190), correct.color)
        assertTrue(renderer.isCorrect)
        assertFalse("Feedback is registered separately from narrated body text", correct.view in renderer.narrationTextViews)

        tree(renderer.view).single { it.contentDescription == context.getString(R.string.math_ui_restart) }.performClick()
        assertEquals(MathPuzzleRenderer.TextRole.CLEARED, events.last().role)
        assertEquals("", events.last().text)
        assertFalse(renderer.isCorrect)
    }

    @Test fun evaluationErrorsAndDynamicMatchingLabelsNeverBecomeNarratedBodyText() = onMain {
        val events = mutableListOf<Event>()
        val numeric = JSONObject().put("type", "math_puzzle").put("prompt", "Introduce dos.")
            .put("responses", JSONArray().put(JSONObject().put("id", "number").put("type", "math_numeric_input")
                .put("label", "Cantidad.").put("validator", JSONObject().put("type", "numeric").put("expected", 2))))
        val renderer = renderer(numeric, events)
        tree(renderer.view).filterIsInstance<EditText>().single().setText("no es un número")
        tree(renderer.view).filterIsInstance<Button>().single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick()
        assertEquals(MathPuzzleRenderer.TextRole.ERROR_FEEDBACK, events.last().role)
        assertEquals(MathWidgetUi.muted, events.last().color)
        assertTrue(events.last().text.isNotBlank())

        events.clear()
        val matching = JSONObject().put("type", "math_puzzle").put("prompt", "Relaciona las tarjetas.")
            .put("responses", JSONArray().put(JSONObject().put("id", "pairs").put("type", "math_matching_drag")
                .put("label", "Compara ambas columnas.")
                .put("sources", JSONArray().put(JSONObject().put("id", "a").put("label", "Ficha original.")))
                .put("targets", JSONArray().put(JSONObject().put("id", "b").put("label", "Destino original.")))
                .put("validator", JSONObject().put("type", "matching").put("expected", JSONObject().put("a", "b")))))
        val matchRenderer = renderer(matching, events)
        assertEquals(listOf("Relaciona las tarjetas.", "Compara ambas columnas.",
            context.getString(R.string.math_ui_tiles), "Ficha original.",
            context.getString(R.string.math_ui_targets), "Destino original."),
            events.filter { it.role == MathPuzzleRenderer.TextRole.BODY }.map { it.text })
        val unassigned = tree(matchRenderer.view).filterIsInstance<TextView>()
            .single { it.text.toString() == context.getString(R.string.math_ui_unassigned) }
        assertFalse(unassigned in matchRenderer.narrationTextViews)
    }

    @Test fun onlyNewEvaluableWrongSubmissionsEmitTheirOwnSelectedGuidance() = onMain {
        val block = choiceBlock()
        val response = block.getJSONArray("responses").getJSONObject(0)
        response.getJSONArray("options").getJSONObject(0).put("reviewFeedback", "ABSENT_OPTION_MUST_NOT_BE_SPOKEN")
        response.getJSONArray("options").getJSONObject(1).put("reviewFeedback", "Revisa sólo el razonamiento que elegiste.")
        block.put("feedback", JSONObject().put("correct", "SUCCESS_MUST_NOT_BE_SPOKEN")
            .put("incorrect", "GLOBAL_ABSENT_CHOICES_MUST_NOT_BE_SPOKEN"))
        val original = block.toString()
        val lessonId = "math_wrong_review_hook_${UUID.randomUUID()}"
        val reviews = mutableListOf<List<String>>()
        fun create() = MathPuzzleRenderer(
            ContextThemeWrapper(context, R.style.Theme_LearnGraphicsProgramming), "math_physics",
            lessonId, 0, 0, block, onIncorrectAttempt = { reviews += it }
        ) { }
        val first = create()
        assertTrue("Constructing a feedback view is not a submission", reviews.isEmpty())
        val buttons = tree(first.view).filterIsInstance<CompoundButton>()
        val check = tree(first.view).filterIsInstance<Button>().single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }
        buttons[1].isChecked = true
        assertTrue("Editing a draft does not request speech", reviews.isEmpty())
        assertTrue(check.performClick())
        assertEquals(1, reviews.size)
        assertTrue(reviews.single().joinToString(" ").contains("Revisa sólo el razonamiento que elegiste."))
        assertFalse(reviews.single().joinToString(" ").contains("MUST_NOT_BE_SPOKEN"))
        assertFalse(reviews.single().joinToString(" ").contains("choice"))
        assertTrue(check.performClick())
        assertEquals("A second deliberate submission can repeat the same guidance", 2, reviews.size)
        val restored = create()
        assertEquals("Reopening an incorrect draft cannot trigger narration", 2, reviews.size)
        val restoredButtons = tree(restored.view).filterIsInstance<CompoundButton>()
        restoredButtons[0].isChecked = true
        tree(restored.view).filterIsInstance<Button>().single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick()
        assertTrue(restored.isCorrect)
        assertEquals("Successful feedback keeps its independent existing narration gate", 2, reviews.size)
        create()
        assertEquals("Restoring correct progress cannot trigger a wrong review", 2, reviews.size)

        val numeric = JSONObject().put("type", "math_puzzle").put("prompt", "Introduce dos.")
            .put("responses", JSONArray().put(JSONObject().put("id", "number").put("type", "math_numeric_input")
                .put("label", "Cantidad.").put("validator", JSONObject().put("type", "numeric").put("expected", 2))))
        val technical = MathPuzzleRenderer(ContextThemeWrapper(context, R.style.Theme_LearnGraphicsProgramming),
            "math_physics", "math_wrong_review_technical_${UUID.randomUUID()}", 0, 0, numeric,
            onIncorrectAttempt = { reviews += it }) { }
        tree(technical.view).filterIsInstance<EditText>().single().setText("not a number")
        tree(technical.view).filterIsInstance<Button>().single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick()
        assertEquals("A technical error cannot request wrong-answer speech", 2, reviews.size)
        assertEquals(original, block.toString())
    }

    @Test fun anEvaluableWrongResultWithoutAnAuthoredOrValidatorMessageUsesTheVisibleProcessFallback() = onMain {
        val block = JSONObject().put("type", "math_puzzle").put("prompt", "Introduce dos.")
            .put("responses", JSONArray().put(JSONObject().put("id", "internal_number_id").put("type", "math_numeric_input")
                .put("validator", JSONObject().put("type", "numeric").put("expected", 2))))
        val reviews = mutableListOf<List<String>>()
        val renderer = MathPuzzleRenderer(ContextThemeWrapper(context, R.style.Theme_LearnGraphicsProgramming),
            "math_physics", "math_wrong_review_empty_${UUID.randomUUID()}", 0, 0, block,
            attemptEvaluation = { _, _ -> listOf(MathValidationResult(false, "")) },
            onIncorrectAttempt = { reviews += it }) { }
        tree(renderer.view).filterIsInstance<EditText>().single().setText("1")
        tree(renderer.view).filterIsInstance<Button>().single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick()
        val fallback = context.getString(R.string.math_ui_review_process)
        val visible = tree(renderer.view).filterIsInstance<TextView>().single { it.text.startsWith("✗ ") }
        assertEquals(listOf(listOf(visible.text.toString())), reviews)
        assertTrue("The spoken fallback must also be visible", tree(renderer.view).filterIsInstance<TextView>()
            .any { it.text.toString().contains(fallback) })
        assertFalse(reviews.flatten().joinToString(" ").contains("internal_number_id"))
        assertFalse(renderer.isCorrect)
    }

    @Test fun ordinaryNumericWrongAnswersSpeakTheSameVisibleProcessGuideWithoutDisclosingTheExpectedValue() = onMain {
        val block = JSONObject().put("type", "math_puzzle").put("prompt", "Introduce la cantidad.")
            .put("responses", JSONArray().put(JSONObject().put("id", "number_secret_id").put("type", "math_numeric_input")
                .put("validator", JSONObject().put("type", "numeric").put("expected", 821))))
        val reviews = mutableListOf<List<String>>()
        val renderer = MathPuzzleRenderer(ContextThemeWrapper(context, R.style.Theme_LearnGraphicsProgramming),
            "math_physics", "math_wrong_review_numeric_${UUID.randomUUID()}", 0, 0, block,
            onIncorrectAttempt = { reviews += it }) { }
        tree(renderer.view).filterIsInstance<EditText>().single().setText("1")
        tree(renderer.view).filterIsInstance<Button>().single { it.tag == MathPuzzleRenderer.CHECK_ACTION_TAG }.performClick()
        assertFalse(renderer.isCorrect)
        val guide = reviews.single().joinToString("\n")
        assertTrue(guide.contains(context.getString(R.string.math_ui_review_process)))
        assertFalse(guide.contains("821"))
        assertFalse(guide.contains("number_secret_id"))
        assertTrue(tree(renderer.view).filterIsInstance<TextView>().any { it.text.toString().contains(guide) })
    }

    private fun renderer(block: JSONObject, events: MutableList<Event>): MathPuzzleRenderer = MathPuzzleRenderer(
        ContextThemeWrapper(context, R.style.Theme_LearnGraphicsProgramming), "math_physics",
        "math_reading_hook_qa_${UUID.randomUUID()}", 0, 0, block,
        onTextReady = { view, role -> events += Event(view, role, view.text.toString(), view.currentTextColor) }
    ) { }

    private fun choiceBlock(): JSONObject = JSONObject().put("type", "math_puzzle")
        .put("prompt", "Elige la propiedad de std::cout.")
        .put("representations", JSONArray().put(JSONObject().put("type", "text").put("text", "Representación original.")))
        .put("responses", JSONArray().put(JSONObject().put("id", "choice").put("type", "math_multiple_choice_single")
            .put("label", "Compara las opciones.").put("options", JSONArray()
                .put(JSONObject().put("id", "first").put("content", JSONArray()
                    .put(JSONObject().put("type", "text").put("text", "Primera alternativa."))
                    .put(JSONObject().put("type", "math_formula").put("latex", "x + 1")))
                    .put("explanation", "Explicación visible."))
                .put(JSONObject().put("id", "second").put("label", "Segunda alternativa.")))
            .put("validator", JSONObject().put("type", "choice").put("expected", JSONArray().put("first")))))
        .put("feedback", JSONObject().put("correct", "Conclusión comprobada.").put("incorrect", "Revisa la comparación."))

    private fun tree(view: View): List<View> = listOf(view) + if (view is ViewGroup)
        (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()

    private fun onMain(block: () -> Unit) = instrumentation.runOnMainSync(block)
}

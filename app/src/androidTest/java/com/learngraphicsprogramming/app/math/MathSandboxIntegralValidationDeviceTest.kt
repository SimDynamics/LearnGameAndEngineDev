package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.os.ParcelFileDescriptor
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Reproduces the page's authored expected value through actual controls and persisted answers. */
@RunWith(AndroidJUnit4::class)
class MathSandboxIntegralValidationDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val route = "math_physics"
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun preserveAndIsolateUserState() {
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
        for (name in listOf("learning_progress", "navigation_state", "lesson_text_size_settings")) {
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, route, lesson)
        ProgressRepository.saveCurrentPage(context, route, lesson, 17)
    }

    @After fun restoreUserState() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE).edit().clear()
            original.forEach { (key, value) ->
                when (value) {
                    is String -> preferences.putString(key, value)
                    is Int -> preferences.putInt(key, value)
                    is Long -> preferences.putLong(key, value)
                    is Float -> preferences.putFloat(key, value)
                    is Boolean -> preferences.putBoolean(key, value)
                    is Set<*> -> preferences.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue(preferences.commit())
        }
    }

    @Test fun pageEighteenAcceptsAConstructedIntegralWithItsTrailingDifferentialAndPersistsCompletion() {
        val block = advancedBlock()
        val response = block.getJSONArray("responses").getJSONObject(0)
        val fields = response.getJSONArray("fields")
        val validators = response.getJSONObject("validator").getJSONObject("fields")
        var serializedAnswers = emptyList<String>()
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                /*
                 * En debug, MathDevelopmentMode fuerza el sandbox a abrir en la primera
                 * página manual, que actualmente es la 17. Resolvemos esa página con sus
                 * valores authored y avanzamos normalmente a la 18 antes de probar la integral.
                 */
                assertEquals("17 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())

                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val basicBlock = puzzleBlock(16)
                val basicResponse = basicBlock.getJSONArray("responses").getJSONObject(0)
                val basicFields = basicResponse.getJSONArray("fields")
                val basicValidators = basicResponse.getJSONObject("validator").getJSONObject("fields")
                val basicEditors = tree(content).filterIsInstance<MathExpressionEditor>()

                assertEquals(basicFields.length(), basicEditors.size)

                for (index in 0 until basicFields.length()) {
                    val id = basicFields.getJSONObject(index).getString("id")
                    assertTrue(
                        "The authored page-17 $id sample can be imported",
                        basicEditors[index].setLatex(basicValidators.getJSONObject(id).getString("expected"))
                    )
                }

                tree(content)
                    .filterIsInstance<Button>()
                    .single { it.text.toString() == "Comprobar" }
                    .performClick()

                assertTrue(
                    "Page 17 must be accepted before navigating to page 18",
                    tree(content).filterIsInstance<TextView>().any { it.text.toString() == "Correcto ✓" }
                )

                activity.findViewById<Button>(R.id.buttonContinue).performClick()

                assertEquals("18 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())

                val editors = tree(content).filterIsInstance<MathExpressionEditor>()
                assertEquals(fields.length(), editors.size)
                for (index in 1 until fields.length()) {
                    val id = fields.getJSONObject(index).getString("id")
                    assertTrue("The authored $id sample can be imported", editors[index].setLatex(validators.getJSONObject(id).getString("expected")))
                }
                val editor = editors.first()
                assertTrue(editor.requestFocus())
                editor.perform(MathKeyboardAction.Integral)
                select(editor, listOf(MathCursorStep(0, "upper")), 0)
                type(editor, "1")
                select(editor, listOf(MathCursorStep(0, "lower")), 0)
                type(editor, "0")

                // LargeOperator owns only its bounds. The integrand is the next independent
                // parent in the containing Sequence.
                select(editor, emptyList(), 1)
                type(editor, "x")
                editor.perform(MathKeyboardAction.Power)
                type(editor, "2")

                // Exit x² and write the differential as further direct siblings.
                select(editor, emptyList(), 2)
                type(editor, "d")
                type(editor, "x")

                val integral = editor.model.expression.children.first() as MathExpression.LargeOperator
                assertEquals(MathLargeOperator.INTEGRAL, integral.kind)
                assertEquals("1", MathLatexSerializer.serialize(requireNotNull(integral.upper)))
                assertEquals("0", MathLatexSerializer.serialize(requireNotNull(integral.lower)))

                val integrand = editor.model.expression.children[1]
                assertTrue(integrand is MathExpression.Scripts)
                assertEquals("{x}^{2}", MathLatexSerializer.serialize(integrand))
                assertEquals(
                    listOf(MathExpression.Variable("d"), MathExpression.Variable("x")),
                    editor.model.expression.children.drop(2)
                )
                assertTrue("All seven authored advanced samples are structurally complete", editors.all { it.isComplete() })
                serializedAnswers = editors.map { it.getLatex() }
                val expected = validators.getJSONObject("integral").getString("expected")
                val diagnostics = "expected='$expected', actual='${editor.getLatex()}', AST=${editor.model.expression}"
                tree(content).filterIsInstance<Button>().single { it.text.toString() == "Comprobar" }.performClick()
                val feedback = tree(content).filterIsInstance<TextView>().map { it.text.toString() }
                assertTrue("A correct visually constructed integral must resolve page 18; $diagnostics; feedback=$feedback",
                    feedback.any { it == "Correcto ✓" })
                assertTrue("Accepted advanced expressions lock after checking", editors.all { !it.isEnabled })
                assertEquals(2, ProgressRepository.getLessonPageProgress(activity, route, lesson).completed)
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                // Debug startup deliberately returns to page 17. Re-enter the already
                // accepted advanced page through the app before asserting its saved answers.
                assertEquals("17 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
                activity.findViewById<Button>(R.id.buttonContinue).performClick()
                assertEquals("18 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val editors = tree(content).filterIsInstance<MathExpressionEditor>()
                assertEquals("The actual serialized answers persist across recreation", serializedAnswers, editors.map { it.getLatex() })
                assertTrue(editors.all { !it.isEnabled })
                assertTrue(tree(content).filterIsInstance<TextView>().any { it.text.toString() == "Correcto ✓" })
                assertEquals(2, ProgressRepository.getLessonPageProgress(activity, route, lesson).completed)
            }
        }
    }

    private fun puzzleBlock(pageIndex: Int): JSONObject {
        val source = context.assets.open("content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json")
            .bufferedReader().use { it.readText() }
        val blocks = JSONObject(source).getJSONArray("pages").getJSONObject(pageIndex).getJSONArray("blocks")
        return (0 until blocks.length()).map { blocks.getJSONObject(it) }.single { it.optString("type") == "math_puzzle" }
    }

    private fun advancedBlock(): JSONObject = puzzleBlock(17)

    private fun select(editor: MathExpressionEditor, path: List<MathCursorStep>, offset: Int) {
        requireNotNull(editor.onCursorRequested).invoke(MathCursor(path, offset))
        assertEquals(path, editor.model.cursor.path)
        assertEquals(offset, editor.model.cursor.offset)
    }
    private fun type(editor: MathExpressionEditor, value: String) =
        assertTrue(requireNotNull(editor.onCreateInputConnection(EditorInfo())).commitText(value, 1))
    private fun tree(view: View): List<View> = listOf(view) +
            if (view is ViewGroup) (0 until view.childCount).flatMap { tree(view.getChildAt(it)) } else emptyList()
    private fun intent() = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", route).putExtra("courseId", "math_course_01")
        .putExtra("unitId", "arithmetic_fundamentals").putExtra("lessonId", lesson).putExtra("lessonNumber", 4)
}

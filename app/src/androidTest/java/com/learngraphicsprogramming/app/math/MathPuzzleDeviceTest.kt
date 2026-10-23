package com.learngraphicsprogramming.app.math

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.RadioButton
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import com.learngraphicsprogramming.app.LessonActivity
import com.learngraphicsprogramming.app.LessonTextSizeSettingsRepository
import com.learngraphicsprogramming.app.ProgressRepository
import com.learngraphicsprogramming.app.R
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Exercises actual Android Views and the existing lesson lifecycle. User preferences are restored. */
@RunWith(AndroidJUnit4::class)
class MathPuzzleDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val route = "math_physics"
    private val lesson = "math_course_01__arithmetic_fundamentals__math_puzzle_sandbox"
    private val snapshots = linkedMapOf<String, Map<String, *>>()

    @Before fun isolateSandboxAndPreserveUserPreferences() {
        wakeDevice()
        listOf("learning_progress", "navigation_state", "lesson_text_size_settings").forEach {
            snapshots[it] = context.getSharedPreferences(it, Context.MODE_PRIVATE).all.toMap()
        }
        ProgressRepository.resetLesson(context, route, lesson)
    }

    @After fun restoreUserPreferences() {
        snapshots.forEach { (name, original) ->
            val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
            val editor = preferences.edit()
            (preferences.all.keys - original.keys).forEach(editor::remove)
            original.forEach { (key, value) ->
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            assertTrue(editor.commit())
        }
    }

    @Test fun compoundAnswerRestoresLocksAndResetBlocksContinuation() {
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val inputs = descendants(content).filterIsInstance<EditText>()
                assertEquals(3, inputs.size)
                val check = button(content, "Comprobar")
                inputs[0].setText("7,5")
                inputs[1].setText("6/8")
                check.performClick()
                activity.findViewById<Button>(R.id.buttonContinue).performClick()
                assertEquals("1 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
                assertEquals(0, ProgressRepository.getLessonPageProgress(activity, route, lesson).completed)
                inputs[2].setText("0.33")
                check.performClick()
                check.performClick()
                assertEquals(1, ProgressRepository.getLessonPageProgress(activity, route, lesson).completed)
                assertTrue(inputs.all { !it.isEnabled })
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val inputs = descendants(content).filterIsInstance<EditText>()
                assertTrue(inputs.all { !it.isEnabled })
                assertEquals(listOf("7,5", "6/8", "0.33"), inputs.map { it.text.toString() })
                button(content, "Reiniciar").performClick()
                assertTrue(inputs.all { it.isEnabled && it.text.isEmpty() })
                assertEquals(0, ProgressRepository.getLessonPageProgress(activity, route, lesson).completed)
                activity.findViewById<Button>(R.id.buttonContinue).performClick()
                assertEquals("1 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
                inputs[0].setText("7.5")
                inputs[1].setText("3/4")
                inputs[2].setText("0,33")
                button(content, "Comprobar").performClick()
                activity.findViewById<Button>(R.id.buttonContinue).performClick()
                assertEquals("2 de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
            }
        }
    }

    @Test fun everySandboxPageRendersAndFullscreenExitStaysVisibleAtLargeTextSize() {
        LessonTextSizeSettingsRepository.saveUsesGlobalScale(context, "math_course_01", true)
        LessonTextSizeSettingsRepository.saveGlobalScale(context, 150)
        for (page in 0 until 18) {
            wakeDevice()
            ProgressRepository.saveCurrentPage(context, route, lesson, page)
            ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
                scenario.onActivity { activity ->
                    val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                    assertFalse("Página ${page + 1}: JSON no renderizado", descendants(content).filterIsInstance<TextView>().any { it.text.contains("No se pudo cargar") })
                    assertTrue(descendants(content).any { it.tag == MathPuzzleRenderer.INTERACTION_TAG })
                    assertEquals("${page + 1} de 18", activity.findViewById<TextView>(R.id.textProgress).text.toString())
                    button(content, "Pantalla completa").performClick()
                }
                instrumentation.waitForIdleSync()
                onView(withContentDescription("Salir de pantalla completa")).check(matches(isDisplayed()))
                scenario.onActivity { activity ->
                    val exit = button(activity.window.decorView, "Salir de pantalla completa")
                    val bounds = Rect()
                    assertTrue("Salida fija visible en página ${page + 1}", exit.getGlobalVisibleRect(bounds))
                    assertTrue(bounds.width() > 0 && bounds.height() > 0)
                }
                if (page in listOf(0, 2, 3, 5, 8, 9, 10, 13, 14, 15)) screenshot("sandbox-page-${page + 1}.png")
                scenario.onActivity { activity ->
                    button(activity.window.decorView, "Salir de pantalla completa").performClick()
                    assertTrue(descendants(activity.findViewById<ViewGroup>(R.id.lessonContent)).any { it.tag == MathPuzzleRenderer.INTERACTION_TAG })
                }
            }
        }
    }

    @Test fun selectionOrderMatchingCoordinatesGeometryAndSlidersCanBeAnswered() {
        withPage(1) { content ->
            descendants(content).filterIsInstance<RadioButton>().first { it.contentDescription == "y = 2x + 1" || it.text == "y = 2x + 1" }.performClick()
            assertPuzzleCorrect(content)
            button(content, "Reiniciar").performClick()
            assertTrue(descendants(content).filterIsInstance<RadioButton>().none { it.isChecked })
        }
        withPage(2) { content ->
            descendants(content).filterIsInstance<CheckBox>().filter { it.contentDescription != "2/3" && it.text.toString() != "2/3" }.forEach { it.performClick() }
            assertPuzzleCorrect(content)
        }
        withPage(7) { content ->
            listOf("n3", "n0", "n2", "n5", "n8").forEach { id -> descendants(content).first { it.contentDescription?.startsWith("Ordenar tarjeta $id:") == true }.performClick() }
            descendants(content).filterIsInstance<EditText>().single().setText("2")
            assertPuzzleCorrect(content)
        }
        withPage(8) { content ->
            listOf("2 + 3" to "5", "3 × 4" to "12", "9 − 2" to "7").forEach { (source, target) ->
                descendants(content).first { it.contentDescription == "Seleccionar ficha $source" }.performClick()
                val label = descendants(content).filterIsInstance<TextView>().first { it.text.toString() == target }
                ((label.parent as View).parent as View).performClick()
            }
            assertPuzzleCorrect(content)
        }
        withPage(10) { content ->
            val inputs = descendants(content).filterIsInstance<EditText>()
            assertEquals(3, inputs.size)
            listOf("2", "3", "2").forEachIndexed { i, value -> inputs[i].setText(value) }
            assertPuzzleCorrect(content)
        }
        withPage(13) { content ->
            val inputs = descendants(content).filterIsInstance<EditText>()
            assertEquals(8, inputs.size)
            listOf("0", "0", "4", "0", "4", "2", "0", "2").forEachIndexed { i, value -> inputs[i].setText(value) }
            assertPuzzleCorrect(content)
        }
        withPage(14) { content ->
            val bars = descendants(content).filterIsInstance<SeekBar>()
            assertEquals(2, bars.size)
            repeat(2) { bars[0].onKeyDown(android.view.KeyEvent.KEYCODE_DPAD_RIGHT, android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_DPAD_RIGHT)) }
            bars[1].onKeyDown(android.view.KeyEvent.KEYCODE_DPAD_RIGHT, android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_DPAD_RIGHT))
            assertPuzzleCorrect(content)
        }
    }

    @Test fun inequalityUsesValueBoundaryAndDirectionTogether() {
        ProgressRepository.saveCurrentPage(context, route, lesson, 9)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val inputs = descendants(content).filterIsInstance<EditText>()
                inputs[0].setText("2")
                inputs[1].setText("3")
                descendants(content).filterIsInstance<CheckBox>().single().performClick()
                descendants(content).filterIsInstance<Spinner>().single().setSelection(0)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                descendants(content).filterIsInstance<Button>().filter { it.text == "Comprobar" }.forEach { it.performClick() }
                assertEquals(2, descendants(content).filterIsInstance<Button>().count { it.text == "Correcto ✓" })
            }
        }
    }

    @Test fun existingProgrammingChoiceAndCodePagesStillRenderAndValidate() {
        val programmingLesson = "cpp_fundamentals__intro_programming__what_is_programming"
        ProgressRepository.resetExerciseState(context, "programming", programmingLesson, 2, 0)
        ProgressRepository.saveCurrentPage(context, "programming", programmingLesson, 2)
        val programmingIntent = Intent(context, LessonActivity::class.java)
            .putExtra("routeId", "programming").putExtra("courseId", "cpp_fundamentals")
            .putExtra("lessonId", programmingLesson).putExtra("lessonNumber", 1)
        ActivityScenario.launch<LessonActivity>(programmingIntent).use { scenario ->
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                val radios = descendants(content).filterIsInstance<RadioButton>()
                assertEquals(3, radios.size)
                radios[1].performClick()
                button(content, "Comprobar").performClick()
                assertTrue(ProgressRepository.getExerciseState(activity, programmingLesson, 2, 0)?.optBoolean("correct") == true)
            }
        }
        ProgressRepository.saveCurrentPage(context, "programming", programmingLesson, 1)
        ActivityScenario.launch<LessonActivity>(programmingIntent).use { scenario ->
            scenario.onActivity { activity ->
                val content = activity.findViewById<ViewGroup>(R.id.lessonContent)
                assertTrue(descendants(content).filterIsInstance<TextView>().any { it.text.contains("Hola desde C++") })
                assertFalse(descendants(content).any { it.tag == MathPuzzleRenderer.INTERACTION_TAG })
            }
        }
    }

    private fun withPage(page: Int, action: (ViewGroup) -> Unit) {
        wakeDevice()
        ProgressRepository.saveCurrentPage(context, route, lesson, page)
        ActivityScenario.launch<LessonActivity>(intent()).use { scenario ->
            scenario.onActivity { activity -> action(activity.findViewById<ViewGroup>(R.id.lessonContent)) }
        }
    }

    private fun assertPuzzleCorrect(content: ViewGroup) {
        button(content, "Comprobar").performClick()
        assertTrue("La respuesta debe validarse en los controles reales", button(content, "Correcto ✓").isEnabled.not())
    }

    private fun intent(): Intent = Intent(context, LessonActivity::class.java)
        .putExtra("routeId", route).putExtra("courseId", "math_course_01")
        .putExtra("lessonId", lesson).putExtra("lessonNumber", 4)

    private fun descendants(view: View): List<View> = listOf(view) +
        if (view is ViewGroup) (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) } else emptyList()

    private fun button(view: View, label: String): View = descendants(view).first { (it is Button && it.text.toString() == label) || it.contentDescription?.toString() == label }

    private fun screenshot(name: String) {
        instrumentation.waitForIdleSync()
        // Let the system's Activity/fullscreen transition finish before visual QA.
        android.os.SystemClock.sleep(800)
        val bitmap = instrumentation.uiAutomation.takeScreenshot() ?: error("No se pudo capturar la pantalla del sandbox")
        // Shell-owned QA captures survive removal of Gradle's temporary test installation.
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            require(name.matches(Regex("[A-Za-z0-9._-]+"))) { "Invalid screenshot file name" }
            android.os.ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(
                "mkdir -p /data/local/tmp/math-puzzle-smoke"
            )).use { it.readBytes() }
            // Some emulators expose a dd symlink without implementing the tool.
            // UiAutomation starts the process directly; keep the shell command
            // as one argument so its binary stdin reaches cat's redirection.
            val streams = instrumentation.uiAutomation.executeShellCommandRw(
                "/system/bin/sh -c /system/bin/cat>/data/local/tmp/math-puzzle-smoke/$name"
            )
            android.os.ParcelFileDescriptor.AutoCloseInputStream(streams[0]).use { output ->
                android.os.ParcelFileDescriptor.AutoCloseOutputStream(streams[1]).use {
                    assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
                }
                output.readBytes()
            }
        }
        bitmap.recycle()
    }

    private fun wakeDevice() {
        // Dismisses only an unsecured development lock screen; authenticated locks remain protected.
        for (command in listOf("input keyevent KEYCODE_WAKEUP", "wm dismiss-keyguard")) {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).use { it.readBytes() }
        }
    }
}

package com.learngraphicsprogramming.app

import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import java.io.File
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Actual lesson controls and authored answers, isolated from every existing progress key. */
@RunWith(AndroidJUnit4::class)
class CppLessonAnswerTabsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalFiles = emptySet<String>()
    private var captured = false

    @Before fun preserveQaPreferences() {
        assertEquals("com.learngraphicsprogramming.app.realdownloadsqa", context.packageName)
        assertEquals(context.packageName, BuildConfig.APPLICATION_ID)
        originalFiles = preferenceFiles()
        originalFiles.forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
        NarrationSettingsRepository.saveEnabled(context, false)
        PremiumRepository.setVerifiedPremium(context)
    }

    @After fun restoreQaPreferences() {
        if (!captured) return
        instrumentation.waitForIdleSync()
        (preferenceFiles() + snapshots.keys).forEach { name ->
            if (name !in originalFiles) assertTrue(context.deleteSharedPreferences(name))
            else {
                val target = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                val edit = target.edit().clear()
                snapshots.getValue(name).forEach { (key, value) ->
                    when (value) {
                        is String -> edit.putString(key, value)
                        is Int -> edit.putInt(key, value)
                        is Long -> edit.putLong(key, value)
                        is Float -> edit.putFloat(key, value)
                        is Boolean -> edit.putBoolean(key, value)
                        is Set<*> -> edit.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference $name/$key")
                    }
                }
                assertTrue(edit.commit())
                assertEquals(snapshots[name], target.all)
            }
        }
        assertEquals(originalFiles, preferenceFiles())
    }

    @Test fun authoredFillCodeAnswerRetainsNativeTabSelectionAndItsExistingEvaluation() =
        exercise("cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida", 2, 5, "fill_code")

    @Test fun authoredInlineBlanksUseTabNavigationWithoutModifyingTheirAnswers() =
        exercise("cpp_fundamentals__expresiones_y_decisiones__combinar_condiciones", 2, 4, "inline_fill_code")

    @Test fun theScreenshotCourseOneLessonThreeTwoBlanksUseTheSameTabNavigation() =
        exercise("cpp_fundamentals__programas_y_ejecucion__texto_secuencia_y_formato", 5, 2, "inline_fill_code")

    private fun exercise(sourceId: String, sourcePage: Int, sourceBlock: Int, type: String) {
        val authored = LessonRepository.loadLessonJson(context, "programming", sourceId)
        val original = authored.toString()
        val block = JSONObject(authored.getJSONArray("pages").getJSONObject(sourcePage - 1)
            .getJSONArray("blocks").getJSONObject(sourceBlock - 1).toString())
        assertEquals(type, block.getString("type"))
        val fixtureId = "cpp_fundamentals__programas_y_ejecucion__tab_answer_${UUID.randomUUID().toString().replace("-", "")}"
        val fixture = JSONObject().put("id", fixtureId).put("title", authored.getString("title"))
            .put("pages", JSONArray().put(JSONObject().put("blocks", JSONArray().put(block))))
        ProgressRepository.saveCurrentPage(context, "programming", SOURCE, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "programas_y_ejecucion")
            .putExtra("lessonId", SOURCE).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            val deadline = SystemClock.uptimeMillis() + 12_000
            var ready = false
            while (!ready && SystemClock.uptimeMillis() < deadline) {
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity -> ready = activity.hasWindowFocus() &&
                    (field(activity, "lessonJson") as JSONObject).optString("id") == SOURCE }
                if (!ready) SystemClock.sleep(50)
            }
            assertTrue("The source Activity must finish startup before one-time injection", ready)
            scenario.onActivity { activity ->
                setField(activity, "currentLessonId", fixtureId)
                setField(activity, "lessonJson", fixture)
                setField(activity, "totalPages", 1)
                setField(activity, "currentPage", 0)
                for (name in listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises")) {
                    (field(activity, name) as MutableMap<*, *>).clear()
                }
                LessonActivity::class.java.getDeclaredMethod("renderCurrentPage").apply { isAccessible = true }.invoke(activity)
            }
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                assertEquals(fixtureId, field(activity, "currentLessonId"))
                val inputs = descendants(activity.window.decorView).filterIsInstance<EditText>().filter { it.tag == "cpp_code_input" }
                val answers = block.getJSONArray("answers")
                assertEquals(if (type == "fill_code") 1 else answers.length(), inputs.size)
                inputs.forEachIndexed { index, input ->
                    assertTrue("The actual lesson input owns native TAB geometry", input is FreeScrollEditText)
                    input.showSoftInputOnFocus = false
                    assertTrue(input.requestFocus())
                    if (type == "inline_fill_code" && inputs.size > 1) {
                        val following = inputs[(index + 1) % inputs.size]
                        val preceding = inputs[(index - 1 + inputs.size) % inputs.size]
                        input.setText("prefix")
                        input.setSelection(3)
                        assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_TAB)))
                        assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_TAB)))
                        assertSame("Physical Tab focuses the next authored blank", following, activity.currentFocus)
                        assertEquals("Tab must not change the source answer", "prefix", input.text.toString())
                        assertEquals("The original caret stays intact", 3, input.selectionStart)
                        assertTrue(following.dispatchKeyEvent(KeyEvent(0, 0,
                            KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_TAB, 0, KeyEvent.META_SHIFT_ON)))
                        assertTrue(following.dispatchKeyEvent(KeyEvent(0, 0,
                            KeyEvent.ACTION_UP, KeyEvent.KEYCODE_TAB, 0, KeyEvent.META_SHIFT_ON)))
                        assertSame("Shift+Tab focuses the previous blank", input, activity.currentFocus)
                    } else {
                        for ((start, end) in listOf(1 to 5, 5 to 1)) {
                            input.setText("prefix")
                            input.setSelection(start, end)
                            assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_TAB)))
                            assertTrue(input.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_TAB)))
                            assertEquals("p\tx", input.text.toString())
                            assertEquals(2, input.selectionStart)
                            assertEquals(2, input.selectionEnd)
                            assertSame(input, activity.currentFocus)
                        }
                    }
                    val answer = answers.getString(if (type == "fill_code") 0 else index)
                    input.setText(if (type == "fill_code") "\t$answer\t" else answer)
                }
                val check = descendants(activity.window.decorView).filterIsInstance<Button>()
                    .single { it.text.toString() == activity.getString(R.string.exercise_check) }
                assertTrue(check.isEnabled)
                check.performClick()
                val saved = requireNotNull(ProgressRepository.getExerciseState(activity, fixtureId, 0, 0))
                assertTrue("The unmodified authored answer remains correct with irrelevant outer TABs", saved.getBoolean("correct"))
                assertEquals(1, field(activity, "completedExercises"))
                assertFalse(check.isEnabled)
            }
        }
        assertEquals("The original curriculum is untouched", original,
            LessonRepository.loadLessonJson(context, "programming", sourceId).toString())
    }

    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java
        .getDeclaredField(name).apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java
        .getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
    private fun descendants(view: View): List<View> = buildList {
        add(view)
        if (view is ViewGroup) for (index in 0 until view.childCount) addAll(descendants(view.getChildAt(index)))
    }
    private fun preferenceFiles() = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.extension == "xml" }.map { it.nameWithoutExtension }.toSet()

    companion object {
        private const val SOURCE = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
    }
}

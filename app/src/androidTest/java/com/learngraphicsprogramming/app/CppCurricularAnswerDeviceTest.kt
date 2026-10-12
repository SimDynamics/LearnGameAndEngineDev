package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID

/**
 * Checks the published C++ answers through LessonActivity's actual native controls.
 * Each copied block retains its authored page/block coordinate but uses a unique
 * lesson storage identity. The normal app is rejected before accessing preferences;
 * all affected curricularqa preferences are restored even after an assertion fails.
 */
@RunWith(AndroidJUnit4::class)
class CppCurricularAnswerDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()
    private var preferencesCaptured = false
    private var screenGuardRegistered = false
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity && activity.packageName == QA_PACKAGE) {
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun requireQaAndPreserveItsPreferences() {
        assertEquals("Use -Plgp.curricularQa=true; never run this class against the normal app",
            QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock LDPlayer before renderer verification", keyguard.isDeviceLocked)
        assertFalse("Dismiss LDPlayer's visual lock screen before renderer verification", keyguard.isKeyguardLocked)
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + PREFS).forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        preferencesCaptured = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        screenGuardRegistered = true
        NarrationSettingsRepository.saveEnabled(context, false)
        // Answer semantics are the subject of these tests; an exhausted fixture
        // reserve must not prevent a comparison from running.
        PremiumRepository.setVerifiedPremium(context)
        assertTrue("The isolated fixture can evaluate every answer without exhausting its reserve",
            AttemptRepository.getState(context, ROUTE).unlimited)
    }

    @After fun restoreAllQaPreferences() {
        if (context.packageName != QA_PACKAGE || BuildConfig.APPLICATION_ID != QA_PACKAGE || !preferencesCaptured) return
        try {
            instrumentation.waitForIdleSync()
            (preferenceFiles() + snapshots.keys).forEach { name ->
                if (name !in originalPreferenceFiles) {
                    assertTrue("Delete only a preference file created in curricularqa by this test: $name",
                        context.deleteSharedPreferences(name))
                    return@forEach
                }
                val original = snapshots.getValue(name)
                val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                val editor = preferences.edit().clear()
                original.forEach { (key, value) ->
                    when (value) {
                        is String -> editor.putString(key, value)
                        is Int -> editor.putInt(key, value)
                        is Long -> editor.putLong(key, value)
                        is Float -> editor.putFloat(key, value)
                        is Boolean -> editor.putBoolean(key, value)
                        is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                        else -> error("Unsupported preference value for $name/$key: $value")
                    }
                }
                assertTrue("Restore $name", editor.commit())
                assertEquals("Restore $name including every original progress key", original, preferences.all)
            }
            assertEquals("Restore the original set of QA preference files", originalPreferenceFiles, preferenceFiles())
        } finally {
            if (screenGuardRegistered) {
                (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
                screenGuardRegistered = false
            }
        }
    }

    @Test fun fiveExactPredictionsRejectAddedEdgeCharacters() {
        for (location in EXACT_OUTPUTS) {
            withCopiedBlock(location) { scenario, fixture ->
                assertEquals("output_prediction", fixture.block.getString("type"))
                assertEquals("The published block selects exact comparison", "exact",
                    fixture.block.getString("outputComparison"))
                for (answer in strings(fixture.block.getJSONArray("answers"))) {
                    assertResponse(scenario, fixture, listOf(answer), true)
                    for (suffix in listOf("\n", " ", "\t")) {
                        assertResponse(scenario, fixture, listOf(answer + suffix), false)
                    }
                    if (location == FIRST_OUTPUT) {
                        assertResponse(scenario, fixture, listOf(" $answer"), false)
                    }
                }
            }
        }
    }

    @Test fun otherPredictionsKeepTrimToleranceAndSignificantInternalFormatting() {
        for ((location, altered) in listOf(LINE_OUTPUT to "Entrada Salida", ALIGNED_OUTPUT to "[ 8]|[9]")) {
            withCopiedBlock(location) { scenario, fixture ->
                assertEquals("output_prediction", fixture.block.getString("type"))
                assertFalse("No exact criterion was added to unrelated predictions",
                    fixture.block.has("outputComparison"))
                val answer = fixture.block.getJSONArray("answers").getString(0)
                assertResponse(scenario, fixture, listOf(answer), true)
                assertResponse(scenario, fixture, listOf(" \t\n$answer\n\t "), true)
                assertResponse(scenario, fixture, listOf(altered), false)
            }
        }
    }

    @Test fun operatorAlternativesAreIndependentAndDoNotCreateAdditionalFields() {
        withCopiedBlock(LOGIC) { scenario, fixture ->
            assertPublishedAlternatives(fixture, listOf(listOf("&&", "and"), listOf("||", "or")))
            for (first in listOf("&&", "and")) {
                for (second in listOf("||", "or")) {
                    assertResponse(scenario, fixture, listOf(first, second), true)
                }
            }
            for (answers in listOf(listOf("||", "&&"), listOf("or", "and"),
                listOf("&&", "&&"), listOf("||", "||"))) {
                assertResponse(scenario, fixture, answers, false)
            }
        }
        withCopiedBlock(COMPARISON) { scenario, fixture ->
            assertPublishedAlternatives(fixture, listOf(listOf("=="), listOf("!=", "not_eq")))
            assertResponse(scenario, fixture, listOf("==", "!="), true)
            assertResponse(scenario, fixture, listOf("==", "not_eq"), true)
            for (answers in listOf(listOf("!=", "=="), listOf("not_eq", "=="), listOf("==", "=="))) {
                assertResponse(scenario, fixture, answers, false)
            }
        }
    }

    @Test fun vectorAlternativesAcceptOnlyTheRequiredOperationAndValue() {
        for ((location, value, wrongValue) in listOf(
            Triple(EMPTY_VECTOR, "9", "8"), Triple(SELECTED_VECTOR, "valor", "origen"))) {
            withCopiedBlock(location) { scenario, fixture ->
                assertPublishedAlternatives(fixture, listOf(listOf("push_back", "emplace_back"), listOf(value)))
                for (operation in listOf("push_back", "emplace_back")) {
                    assertResponse(scenario, fixture, listOf(operation, value), true)
                    assertResponse(scenario, fixture, listOf(operation, wrongValue), false)
                }
                for (operation in listOf("pop_back", "insert", "assign", "resize", "clear")) {
                    assertResponse(scenario, fixture, listOf(operation, value), false)
                }
            }
        }
    }

    @Test fun fillLiteralsPreserveDuplicateSpacesTabsAndRealNewlines() {
        for (location in listOf(MUELLE_LITERAL, REPORT_LITERAL)) {
            withCopiedBlock(location) { scenario, fixture ->
                assertEquals("fill_code", fixture.block.getString("type"))
                for (literal in strings(fixture.block.getJSONArray("answers"))) {
                    assertResponse(scenario, fixture, listOf(literal), true)
                    assertResponse(scenario, fixture, listOf(" \t$literal\n "), true)
                    for (replacement in listOf("  ", "\t", "\n")) {
                        assertResponse(scenario, fixture, listOf(literal.replaceFirst(" ", replacement)), false)
                    }
                    if (location == REPORT_LITERAL) {
                        assertTrue("The published source uses an escape, not a real source newline", literal.contains("\\n"))
                        assertResponse(scenario, fixture, listOf(literal.replace("\\n", "\n")), false)
                    }
                }
            }
        }
    }

    @Test fun inlineLiteralPreservesItsContentAndAllowsOuterWhitespace() {
        withCopiedBlock(CARGA_LITERAL) { scenario, fixture ->
            val answers = strings(fixture.block.getJSONArray("answers"))
            assertEquals(listOf("<<", "\"Carga lista\""), answers)
            assertResponse(scenario, fixture, answers, true)
            assertResponse(scenario, fixture, listOf(" \t<< ", " \t\"Carga lista\" "), true)
            for (replacement in listOf("  ", "\t", "\n")) {
                assertResponse(scenario, fixture, listOf("<<", answers[1].replaceFirst(" ", replacement)), false)
            }
            assertResponse(scenario, fixture, listOf("<", answers[1]), false)
        }
    }

    @Test fun oldAnswerArraysStillAcceptTheirOperatorsAndTypes() {
        for (location in listOf(LEGACY_TYPE, LEGACY_LOGIC)) {
            withCopiedBlock(location) { scenario, fixture ->
                assertEquals("fill_code", fixture.block.getString("type"))
                assertFalse(fixture.block.has("answerAlternatives"))
                for (answer in strings(fixture.block.getJSONArray("answers"))) {
                    assertResponse(scenario, fixture, listOf(" \t${answer.replace(" ", " \t  ")}\n "), true)
                }
            }
        }
        withCopiedBlock(CARGA_LITERAL) { scenario, fixture ->
            assertFalse("The unchanged inline block still uses only the original answers contract",
                fixture.block.has("answerAlternatives"))
            assertEquals(2, fixture.block.getJSONArray("answers").length())
            assertResponse(scenario, fixture, strings(fixture.block.getJSONArray("answers")), true)
        }
    }

    @Test fun existingHolesInsideQuotedTemplatesPreserveTheirEdgeCharacters() {
        withCopiedBlock(QUOTED_TOKEN) { scenario, fixture ->
            assertEquals("Token t{nombre, [], \"[]\"};\n", fixture.block.getString("template"))
            assertEquals(listOf("0", "Mesa9"), strings(fixture.block.getJSONArray("answers")))
            assertResponse(scenario, fixture, listOf("0", "Mesa9"), true)
            assertResponse(scenario, fixture, listOf(" \t0\n ", "Mesa9"), true)
            for (edge in listOf(" ", "\t", "\n")) {
                assertResponse(scenario, fixture, listOf("0", edge + "Mesa9"), false)
                assertResponse(scenario, fixture, listOf("0", "Mesa9" + edge), false)
            }
        }
        for (location in QUOTED_FILL_HOLES) {
            withCopiedBlock(location) { scenario, fixture ->
                assertEquals("fill_code", fixture.block.getString("type"))
                val answer = fixture.block.getJSONArray("answers").getString(0)
                assertResponse(scenario, fixture, listOf(answer), true)
                for (edge in listOf(" ", "\t", "\n")) {
                    assertResponse(scenario, fixture, listOf(edge + answer), false)
                    assertResponse(scenario, fixture, listOf(answer + edge), false)
                }
            }
        }
    }

    @Test fun draftsCompletionAndCoordinatesSurviveReconstructionAndRecreation() {
        for (location in listOf(FIRST_OUTPUT, MUELLE_LITERAL, LOGIC, CARGA_LITERAL)) {
            withCopiedBlock(location) { scenario, fixture ->
                val valid = if (location == LOGIC) listOf("and", "or")
                    else strings(fixture.block.getJSONArray("answers"))
                val invalid = valid.toMutableList().apply {
                    val index = if (location == CARGA_LITERAL) 1 else 0
                    this[index] = if (location == LOGIC) "or" else this[index].replaceFirst(" ", "\t")
                }
                assertResponse(scenario, fixture, invalid, false)
                rebuild(scenario, fixture)
                assertRestoredResponse(scenario, fixture, invalid, false)
                scenario.recreate()
                scenario.onActivity { injectFixture(it, fixture) }
                assertRestoredResponse(scenario, fixture, invalid, false)
                assertResponse(scenario, fixture, valid, true)
                rebuild(scenario, fixture)
                assertRestoredResponse(scenario, fixture, valid, true)
                scenario.recreate()
                scenario.onActivity { injectFixture(it, fixture) }
                assertRestoredResponse(scenario, fixture, valid, true)
                scenario.onActivity { activity ->
                    val key = "exercise_${fixture.id}_page_${location.page - 1}_block_${location.block - 1}"
                    assertTrue("Completion keeps the original zero-based page/block storage coordinates",
                        context.getSharedPreferences(PROGRESS, Context.MODE_PRIVATE).contains(key))
                    assertEquals(1, field(activity, "requiredExercises"))
                    assertEquals(1, field(activity, "completedExercises"))
                }
            }
        }
    }

    private fun withCopiedBlock(location: Location,
                                test: (ActivityScenario<LessonActivity>, Fixture) -> Unit) {
        val source = LessonRepository.loadLessonJson(context, ROUTE, location.lesson)
        assertEquals(location.lesson, source.getString("id"))
        val block = JSONObject(source.getJSONArray("pages").getJSONObject(location.page - 1)
            .getJSONArray("blocks").getJSONObject(location.block - 1).toString())
        val id = "cpp_fundamentals__programas_y_ejecucion__answer_device_${UUID.randomUUID().toString().replace("-", "") }"
        val pages = JSONArray().apply {
            repeat(source.getJSONArray("pages").length()) { pageIndex ->
                put(JSONObject().put("blocks", JSONArray().apply {
                    if (pageIndex == location.page - 1) {
                        repeat(location.block - 1) { put(placeholder()) }
                        put(block)
                    } else put(placeholder())
                }))
            }
        }
        val fixture = Fixture(id, location, block,
            JSONObject().put("id", id).put("title", source.getString("title")).put("pages", pages))
        val progress = context.getSharedPreferences(PROGRESS, Context.MODE_PRIVATE)
        val originalExercises = originalExerciseKeys(progress.all)
        // LessonRepository deliberately has no mutable cache. Launch a known real
        // lesson, then replace only the Activity's cached JSON before interacting.
        // Its launch/navigation preferences are restored by @After as well.
        ProgressRepository.saveCurrentPage(context, ROUTE, FIRST_OUTPUT.lesson, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", "cpp_fundamentals").putExtra("unitId", "programas_y_ejecucion")
            .putExtra("lessonId", FIRST_OUTPUT.lesson).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        try {
            ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
                scenario.onActivity { injectFixture(it, fixture) }
                instrumentation.waitForIdleSync()
                test(scenario, fixture)
            }
        } finally {
            instrumentation.waitForIdleSync()
            assertEquals("Copied curriculum blocks never overwrite an original exercise/progress answer key",
                originalExercises, originalExerciseKeys(progress.all))
        }
    }

    private fun placeholder() = JSONObject().put("type", "paragraph").put("text", "Prueba curricular aislada")

    private fun originalExerciseKeys(values: Map<String, *>) = values.filterKeys {
        it.startsWith("exercise_") && !it.contains("__answer_device_")
    }

    private fun injectFixture(activity: LessonActivity, fixture: Fixture) {
        assertEquals(QA_PACKAGE, activity.packageName)
        setField(activity, "currentLessonId", fixture.id)
        setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
        setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
        setField(activity, "currentPage", fixture.location.page - 1)
        listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
            mapField(activity, it).clear()
        }
        invoke(activity, "renderCurrentPage")
        assertEquals(fixture.id, field(activity, "currentLessonId"))
        assertEquals(fixture.location.page - 1, field(activity, "currentPage"))
        val expectedFields = if (fixture.block.getString("type") == "inline_fill_code")
            Regex("\\[\\]").findAll(fixture.block.getString("template")).count() else 1
        assertEquals("Alternatives do not add blanks to the published template", expectedFields, inputs(activity).size)
        assertEquals(1, field(activity, "requiredExercises"))
    }

    private fun assertPublishedAlternatives(fixture: Fixture, expected: List<List<String>>) {
        assertEquals("inline_fill_code", fixture.block.getString("type"))
        val alternatives = fixture.block.getJSONArray("answerAlternatives")
        assertEquals(expected, (0 until alternatives.length()).map { strings(alternatives.getJSONArray(it)) })
        assertEquals("Legacy answers remain the first choice of the same blanks",
            expected.map { it.first() }, strings(fixture.block.getJSONArray("answers")))
    }

    private fun assertResponse(scenario: ActivityScenario<LessonActivity>, fixture: Fixture,
                               answers: List<String>, expectedCorrect: Boolean) {
        scenario.onActivity { activity ->
            val existing = ProgressRepository.getExerciseState(activity, fixture.id,
                fixture.location.page - 1, fixture.location.block - 1)
            if (existing != null) resetButton(activity).performClick()
            assertEquals(answers.size, inputs(activity).size)
            inputs(activity).forEachIndexed { index, input ->
                input.setText(answers[index])
                assertEquals("Native input preserves the exact test characters", answers[index], input.text.toString())
            }
            val check = checkButton(activity)
            assertTrue("The real Check listener is attached and enabled", check.isAttachedToWindow && check.isEnabled)
            check.performClick()
            assertSavedResponse(activity, fixture, answers, expectedCorrect)
        }
    }

    private fun assertSavedResponse(activity: LessonActivity, fixture: Fixture,
                                    answers: List<String>, expectedCorrect: Boolean) {
        val state = requireNotNull(ProgressRepository.getExerciseState(activity, fixture.id,
            fixture.location.page - 1, fixture.location.block - 1))
        assertEquals("${fixture.location}: $answers", expectedCorrect, state.getBoolean("correct"))
        if (fixture.block.getString("type") == "inline_fill_code") {
            assertEquals("The actual submitted fields are persisted without canonicalizing literals",
                answers, strings(state.getJSONArray("answers")))
        } else assertEquals(answers.single(), state.getString("answer"))
        assertEquals(if (expectedCorrect) 1 else 0, field(activity, "completedExercises"))
        inputs(activity).forEach { assertEquals(!expectedCorrect, it.isEnabled) }
        assertEquals(!expectedCorrect, checkButton(activity).isEnabled)
    }

    private fun assertRestoredResponse(scenario: ActivityScenario<LessonActivity>, fixture: Fixture,
                                      answers: List<String>, expectedCorrect: Boolean) {
        scenario.onActivity { activity ->
            assertEquals("Reconstruction restores the draft including significant characters",
                answers, inputs(activity).map { it.text.toString() })
            assertSavedResponse(activity, fixture, answers, expectedCorrect)
        }
    }

    private fun rebuild(scenario: ActivityScenario<LessonActivity>, fixture: Fixture) {
        scenario.onActivity { activity ->
            val page = fixture.location.page - 1
            listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                mapField(activity, it).remove(page)
            }
            invoke(activity, "renderCurrentPage")
        }
    }

    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun preferenceFiles(): Set<String> = File(context.applicationInfo.dataDir, "shared_prefs")
        .listFiles().orEmpty().filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun inputs(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<EditText>()
    private fun checkButton(activity: LessonActivity): Button = descendants(content(activity))
        .filterIsInstance<Button>().single {
            it.text.toString() in listOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct))
        }
    private fun resetButton(activity: LessonActivity): View = descendants(content(activity)).single {
        it.contentDescription?.toString() in listOf(activity.getString(R.string.exercise_reset),
            activity.getString(R.string.exercise_reset_text), activity.getString(R.string.exercise_reset_code))
    }
    private fun descendants(root: View): List<View> = buildList {
        add(root)
        if (root is ViewGroup) for (index in 0 until root.childCount) addAll(descendants(root.getChildAt(index)))
    }
    private fun strings(array: JSONArray) = (0 until array.length()).map { array.getString(it) }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.set(activity, value)
    @Suppress("UNCHECKED_CAST")
    private fun mapField(activity: LessonActivity, name: String) = field(activity, name) as MutableMap<Int, Any>
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)

    private data class Location(val lesson: String, val page: Int, val block: Int)
    private data class Fixture(val id: String, val location: Location, val block: JSONObject, val lesson: JSONObject)

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ROUTE = "programming"
        private const val PROGRESS = "learning_progress"
        private const val ATTEMPTS = "attempt_reserve_seconds_test"
        private val PREFS = listOf(PROGRESS, ATTEMPTS, "navigation_state", "lesson_narration_settings",
            "cpp_lesson_drafts", "app_settings", "appearance_settings", "ui_language_settings", "premium_state")
        private val FIRST_OUTPUT = Location("cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida", 2, 4)
        private val EXACT_OUTPUTS = listOf(FIRST_OUTPUT,
            Location("cpp_fundamentals__repeticion_y_terminacion__recorridos_anidados", 10, 3),
            Location("cpp_fundamentals__repeticion_y_terminacion__recorridos_anidados", 12, 6),
            Location("cpp_fundamentals__datos_propios_y_estado_valido__transferir_invariantes_a_un_deposito", 12, 3),
            Location("cpp_fundamentals__datos_propios_y_estado_valido__transferir_invariantes_a_un_deposito", 15, 3))
        private val LINE_OUTPUT = Location("cpp_fundamentals__programas_y_ejecucion__texto_secuencia_y_formato", 2, 5)
        private val ALIGNED_OUTPUT = Location("cpp_fundamentals__archivos_y_programa_completo__ancho_relleno_y_alineacion_de_campos", 2, 2)
        private val LOGIC = Location("cpp_fundamentals__expresiones_y_decisiones__combinar_condiciones", 2, 4)
        private val COMPARISON = Location("cpp_fundamentals__expresiones_y_decisiones__comparaciones_y_condiciones", 3, 6)
        private val EMPTY_VECTOR = Location("cpp_fundamentals__colecciones_y_recorridos__un_vector_para_una_coleccion", 4, 5)
        private val SELECTED_VECTOR = Location("cpp_fundamentals__colecciones_y_recorridos__acumular_contar_y_seleccionar", 5, 4)
        private val MUELLE_LITERAL = Location(FIRST_OUTPUT.lesson, 2, 5)
        private val REPORT_LITERAL = Location("cpp_fundamentals__programas_y_ejecucion__resolver_probar_y_recuperar", 1, 4)
        private val CARGA_LITERAL = Location(LINE_OUTPUT.lesson, 5, 2)
        private val LEGACY_TYPE = Location(EMPTY_VECTOR.lesson, 2, 3)
        private val LEGACY_LOGIC = Location(LOGIC.lesson, 1, 5)
        private val QUOTED_TOKEN = Location("cpp_fundamentals__texto_interpretacion_y_flujos__nombres_del_lenguaje_y_tokens_con_texto", 2, 5)
        private val QUOTED_FILL_HOLES = listOf(Location(ALIGNED_OUTPUT.lesson, 3, 5),
            Location("cpp_fundamentals__archivos_y_programa_completo__bases_y_anchuras_en_cada_campo", 6, 5),
            Location("cpp_fundamentals__archivos_y_programa_completo__precision_por_campo_y_resumenes_formateados", 4, 5),
            Location("cpp_fundamentals__archivos_y_programa_completo__texto_con_campos_y_patrones_literales", 3, 4))
    }
}

package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.RadioButton
import android.widget.TextView
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
import java.security.MessageDigest
import java.util.UUID
import java.util.WeakHashMap

/** Native C++ feedback checks use copied published blocks and fresh storage identities.
 * They never modify curricular assets, the normal app, or the student's existing progress. */
@RunWith(AndroidJUnit4::class)
class CppIncorrectFeedbackDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val session = UUID.randomUUID().toString()
    private val snapshots = linkedMapOf<String, Map<String, *>>()
    private var originalPreferenceFiles = emptySet<String>()
    private var captured = false
    private var lifecycleRegistered = false
    private val resumed = WeakHashMap<LessonActivity, Long>()
    private val screenGuard = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, state: Bundle?) {
            if (activity is LessonActivity && activity.packageName == QA_PACKAGE)
                activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        override fun onActivityResumed(activity: Activity) {
            if (activity is LessonActivity && activity.packageName == QA_PACKAGE)
                resumed[activity] = SystemClock.uptimeMillis()
        }
        override fun onActivityStarted(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit
        override fun onActivityStopped(activity: Activity) = Unit
        override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    @Before fun requireQaAndPreserveAllPreferences() {
        assertEquals("Use curricularqa; never run against the student's application", QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Unlock LDPlayer before native feedback verification", keyguard.isDeviceLocked)
        assertFalse(keyguard.isKeyguardLocked)
        originalPreferenceFiles = preferenceFiles()
        (originalPreferenceFiles + PREFS).forEach { name ->
            snapshots[name] = context.getSharedPreferences(name, Context.MODE_PRIVATE).all.mapValues { (_, value) ->
                if (value is Set<*>) value.toSet() else value
            }
        }
        captured = true
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        lifecycleRegistered = true
        NarrationSettingsRepository.saveEnabled(context, false)
        PremiumRepository.setVerifiedPremium(context)
        assertTrue(AttemptRepository.getState(context, ROUTE).unlimited)
    }

    @After fun restoreEveryPreferenceIncludingPreviousAnswers() {
        if (!captured || context.packageName != QA_PACKAGE || BuildConfig.APPLICATION_ID != QA_PACKAGE) return
        try {
            instrumentation.waitForIdleSync()
            (preferenceFiles() + snapshots.keys).forEach { name ->
                if (name !in originalPreferenceFiles) {
                    assertTrue("Remove only a new QA preference file: $name", context.deleteSharedPreferences(name))
                } else {
                    val original = snapshots.getValue(name)
                    val prefs = context.getSharedPreferences(name, Context.MODE_PRIVATE)
                    val editor = prefs.edit().clear()
                    original.forEach { (key, value) ->
                        when (value) {
                            is String -> editor.putString(key, value)
                            is Int -> editor.putInt(key, value)
                            is Long -> editor.putLong(key, value)
                            is Float -> editor.putFloat(key, value)
                            is Boolean -> editor.putBoolean(key, value)
                            is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                            else -> error("Unsupported preference value $name/$key")
                        }
                    }
                    assertTrue(editor.commit())
                    assertEquals("Restore every previous QA progress value", original, prefs.all)
                }
            }
            assertEquals(originalPreferenceFiles, preferenceFiles())
        } finally {
            if (lifecycleRegistered) {
                (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
                lifecycleRegistered = false
            }
        }
    }

    @Test fun eighteenUnchangedPuzzlesGiveSafeFallbackAndIndependentRetries() =
        exerciseAllLocations(authored = false)

    @Test fun eighteenPublishedPuzzlesAcceptOptionalConceptualGuidanceWithoutRevealingSolutions() =
        exerciseAllLocations(authored = true)

    private fun exerciseAllLocations(authored: Boolean) {
        val mode = if (authored) "authored" else "legacy-fallback"
        var rejected = 0
        var accepted = 0
        val evidence = JSONArray()
        for (location in LOCATIONS) {
            withBlock(location, transform = { if (authored) addReviewedGuidance(it) }) { scenario, fixture ->
                val wrongAnswers = wrongAnswers(fixture.block)
                assertEquals("Two genuinely distinct incorrect attempts per published puzzle", 2, wrongAnswers.distinct().size)
                wrongAnswers.forEachIndexed { index, answer ->
                    scenario.onActivity { activity ->
                        setDraft(activity, fixture, answer)
                        assertEquals("Editing a draft must remove guidance for the previous submission", "", feedback(activity).text.toString())
                        check(activity).performClick()
                        assertDraft(activity, fixture, answer, false)
                        assertReview(activity, fixture, answer, authored)
                        rejected++
                    }
                    if (index == 0) capture(scenario, fixture, "$mode-p${location.page}-b${location.block}-incorrect")
                    rebuild(scenario, fixture)
                    scenario.onActivity { activity ->
                        assertDraft(activity, fixture, answer, false)
                        assertReview(activity, fixture, answer, authored)
                    }
                }
                val correct = correctAnswer(fixture.block)
                scenario.onActivity { activity ->
                    setDraft(activity, fixture, correct)
                    assertEquals("An independent retry clears the previous review before evaluating", "", feedback(activity).text.toString())
                    assertTrue(check(activity).isEnabled)
                    check(activity).performClick()
                    assertDraft(activity, fixture, correct, true)
                    assertTrue("Only success exposes the published complete explanation",
                        feedback(activity).text.toString().contains(fixture.block.getString("explanation")))
                    accepted++
                }
                capture(scenario, fixture, "$mode-p${location.page}-b${location.block}-correct")
                rebuild(scenario, fixture)
                scenario.onActivity { activity ->
                    assertDraft(activity, fixture, correct, true)
                    assertTrue(feedback(activity).text.toString().contains(fixture.block.getString("explanation")))
                }
                evidence.put(JSONObject().put("page", location.page).put("block", location.block)
                    .put("id", fixture.block.getString("id")).put("type", fixture.block.getString("type"))
                    .put("incorrectAttempts", wrongAnswers.size).put("correctAttempts", 1))
            }
        }
        assertEquals(36, rejected)
        assertEquals(18, accepted)
        File(evidenceDirectory(), "$mode-summary.json").writeText(JSONObject().put("mode", mode)
            .put("package", context.packageName).put("rejected", rejected).put("accepted", accepted)
            .put("puzzles", evidence).put("authoredGuidanceIsQaOverlayOnly", authored)
            .put("sourceAssetUntouched", true).toString(2))
    }

    @Test fun legacyCorrectAndIncorrectStatesSurviveAddingReviewsWithoutInventingAnEvaluation() {
        for (location in listOf(Location(1, 9), Location(4, 3), Location(11, 3), Location(14, 4))) {
            val source = LessonRepository.loadLessonJson(context, ROUTE, SOURCE_LESSON).getJSONArray("pages")
                .getJSONObject(location.page - 1).getJSONArray("blocks").getJSONObject(location.block - 1)
            for (wasCorrect in listOf(false, true)) {
                val answer = if (wasCorrect) correctAnswer(source) else wrongAnswers(source).first()
                val legacyState = JSONObject().put("correct", wasCorrect).apply {
                    when (source.getString("type")) {
                        "multiple_choice" -> put("selectedIndex", answer)
                        "fill_code", "output_prediction" -> put("answer", answer)
                        "order_code" -> put("order", JSONArray(answer as List<*>))
                    }
                }
                withBlock(location, transform = ::addReviewedGuidance, savedState = legacyState) { scenario, fixture ->
                    scenario.onActivity { activity ->
                        assertDraft(activity, fixture, answer, wasCorrect)
                        if (wasCorrect) {
                            assertTrue(feedback(activity).text.toString().contains(fixture.block.getString("explanation")))
                        } else {
                            assertEquals("A legacy incorrect draft is not evidence that this revision was submitted", "",
                                feedback(activity).text.toString())
                            check(activity).performClick()
                            assertReview(activity, fixture, answer, true)
                            assertDraft(activity, fixture, answer, false)
                        }
                    }
                    rebuild(scenario, fixture)
                    scenario.onActivity { activity ->
                        assertDraft(activity, fixture, answer, wasCorrect)
                        if (!wasCorrect) assertReview(activity, fixture, answer, true)
                    }
                }
            }
        }
    }

    @Test fun malformedOptionalReviewIsATechnicalFailureWithoutScoringOrShowingContent() {
        for (review in listOf(JSONArray().put("Texto ajeno a una lista incompleta"),
            JSONArray().put("Orientación uno").put(42).put("Orientación tres"),
            JSONArray().put("Orientación uno").put(" ").put("Orientación tres"))) {
            withBlock(Location(1, 9), transform = { it.put("reviewFeedback", review) }) { scenario, fixture ->
                scenario.onActivity { activity ->
                    val answer = wrongAnswers(fixture.block).first()
                    val ledger = field(activity, "exerciseAttempts") as ExerciseAttemptLedger
                    val before = ledger.save().toString()
                    submit(activity, fixture, answer)
                    assertDraft(activity, fixture, answer, false)
                    assertEquals("A malformed optional content contract is technical, not a scored wrong answer", "",
                        feedback(activity).text.toString())
                    assertFalse("Do not record an evaluation that could not run",
                        saved(fixture).has(CppExerciseFeedback.REVIEWED_ANSWER_IDENTITY))
                    assertEquals("A technical content failure cannot add a penalized answer identity", before, ledger.save().toString())
                }
            }
        }
    }

    private fun assertReview(activity: LessonActivity, fixture: Fixture, answer: Any, authored: Boolean) {
        val text = feedback(activity).text.toString()
        val expected = if (authored) {
            if (fixture.block.getString("type") == "multiple_choice")
                fixture.block.getJSONArray("reviewFeedback").getString(answer as Int)
            else fixture.block.getString("reviewFeedback")
        } else activity.getString(when (fixture.block.getString("type")) {
            "multiple_choice" -> R.string.cpp_review_choice
            "fill_code" -> R.string.cpp_review_fill
            "output_prediction" -> R.string.cpp_review_output
            "order_code" -> R.string.cpp_review_order
            else -> error("Unexpected block")
        })
        assertTrue("Feedback must contain the relevant review rather than only a verdict: $text", text.contains(expected))
        assertTrue(text.contains(activity.getString(if (fixture.block.getString("type") == "order_code")
            R.string.exercise_wrong_order else R.string.exercise_incorrect)))
        assertFalse("The complete explanation can reveal the solution", text.contains(fixture.block.getString("explanation")))
        assertFalse("Internal curricular IDs are not human labels", text.contains(fixture.block.getString("id")))
        when (fixture.block.getString("type")) {
            "multiple_choice" -> if (authored) strings(fixture.block.getJSONArray("reviewFeedback")).forEachIndexed { index, hint ->
                if (index != answer) assertFalse("Never classify or reveal an unselected option", text.contains(hint))
            }
            "fill_code", "output_prediction" -> strings(fixture.block.getJSONArray("answers")).forEach {
                val literal = Regex("(?<![\\p{L}\\p{N}_])" + Regex.escape(it) + "(?![\\p{L}\\p{N}_])")
                assertFalse("Do not give the expected literal before success", literal.containsMatchIn(text))
            }
            "order_code" -> strings(fixture.block.getJSONArray("fragments")).forEach {
                assertFalse("Review the process without handing over the ordered fragments", text.contains(it))
            }
        }
    }

    private fun correctAnswer(block: JSONObject): Any = when (block.getString("type")) {
        "multiple_choice" -> block.getInt("correctIndex")
        "fill_code", "output_prediction" -> block.getJSONArray("answers").getString(0)
        "order_code" -> integers(block.getJSONArray("correctOrder"))
        else -> error("Unexpected puzzle type")
    }

    private fun wrongAnswers(block: JSONObject): List<Any> = when (block.getString("type")) {
        "multiple_choice" -> (0 until block.getJSONArray("options").length()).filter { it != block.getInt("correctIndex") }
        "fill_code" -> listOf("criterio", "amplia.cpp")
        "output_prediction" -> listOf("1:1:1", "0:0:0")
        "order_code" -> listOf(listOf(3, 1, 0, 2), listOf(1, 0, 3, 2))
        else -> error("Unexpected puzzle type")
    }

    /** These reviewed examples live exclusively in the instrumentation APK. Work can
     * author different guidance through the optional contract without changing answers. */
    private fun addReviewedGuidance(block: JSONObject) {
        val guides = when (block.getString("id")) {
            "opc_politica_estricta_no_es_fallo_universal" -> listOf(
                "Escribe qué relación pide el contrato cuando los puntos igualan el mínimo y contrástala con la implementación que elegiste.",
                "Comprueba qué política se acordó y qué caso de frontera estás contrastando.",
                "Separa lo que declara la firma de la condición que evalúa el cuerpo de la función.")
            "opc_leer_los_argumentos_de_option" -> listOf(
                "Ubica cada argumento de la declaración y distingue una descripción de una instrucción ejecutable.",
                "Revisa qué lenguaje interpreta esa cadena y en qué archivo se define el cuerpo de admitir.",
                "Identifica qué mecanismo entrega los datos al programa durante su ejecución.")
            "opc_valor_inicial_no_imposicion" -> listOf(
                "Distingue el valor inicial escrito en la descripción de la elección explícita proporcionada al configurar.",
                "Examina si la cadena de ayuda es una expresión ejecutable o una descripción de la opción.",
                "Contrasta las condiciones bajo las que se utiliza un valor inicial con la elección actual.")
            "opc_momento_de_la_eleccion" -> listOf(
                "Localiza en qué fase se interpreta la descripción y en cuál se entregan los argumentos de la llamada.",
                "Sigue el paso desde la descripción hasta la fuente seleccionada y después hasta la llamada ejecutada.",
                "Distingue el lenguaje de construcción del flujo de instrucciones del programa.")
            "opc_una_impl_por_configuracion" -> listOf(
                "Sigue las ramas del condicional y distingue estar en el directorio de formar parte de un objetivo.",
                "Comprueba qué aporta una declaración de interfaz y dónde se encuentra una definición de la función.",
                "Traza qué objetivo recibe cada fuente en la rama que se configura.")
            "opc_casos_que_distinguen_las_politicas" -> listOf(
                "Calcula qué condiciones satisface el par observado bajo cada comparación antes de identificar la elección.",
                "Pregunta si la observación sería también posible con la otra política y busca un caso que las separe.",
                "Distingue una observación compatible de una prueba que discrimina entre implementaciones.")
            "opc_cache_persistente_con_otro_inicial" -> listOf(
                "Relaciona el valor conservado con las condiciones que permiten usar un valor inicial nuevo.",
                "Revisa si ya existe una elección almacenada y qué acción se realizó realmente sobre la caché.",
                "Distingue no proporcionar un argumento nuevo de borrar una decisión previamente conservada.")
            "opc_nuevo_inicial_con_eleccion_explicita" -> listOf(
                "Revisa qué se renueva en una configuración fresca y qué argumentos se proporcionan de nuevo.",
                "Contrasta el valor inicial de la descripción con el campo explícito de esta configuración.",
                "Separa los datos de una ejecución anterior de las decisiones utilizadas para configurar ahora.")
            "opc_mismos_archivos_datos_nuevos" -> listOf(
                "Identifica el canal por el que el cliente recibe cada par sin cambiar el programa construido.",
                "Distingue una revisión de construcción de un cambio en los argumentos que el cliente lee.",
                "Busca la instrucción que lee datos y comprueba de qué canal los obtiene realmente.")
            "opc_fase_del_rechazo_de_datos" -> listOf(
                "Ubica primero qué fases ya concluyeron y qué proceso produjo los diagnósticos observados.",
                "Comprueba si la terminación observada requiere que el cliente ya estuviera ejecutándose.",
                "Sigue la rama que valida la preparación de entrada antes de llegar a la función.")
            "opc_cero_no_identifica_tarifa" -> listOf(
                "Sustituye la cantidad observada en cada expresión y revisa si ambas podrían producir esa observación.",
                "Pregunta qué caso permitiría distinguir las expresiones cuando coinciden para la observación actual.",
                "Distingue un producto válido con cantidad nula de la ausencia de una implementación enlazada.")
            "opc_registro_que_separa_dos_ejecuciones" -> listOf(
                "Comprueba que cada resultado pueda relacionarse con su construcción y con sus propios datos de entrada.",
                "Examina si el valor inicial basta para reconstruir la elección que se utilizó realmente.",
                "Revisa si una entrada conservada describe cada corrida cuando los datos cambian entre ejecuciones.")
            else -> null
        }
        if (guides != null) {
            assertEquals(block.getJSONArray("options").length(), guides.size)
            block.put("reviewFeedback", JSONArray(guides))
        } else block.put("reviewFeedback", when (block.getString("id")) {
            "opc_oraculo_con_override_off", "opc_oraculo_con_eleccion_on", "opc_recuperar_extremos_en_otra_politica" ->
                "Evalúa cada relación con los argumentos de su llamada, conserva su orden y revisa los separadores que envía la salida."
            "opc_tarifa_normal_tras_quitar_eleccion" ->
                "Relaciona la revisión elegida con la expresión de precio, sustituye cada cantidad y sigue el orden de las impresiones."
            "opc_nombre_consultado_por_if" ->
                "Distingue el nombre de la decisión declarada, el objetivo y la fuente; revisa qué clase de nombre consulta el condicional."
            "opc_orden_tras_editar_eleccion" ->
                "Identifica qué producto necesita cada acción y comprueba que sus requisitos ya existan cuando intentas realizarla."
            else -> error("Missing reviewed QA guidance for ${block.optString("id")}")
        })
    }

    private fun withBlock(location: Location, transform: (JSONObject) -> Unit = {},
                          savedState: JSONObject? = null,
                          test: (ActivityScenario<LessonActivity>, Fixture) -> Unit) {
        val source = LessonRepository.loadLessonJson(context, ROUTE, SOURCE_LESSON)
        val original = source.getJSONArray("pages").getJSONObject(location.page - 1)
            .getJSONArray("blocks").getJSONObject(location.block - 1)
        val block = JSONObject(original.toString()).also(transform)
        val id = "programming_practice_tools__construccion_reproducible__incorrect_feedback_${UUID.randomUUID().toString().replace("-", "") }"
        val pages = JSONArray().apply {
            repeat(source.getJSONArray("pages").length()) { p ->
                put(JSONObject().put("blocks", JSONArray().apply {
                    if (p == location.page - 1) {
                        repeat(location.block - 1) { put(placeholder()) }
                        put(block)
                    } else put(placeholder())
                }))
            }
        }
        val fixture = Fixture(id, location, block,
            JSONObject().put("id", id).put("title", source.getString("title")).put("pages", pages))
        val progress = context.getSharedPreferences("learning_progress", Context.MODE_PRIVATE)
        val originalExercises = publishedExerciseKeys(progress.all)
        if (savedState != null) ProgressRepository.saveExerciseState(context, ROUTE, id,
            location.page - 1, location.block - 1, JSONObject(savedState.toString()))
        ProgressRepository.saveCurrentPage(context, ROUTE, SOURCE_LESSON, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", "programming_practice_tools").putExtra("unitId", "construccion_reproducible")
            .putExtra("lessonId", SOURCE_LESSON).putExtra("lessonNumber", 3).putExtra("lessonTotal", 3)
        try {
            ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
                awaitStableStartupAndInject(scenario, fixture)
                test(scenario, fixture)
            }
        } finally {
            instrumentation.waitForIdleSync()
            assertEquals("Native copied-block tests cannot overwrite published exercise answers",
                originalExercises, publishedExerciseKeys(progress.all))
        }
    }

    private fun awaitStableStartupAndInject(scenario: ActivityScenario<LessonActivity>, fixture: Fixture) {
        val deadline = SystemClock.uptimeMillis() + 12_000L
        var keyBefore = ""
        var stableSince = 0L
        var samples = 0
        var diagnostic = ""
        while (SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            var injected = false
            scenario.onActivity { activity ->
                val root = content(activity)
                val loaded = field(activity, "lessonJson") as? JSONObject
                val ready = !activity.isDestroyed && !activity.isFinishing && activity in resumed &&
                    field(activity, "currentLessonId") == SOURCE_LESSON && loaded?.optString("id") == SOURCE_LESSON &&
                    field(activity, "currentPage") == 0 && mapField(activity, "pageViewCache").containsKey(0) &&
                    root.childCount > 0 && root.isAttachedToWindow && root.width > 0 && root.height > 0 &&
                    !root.isLayoutRequested && activity.window.decorView.hasWindowFocus()
                val key = "${System.identityHashCode(activity)}/${System.identityHashCode(loaded)}/" +
                    "${System.identityHashCode(root.getChildAt(0))}/${root.width}x${root.height}/" +
                    "${activity.resources.configuration.orientation}/${activity.resources.configuration.densityDpi}"
                val now = SystemClock.uptimeMillis()
                if (!ready || key != keyBefore) {
                    stableSince = if (ready) now else 0L
                    samples = if (ready) 1 else 0
                } else samples++
                keyBefore = key
                diagnostic = "$key ready=$ready page=${field(activity, "currentPage")} loaded=${loaded?.optString("id")}"
                if (ready && samples >= 4 && now - stableSince >= 500L) {
                    setField(activity, "currentLessonId", fixture.id)
                    setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
                    setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
                    setField(activity, "currentPage", fixture.location.page - 1)
                    listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach { mapField(activity, it).clear() }
                    invoke(activity, "renderCurrentPage")
                    assertEquals(fixture.id, field(activity, "currentLessonId"))
                    assertEquals(1, field(activity, "requiredExercises"))
                    injected = true
                }
            }
            if (injected) {
                instrumentation.waitForIdleSync()
                return
            }
            SystemClock.sleep(100L)
        }
        fail("The original Activity never reached a stable focused startup: $diagnostic")
    }

    private fun rebuild(scenario: ActivityScenario<LessonActivity>, fixture: Fixture) {
        scenario.onActivity { activity ->
            assertEquals("A lost QA lesson must fail rather than be silently reinjected", fixture.id, field(activity, "currentLessonId"))
            listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                mapField(activity, it).remove(fixture.location.page - 1)
            }
            invoke(activity, "renderCurrentPage")
        }
        instrumentation.waitForIdleSync()
    }

    private fun setDraft(activity: LessonActivity, fixture: Fixture, answer: Any) {
        when (fixture.block.getString("type")) {
            "multiple_choice" -> radios(activity)[answer as Int].performClick()
            "fill_code", "output_prediction" -> input(activity).setText(answer as String)
            "order_code" -> {
                @Suppress("UNCHECKED_CAST") val order = answer as List<Int>
                order.forEach { index -> orderRows(activity).single { it.tag == index }.performClick() }
            }
            else -> error("Unsupported test block ${fixture.block.optString("type")}")
        }
    }
    private fun submit(activity: LessonActivity, fixture: Fixture, answer: Any) {
        setDraft(activity, fixture, answer)
        assertTrue("Native Check remains enabled for an independent attempt", check(activity).isEnabled)
        check(activity).performClick()
    }

    private fun saved(fixture: Fixture): JSONObject = requireNotNull(ProgressRepository.getExerciseState(context,
        fixture.id, fixture.location.page - 1, fixture.location.block - 1))
    private fun assertDraft(activity: LessonActivity, fixture: Fixture, answer: Any, correct: Boolean) {
        val state = saved(fixture)
        assertEquals(correct, state.getBoolean("correct"))
        assertEquals(if (correct) 1 else 0, field(activity, "completedExercises"))
        assertEquals(!correct, check(activity).isEnabled)
        when (fixture.block.getString("type")) {
            "multiple_choice" -> {
                assertEquals(answer, state.getInt("selectedIndex"))
                assertEquals(answer, radios(activity).indexOfFirst { it.isChecked })
                radios(activity).forEach { assertEquals(!correct, it.isEnabled) }
            }
            "fill_code", "output_prediction" -> {
                assertEquals(answer, state.getString("answer"))
                assertEquals(answer, input(activity).text.toString())
                assertEquals(!correct, input(activity).isEnabled)
            }
            "order_code" -> {
                assertEquals(answer, integers(state.getJSONArray("order")))
                assertEquals(answer, orderRows(activity).map { it.tag as Int })
            }
        }
    }

    private fun capture(scenario: ActivityScenario<LessonActivity>, fixture: Fixture, name: String) {
        val deadline = SystemClock.uptimeMillis() + 5_000L
        var laidOut = false
        while (!laidOut && SystemClock.uptimeMillis() < deadline) {
            instrumentation.waitForIdleSync()
            scenario.onActivity { activity ->
                val result = feedback(activity)
                laidOut = result.isAttachedToWindow && result.width > 0 && result.height > 0 && !result.isLayoutRequested
            }
            if (!laidOut) SystemClock.sleep(20L)
        }
        assertTrue("The native result must complete its real layout before capture", laidOut)
        scenario.onActivity { activity ->
            val result = feedback(activity)
            assertTrue("Capture the laid-out native result at its existing dimensions", result.width > 0 && result.height > 0)
            val bitmap = Bitmap.createBitmap(result.width, result.height, Bitmap.Config.ARGB_8888)
            val file = File(evidenceDirectory(), "$name.png")
            try {
                val canvas = Canvas(bitmap)
                canvas.drawColor(Color.rgb(28, 31, 36))
                result.draw(canvas)
                file.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            } finally { bitmap.recycle() }
            val metadata = JSONObject().put("sourceLesson", SOURCE_LESSON).put("sourcePage", fixture.location.page)
                .put("sourceBlock", fixture.location.block).put("sourcePuzzleId", fixture.block.optString("id"))
                .put("type", fixture.block.getString("type")).put("qaLessonId", fixture.id)
                .put("package", context.packageName).put("state", saved(fixture))
                .put("feedback", result.text.toString()).put("width", result.width).put("height", result.height)
                .put("lineCount", result.layout.lineCount).put("png", file.name).put("pngSha256", sha(file.readBytes()))
                .put("captureKind", "actual Android result TextView drawn at its existing lesson bounds; not a full-window screenshot")
            File(evidenceDirectory(), "$name.json").writeText(metadata.toString(2))
        }
    }

    private fun feedback(activity: LessonActivity): TextView = content(activity).getChildAt(content(activity).childCount - 1) as TextView
    private fun check(activity: LessonActivity): Button = descendants(content(activity)).filterIsInstance<Button>().single {
        it.text.toString() in listOf(activity.getString(R.string.exercise_check), activity.getString(R.string.exercise_correct))
    }
    private fun input(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<EditText>().single()
    private fun radios(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<RadioButton>()
    private fun orderRows(activity: LessonActivity) = descendants(content(activity)).filterIsInstance<TextView>().filter { it.tag is Int }
    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun descendants(root: View): List<View> = buildList {
        add(root)
        if (root is ViewGroup) for (i in 0 until root.childCount) addAll(descendants(root.getChildAt(i)))
    }
    private fun placeholder() = JSONObject().put("type", "paragraph").put("text", "Prueba curricular aislada")
    private fun publishedExerciseKeys(values: Map<String, *>) = values.filterKeys {
        it.startsWith("exercise_") && !it.contains("__incorrect_feedback_")
    }
    private fun preferenceFiles(): Set<String> = File(context.applicationInfo.dataDir, "shared_prefs").listFiles().orEmpty()
        .filter { it.name.endsWith(".xml") }.map { it.name.removeSuffix(".xml") }.toSet()
    private fun integers(array: JSONArray) = (0 until array.length()).map { array.getInt(it) }
    private fun strings(array: JSONArray) = (0 until array.length()).map { array.getString(it) }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    private fun setField(activity: LessonActivity, name: String, value: Any) = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.set(activity, value)
    @Suppress("UNCHECKED_CAST") private fun mapField(activity: LessonActivity, name: String) = field(activity, name) as MutableMap<Int, Any>
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)
    private fun evidenceDirectory() = File(requireNotNull(context.getExternalFilesDir(null)), "cpp-incorrect-feedback-qa/$session").apply { mkdirs() }
    private fun sha(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    private data class Location(val page: Int, val block: Int)
    private data class Fixture(val id: String, val location: Location, val block: JSONObject, val lesson: JSONObject)

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ROUTE = "programming"
        private const val SOURCE_LESSON = "programming_practice_tools__construccion_reproducible__opciones_y_revision_de_la_configuracion"
        private val PREFS = listOf("learning_progress", "navigation_state", "lesson_narration_settings",
            "attempt_reserve_seconds_test", "cpp_lesson_drafts", "app_settings", "appearance_settings", "ui_language_settings", "premium_state")
        private val LOCATIONS = listOf(Location(1, 9), Location(2, 6), Location(2, 7), Location(3, 6), Location(3, 7),
            Location(4, 3), Location(6, 3), Location(6, 4), Location(7, 6), Location(8, 5), Location(9, 6), Location(10, 3),
            Location(11, 3), Location(13, 4), Location(13, 5), Location(14, 3), Location(14, 4), Location(14, 5))
    }
}

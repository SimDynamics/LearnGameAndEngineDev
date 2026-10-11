package com.learngraphicsprogramming.app

import android.app.Activity
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.os.PowerManager
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.CheckBox
import android.widget.EditText
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.lifecycle.Lifecycle
import com.learngraphicsprogramming.app.narration.NarrationSettingsRepository
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Sixteen authored lab locations, through their real renderer and Run listener.
 * Requires the opt-in curricularqa application. Never run against the user's
 * normal package: the first setup assertions precede every preference access.
 * Source solutions come from later pages of the same published JSON assets.
 */
@RunWith(AndroidJUnit4::class)
class CppNewCurricularLabsDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
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

    @Before fun requireIsolatedApplicationBeforeWritingAnything() {
        assertEquals("Build and install with -Plgp.curricularQa=true; do not target the normal app",
            QA_PACKAGE, context.packageName)
        assertEquals("The compiled application must also be the isolated copy",
            QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertEquals(QA_PACKAGE, context.applicationContext.packageName)
        (context.applicationContext as Application).registerActivityLifecycleCallbacks(screenGuard)
        screenGuardRegistered = true
        ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("input keyevent KEYCODE_WAKEUP")
        ).use { it.readBytes() }
        val power = context.getSystemService(Context.POWER_SERVICE) as PowerManager
        val wakeDeadline = SystemClock.uptimeMillis() + 5_000L
        while (!power.isInteractive && SystemClock.uptimeMillis() < wakeDeadline) SystemClock.sleep(50L)
        assertTrue("Wake the display before launching or recreating QA activities", power.isInteractive)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Dismiss the phone's visual lock screen before renderer acceptance checks", keyguard.isKeyguardLocked)
        assertFalse("Unlock the phone before renderer acceptance checks", keyguard.isDeviceLocked)

        // Every preference below belongs to curricularqa, not the user's app.
        NarrationSettingsRepository.saveEnabled(context, false)
        assertTrue(context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).edit()
            .putBoolean("unlimited", false).putFloat("reserve_$ROUTE", 4f)
            .putLong("updated_at_$ROUTE", System.currentTimeMillis()).commit())
        val drafts = context.getSharedPreferences(DRAFTS, Context.MODE_PRIVATE)
        val editor = drafts.edit()
        val prefixes = LABS.map { it.draftPrefix() }
        drafts.all.keys.filter { key -> prefixes.any { prefix -> key.startsWith(prefix) } }
            .forEach { key -> editor.remove(key) }
        assertTrue("Reset only these sixteen isolated lab drafts", editor.commit())
    }

    @After fun releaseOnlyTheQaScreenGuard() {
        // After also runs when Before rejects an accidental normal-package run.
        if (context.packageName != QA_PACKAGE || BuildConfig.APPLICATION_ID != QA_PACKAGE) return
        if (screenGuardRegistered) {
            (context.applicationContext as Application).unregisterActivityLifecycleCallbacks(screenGuard)
            screenGuardRegistered = false
        }
    }

    @Test fun threeInputsAreReadRatherThanFixedAndTheirDraftSurvivesRecreation() {
        withLab(THREE_INPUTS) { scenario, lesson, block ->
            assertSingleSourceSkeleton(scenario, block)
            assertEquals("Iris 4 2", block.getString("initialStandardInput"))
            val reference = referenceCode(lesson, 11, 2)
            scenario.onActivity { editor(it).setText(reference) }
            assertSuccessful(run(scenario), "Iris: 22")
            runWithInput(scenario, "Leo\n0\n4\n", "Leo: 4")
            runWithInput(scenario, "Vera 3 1", "Vera: 16")
            runWithInput(scenario, "Sol 20 4", "Sol: 104")
            runWithInput(scenario, "Sol 20 4", "Sol: 104")

            val draft = reference + "\n// New three-input lab draft\n"
            scenario.onActivity {
                editor(it).setText(draft)
                input(it).setText("Iris\t4\n2")
            }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(THREE_INPUTS.page - 1, field(it, "currentPage"))
                assertEquals(draft, editor(it).text.toString())
                assertEquals("Iris\t4\n2", input(it).text.toString())
            }
            assertSuccessful(run(scenario), "Iris: 22")
        }
    }

    @Test fun completeLinesRecoverAndPublishOnlyAcceptedOccupancy() {
        withLab(LINES) { scenario, lesson, block ->
            assertSingleSourceSkeleton(scenario, block)
            val initialInput = "2:1\n \t\nx:2\n1:2 sobra\n4:0\n3:0\n0:3"
            assertEquals(initialInput, block.getString("initialStandardInput"))
            val reference = referenceCode(lesson, 6, 3)
            scenario.onActivity { editor(it).setText(reference) }
            assertSuccessful(run(scenario), linesOutput(listOf(
                "Aceptada" to 4, "Sin datos" to 4, "Formato invalido" to 4,
                "Datos adicionales" to 4, "Fuera de dominio" to 4,
                "Capacidad invalida" to 4, "Aceptada" to 1), 2, 1))
            runWithInput(scenario, "", linesOutput(emptyList(), 0, 3))
            runWithInput(scenario, "\n", linesOutput(listOf("Sin datos" to 3), 0, 3))
            runWithInput(scenario, "0:0", linesOutput(listOf("Aceptada" to 3), 1, 3))
            runWithInput(scenario, "0:0\n", linesOutput(listOf("Aceptada" to 3), 1, 3))
            runWithInput(scenario, "0:0\n\n", linesOutput(listOf("Aceptada" to 3, "Sin datos" to 3), 1, 3))
            runWithInput(scenario, "0:3\n0:1\n3:0\n3:0\n1:0\nq:2\n0:0\n", linesOutput(listOf(
                "Aceptada" to 0, "Capacidad invalida" to 0,
                "Aceptada" to 3, "Aceptada" to 6, "Capacidad invalida" to 6,
                "Formato invalido" to 6, "Aceptada" to 6), 4, 6))
            runWithInput(scenario, "2:\n1:0\n2:1x\n0:4\n4:0 aviso\n+2 : 01\n", linesOutput(listOf(
                "Formato invalido" to 3, "Aceptada" to 4, "Datos adicionales" to 4,
                "Fuera de dominio" to 4, "Datos adicionales" to 4, "Aceptada" to 5), 2, 5))
            val truncated = "1:0\n0:"
            val truncatedOutput = linesOutput(listOf("Aceptada" to 4, "Formato invalido" to 4), 1, 4)
            runWithInput(scenario, truncated, truncatedOutput)
            scenario.recreate()
            scenario.onActivity {
                assertEquals(LINES.page - 1, field(it, "currentPage"))
                assertEquals(reference, editor(it).text.toString())
                assertEquals(truncated, input(it).text.toString())
            }
            // A fresh execution starts at 3; it must not retain occupancy 4.
            assertSuccessful(run(scenario), truncatedOutput)
        }
    }

    @Test fun controlledOriginFailuresKeepAcceptedStateAndExitWithoutTheNormalSummary() {
        withLab(LINES) { scenario, lesson, _ ->
            val reference = referenceCode(lesson, 6, 3)
            val main = "int main() {"
            val publication = "        std::cout << mensaje(resultado) << \" | Ocupadas: \" << ocupadas << \"\\n\";"
            val failure = "    if (std::cin.bad() || !std::cin.eof()) {"
            listOf(main, publication, failure).forEach { marker ->
                assertTrue("The published reference still contains $marker", reference.contains(marker))
                assertEquals("Technical injection must have one exact location",
                    reference.indexOf(marker), reference.lastIndexOf(marker))
            }

            // These are technical flags, never malformed prepared input.
            // Both lines are valid; the second must remain unread after failure.
            val validInput = "2:1\n0:3\n"
            for (flag in listOf("badbit", "failbit")) {
                for (afterAcceptance in listOf(false, true)) {
                    val occupied = if (afterAcceptance) 4 else 3
                    val accepted = if (afterAcceptance) 1 else 0
                    val expectedBad = flag == "badbit"
                    val setFlag = "    std::cin.setstate(std::ios::$flag);"
                    val injected = if (afterAcceptance) {
                        reference.replaceFirst(publication, publication + "\n    " + setFlag)
                    } else {
                        reference.replaceFirst(main, main + "\n" + setFlag)
                    }
                    // Exit 91/92 would expose changed state or an incorrect flag.
                    // The published failure branch must still reach return 1.
                    val source = injected.replaceFirst(failure, failure + "\n" +
                        "        if (ocupadas != $occupied || aceptadas != $accepted) return 91;\n" +
                        "        if (std::cin.eof() || !std::cin.fail() || std::cin.bad() != $expectedBad) return 92;")
                    var exitOneStatus = ""
                    scenario.onActivity {
                        editor(it).setText(source)
                        input(it).setText(validInput)
                        exitOneStatus = it.getString(R.string.cpp_execution_failed, 1)
                    }
                    val result = run(scenario)
                    val label = "$flag; after acceptance=$afterAcceptance"
                    assertEquals("Controlled origin failure exits with 1 ($label): $result",
                        exitOneStatus, result.status)
                    assertEquals("No build error ($label)", "", result.compilation)
                    val expected = (if (afterAcceptance) "Aceptada | Ocupadas: 4\n" else "") + "Fallo del origen\n"
                    assertEquals("Accepted state is retained and no normal summary is emitted ($label)",
                        expected, result.stdout)
                    assertEquals("No diagnostic is forged into stderr ($label)", "", result.stderr)
                    assertFalse(result.stdout.contains("Aceptadas:"))
                }
            }
        }
    }

    @Test fun independentProjectUsesTheLaterReferenceAndDistinguishesBuildFailures() {
        withLab(PROJECT) { scenario, lesson, block ->
            assertProjectSkeleton(scenario, block)
            val reference = originalProject(lesson)
            fillProject(scenario, reference)
            assertSuccessful(run(scenario), "Espacios: 23\nCarga: Pesado\n")

            // Test the header twice and all authored boundaries in one build.
            val boundaries = "#include \"estante.h\"\n" + boundaryMain(listOf(
                0 to 0, 0 to 4, 3 to 3, 10 to 10,
                -1 to 2, 2 to -1, 11 to 0, 0 to 11))
            editFile(scenario, "main.cpp") { boundaries }
            assertSuccessful(run(scenario),
                "Espacios: 0\nCarga: Vacio\nEspacios: 20\nCarga: Ligero\n" +
                    "Espacios: 21\nCarga: Pesado\nEspacios: 70\nCarga: Pesado\n" +
                    "Cantidades invalidas\n".repeat(4))

            val headerDraft = reference.getValue("estante.h") + "\n// Isolated project draft\n"
            editFile(scenario, "estante.h") { headerDraft }
            enableSource(scenario, "estado.cpp", false)
            scenario.onActivity { input(it).setText("project draft input") }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(PROJECT.page - 1, field(it, "currentPage"))
                assertEquals("estado.cpp", editor(it).contentDescription.toString())
                assertEquals("project draft input", input(it).text.toString())
            }
            assertEquals(headerDraft, readFile(scenario, "estante.h"))
            assertEquals(boundaries, readFile(scenario, "main.cpp"))
            assertEquals(reference.getValue("conteo.cpp"), readFile(scenario, "conteo.cpp"))
            assertEquals(reference.getValue("estado.cpp"), readFile(scenario, "estado.cpp"))
            assertSources(scenario, setOf("conteo.cpp", "main.cpp"))
            assertLinkFailure(run(scenario), "descripcion_carga")

            enableSource(scenario, "estado.cpp", true)
            val calculation = reference.getValue("conteo.cpp")
            assertTrue(calculation.contains("int espacios_ocupados("))
            editFile(scenario, "conteo.cpp") { calculation.replace("int espacios_ocupados(", "double espacios_ocupados(") }
            assertCompilationFailure(run(scenario), "conteo.cpp", "espacios_ocupados")
            editFile(scenario, "conteo.cpp") { calculation }
            editFile(scenario, "main.cpp") { reference.getValue("main.cpp") }
            assertSuccessful(run(scenario), "Espacios: 23\nCarga: Pesado\n")
        }
    }

    @Test fun transferProjectHasItsOwnDraftAndChecksTheNewWeightsAndThreshold() {
        withLab(TRANSFER) { scenario, lesson, block ->
            assertProjectSkeleton(scenario, block)
            val reference = linkedMapOf(
                "estante.h" to referenceCode(lesson, 6, 3),
                "conteo.cpp" to referenceCode(lesson, 11, 3),
                "estado.cpp" to referenceCode(lesson, 11, 5),
                "main.cpp" to referenceCode(lesson, 11, 7))
            fillProject(scenario, reference)
            assertSuccessful(run(scenario), "Espacios: 24\nCarga: Ligero\n")
            val boundaries = boundaryMain(listOf(
                7 to 1, 0 to 0, 10 to 10, 2 to 3,
                -1 to 2, 2 to -1, 11 to 0, 0 to 11))
            editFile(scenario, "main.cpp") { boundaries }
            assertSuccessful(run(scenario),
                "Espacios: 25\nCarga: Pesado\nEspacios: 0\nCarga: Vacio\n" +
                    "Espacios: 70\nCarga: Pesado\nEspacios: 18\nCarga: Ligero\n" +
                    "Cantidades invalidas\n".repeat(4))
            val headerDraft = reference.getValue("estante.h") + "\n// Transfer draft, not the earlier attempt\n"
            editFile(scenario, "estante.h") { headerDraft }
            scenario.onActivity { input(it).setText("transfer draft input") }

            // Both labs are in one lesson, but their page/block keys must differ.
            scenario.onActivity { showPage(it, PROJECT.page - 1) }
            assertProjectSkeleton(scenario, blockAt(lesson, PROJECT.page, PROJECT.block))
            val earlierHeader = blockAt(lesson, PROJECT.page, PROJECT.block)
                .getJSONArray("projectFiles").getJSONObject(0).getString("initialCode") + "\n// Earlier attempt draft\n"
            editFile(scenario, "estante.h") { earlierHeader }
            enableSource(scenario, "conteo.cpp", false)
            scenario.onActivity {
                input(it).setText("earlier attempt input")
                showPage(it, TRANSFER.page - 1)
            }
            assertEquals(headerDraft, readFile(scenario, "estante.h"))
            assertEquals(boundaries, readFile(scenario, "main.cpp"))
            assertSources(scenario, setOf("conteo.cpp", "estado.cpp", "main.cpp"))
            scenario.recreate()
            scenario.onActivity {
                assertEquals(TRANSFER.page - 1, field(it, "currentPage"))
                assertEquals("transfer draft input", input(it).text.toString())
            }
            assertEquals(headerDraft, readFile(scenario, "estante.h"))
            assertEquals(reference.getValue("conteo.cpp"), readFile(scenario, "conteo.cpp"))
            assertEquals(reference.getValue("estado.cpp"), readFile(scenario, "estado.cpp"))
            assertEquals(boundaries, readFile(scenario, "main.cpp"))
            assertSources(scenario, setOf("conteo.cpp", "estado.cpp", "main.cpp"))
            editFile(scenario, "main.cpp") { reference.getValue("main.cpp") }
            assertSuccessful(run(scenario), "Espacios: 24\nCarga: Ligero\n")
            scenario.onActivity { showPage(it, PROJECT.page - 1) }
            assertEquals(earlierHeader, readFile(scenario, "estante.h"))
            assertSources(scenario, setOf("estado.cpp", "main.cpp"))
            scenario.onActivity { assertEquals("earlier attempt input", input(it).text.toString()) }
        }
    }

    @Test fun overloadLabsSelectByArgumentAndKeepIndependentDrafts() {
        val four = OVERLOAD_FOUR
        val eight = OVERLOAD_EIGHT

        fun boundarySource(reference: String, cases: List<Pair<Int, String>>): String {
            val main = reference.indexOf("int main() {")
            val definitions = reference.indexOf("\nint por_persona(int piezas) {", main)
            assertTrue("Retain the actual declarations and both later function bodies", main >= 0 && definitions > main)
            val driver = buildString {
                append("int main() {\n")
                cases.forEach { (pieces, meters) ->
                    append("""
                        {
                            int piezas = $pieces;
                            double metros = $meters;
                            double desde_piezas = por_persona(piezas);
                            double desde_metros = por_persona(metros);
                            if (piezas != $pieces || metros != $meters) return 91;
                            std::cout << "Piezas: " << desde_piezas << '\n';
                            std::cout << "Metros: " << desde_metros << '\n';
                        }
                    """.trimIndent())
                    append('\n')
                }
                append("    return 0;\n}\n")
            }
            return reference.replaceRange(main, definitions, driver)
        }

        withLab(four) { scenario, lesson, fourBlock ->
            assertSingleSourceSkeleton(scenario, fourBlock)
            val eightBlock = blockAt(lesson, eight.page, eight.block)
            assertEquals("The second coordinate is also an authored lab", "cpp_compiler", eightBlock.getString("type"))
            assertFalse(eightBlock.has("projectFiles"))
            val fourReference = referenceCode(lesson, 9, 3)
            val eightReference = referenceCode(lesson, 12, 3)
            scenario.onActivity { editor(it).setText(fourReference) }
            assertSuccessful(run(scenario), "Piezas: 2\nMetros: 2.375\n")

            // Both receivers are double. The int argument must still perform
            // indivisible division; a double receiver cannot restore fractions.
            val fourBoundaries = boundarySource(fourReference, listOf(
                7 to "7.0", 0 to "0.0", 3 to "3.0", 4 to "4.0",
                20 to "20.0", 1 to "0.5", 11 to "11.0"))
            val fourOutput = "Piezas: 1\nMetros: 1.75\nPiezas: 0\nMetros: 0\n" +
                "Piezas: 0\nMetros: 0.75\nPiezas: 1\nMetros: 1\n" +
                "Piezas: 5\nMetros: 5\nPiezas: 0\nMetros: 0.125\n" +
                "Piezas: 2\nMetros: 2.75\n"
            scenario.onActivity { editor(it).setText(fourBoundaries) }
            assertSuccessful(run(scenario), fourOutput)
            val fourDraft = fourBoundaries + "\n// Authored four-person draft\n"
            scenario.onActivity {
                editor(it).setText(fourDraft)
                showPage(it, eight.page - 1)
            }

            // A different page/block must begin from its own published skeleton.
            assertSingleSourceSkeleton(scenario, eightBlock)
            scenario.onActivity { editor(it).setText(eightReference) }
            assertSuccessful(run(scenario), "Piezas: 1\nMetros: 1.6875\n")
            val eightBoundaries = boundarySource(eightReference, listOf(
                11 to "11.0", 0 to "0.0", 7 to "7.0", 8 to "8.0",
                20 to "20.0", 1 to "0.5", 10 to "10.0"))
            val eightOutput = "Piezas: 1\nMetros: 1.375\nPiezas: 0\nMetros: 0\n" +
                "Piezas: 0\nMetros: 0.875\nPiezas: 1\nMetros: 1\n" +
                "Piezas: 2\nMetros: 2.5\nPiezas: 0\nMetros: 0.0625\n" +
                "Piezas: 1\nMetros: 1.25\n"
            scenario.onActivity { editor(it).setText(eightBoundaries) }
            assertSuccessful(run(scenario), eightOutput)
            val eightDraft = eightBoundaries + "\n// Authored eight-person draft\n"
            scenario.onActivity { editor(it).setText(eightDraft) }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(eight.page - 1, field(it, "currentPage"))
                assertEquals(eightDraft, editor(it).text.toString())
                showPage(it, four.page - 1)
                assertEquals(fourDraft, editor(it).text.toString())
            }
            // Running the restored earlier page also detects a stale eight-
            // person source snapshot. The existing helper checks no charges or
            // exercise completions for every actual renderer execution.
            assertSuccessful(run(scenario), fourOutput)
            scenario.onActivity {
                showPage(it, eight.page - 1)
                assertEquals(eightDraft, editor(it).text.toString())
            }
        }
    }

    @Test fun recursiveGroupingLabsGuardInputsAndKeepIndependentDrafts() {
        val pairs = RECURSIVE_PAIRS
        val groups = RECURSIVE_GROUPS
        // These expectations come from complete groups in the stated problem,
        // independently of the recurrence used by the published solution.
        val pairCounts = listOf(0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6)
        val groupCounts = listOf(0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4)

        fun boundarySource(reference: String, function: String, expected: List<Int>): String {
            val main = "int main() {"
            val argument = Regex("    const int fichas = [0-9]+;\\n")
            val signature = "int $function(int fichas) {"
            assertEquals("Use the reference's real main", 1, reference.split(main).size - 1)
            assertEquals("Replace only its fixed example argument", 1, argument.findAll(reference).count())
            assertEquals("The reference receives the recursive argument by value", 1, reference.split(signature).size - 1)
            val monitored = argument.replaceFirst(reference, "")
                .replaceFirst(main, "int ejecutar_caso(int fichas) {")
                .replaceFirst(signature, """
                    $signature
                        ++qa_llamadas;
                        if (fichas < 0 || fichas > 12) { ++qa_invalidas; return 0; }
                """.trimIndent())
            // Only technical counters are injected. The reference's guarded
            // main body still controls whether the real recurrence is called.
            val driver = """
                int main() {
                    const int esperadas[] = {${expected.joinToString(",") { (it + 1).toString() }}};
                    for (int fichas = 0; fichas <= 12; ++fichas) {
                        qa_llamadas = 0;
                        qa_invalidas = 0;
                        const int anterior = fichas;
                        std::cout << "Caso:" << fichas << ':';
                        const int salida = ejecutar_caso(fichas);
                        std::cout << '\n';
                        if (salida != 0 || fichas != anterior || qa_invalidas != 0) return 91;
                        if (qa_llamadas != esperadas[fichas]) return 92;
                    }
                    const int invalidos[] = {-1, 13};
                    for (int fichas : invalidos) {
                        qa_llamadas = 0;
                        qa_invalidas = 0;
                        std::cout << "Caso:" << fichas << ':';
                        const int salida = ejecutar_caso(fichas);
                        std::cout << '\n';
                        if (salida != 0 || qa_invalidas != 0) return 93;
                        if (qa_llamadas != 0) return 94;
                    }
                    return 0;
                }
            """.trimIndent()
            return "int qa_llamadas = 0;\nint qa_invalidas = 0;\n" + monitored + "\n" + driver + "\n"
        }

        fun boundaryOutput(label: String, expected: List<Int>) = buildString {
            expected.forEachIndexed { fichas, count ->
                append("Caso:$fichas:$label:$count;Fichas:$fichas\n")
            }
            append("Caso:-1:Fuera del dominio\nCaso:13:Fuera del dominio\n")
        }

        withLab(pairs) { scenario, lesson, pairBlock ->
            assertSingleSourceSkeleton(scenario, pairBlock)
            val groupBlock = blockAt(lesson, groups.page, groups.block)
            assertEquals("The transfer coordinate remains an authored lab", "cpp_compiler", groupBlock.getString("type"))
            assertFalse(groupBlock.has("projectFiles"))
            val pairReference = referenceCode(lesson, 8, 2)
            val groupReference = referenceCode(lesson, 11, 2)
            scenario.onActivity { editor(it).setText(pairReference) }
            assertSuccessful(run(scenario), "Parejas:4;Fichas:9")

            val pairBoundaries = boundarySource(pairReference, "parejas_recursivas", pairCounts)
            val pairOutput = boundaryOutput("Parejas", pairCounts)
            scenario.onActivity { editor(it).setText(pairBoundaries) }
            assertSuccessful(run(scenario), pairOutput)
            val pairDraft = pairBoundaries + "\n// Authored pairs draft\n"
            scenario.onActivity {
                editor(it).setText(pairDraft)
                showPage(it, groups.page - 1)
            }

            assertSingleSourceSkeleton(scenario, groupBlock)
            scenario.onActivity { editor(it).setText(groupReference) }
            assertSuccessful(run(scenario), "Grupos:3;Fichas:10")
            val groupBoundaries = boundarySource(groupReference, "grupos_recursivos", groupCounts)
            val groupOutput = boundaryOutput("Grupos", groupCounts)
            scenario.onActivity { editor(it).setText(groupBoundaries) }
            assertSuccessful(run(scenario), groupOutput)
            val groupDraft = groupBoundaries + "\n// Authored groups-of-three draft\n"
            scenario.onActivity { editor(it).setText(groupDraft) }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(groups.page - 1, field(it, "currentPage"))
                assertEquals(groupDraft, editor(it).text.toString())
                showPage(it, pairs.page - 1)
                assertEquals(pairDraft, editor(it).text.toString())
            }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(pairs.page - 1, field(it, "currentPage"))
                assertEquals(pairDraft, editor(it).text.toString())
            }
            // Running after page changes and recreation also distinguishes a
            // restored pairs source from a stale groups-of-three snapshot.
            assertSuccessful(run(scenario), pairOutput)
            scenario.onActivity {
                showPage(it, groups.page - 1)
                assertEquals(groupDraft, editor(it).text.toString())
            }
        }
    }

    @Test fun referenceSwapLabsPreservePreviousValuesIdentityAndIndependentDrafts() {
        val swap = REFERENCE_SWAP
        val aliases = REFERENCE_ALIASES
        val swapCases = listOf(8 to -3, 7 to 2, 4 to 4, -5 to -1, 0 to 6, -12 to 12)
        val aliasCases = listOf(10 to -4, 4 to 4, 0 to 6, -12 to 12)

        fun exchangePrefix(reference: String): String {
            val marker = "int main() {"
            assertEquals("Keep the actual reference before its main", 1, reference.split(marker).size - 1)
            val prefix = reference.substringBefore(marker)
            assertEquals("Both original objects have mutable-reference parameters", 1,
                prefix.split("void intercambiar(int& primero, int& segundo) {").size - 1)
            return prefix
        }

        fun swapSource(reference: String) = exchangePrefix(reference) + buildString {
            append("int main() {\n")
            swapCases.forEach { (first, second) ->
                append("""
                    {
                        int primero = $first;
                        int segundo = $second;
                        intercambiar(primero, segundo);
                        if (primero != $second || segundo != $first) return 91;
                        std::cout << "Intercambio: " << primero << ' ' << segundo << '\n';
                        intercambiar(primero, segundo);
                        if (primero != $first || segundo != $second) return 92;
                        std::cout << "Restaurado: " << primero << ' ' << segundo << '\n';
                        intercambiar(primero, primero);
                        if (primero != $first || segundo != $second) return 93;
                        std::cout << "Compartido: " << primero << ' ' << segundo << '\n';
                    }
                """.trimIndent())
                append('\n')
            }
            append("    return 0;\n}\n")
        }

        val swapOutput = buildString {
            swapCases.forEach { (first, second) ->
                append("Intercambio: $second $first\nRestaurado: $first $second\nCompartido: $first $second\n")
            }
        }

        fun aliasSource(reference: String) = exchangePrefix(reference) + buildString {
            append("int main() {\n")
            aliasCases.forEach { (reserve, used) ->
                val marker = if (reserve == 12) -12 else 12
                append("""
                    {
                        int reserva = $reserve;
                        int usado = $used;
                        int& vista_reserva = reserva;
                        int& vista_usado = usado;
                        int& vista_doble = reserva;
                        const int copia = reserva;
                        const int& lectura = reserva;
                        intercambiar(vista_reserva, vista_usado);
                        if (reserva != $used || usado != $reserve) return 94;
                        std::cout << "Distintos: " << reserva << ' ' << usado << '\n';
                        intercambiar(vista_reserva, vista_doble);
                        if (reserva != $used || usado != $reserve) return 95;
                        std::cout << "Compartido: " << reserva << ' ' << usado << '\n';
                        intercambiar(vista_reserva, vista_usado);
                        if (reserva != $reserve || usado != $used) return 96;
                        std::cout << "Restaurado: " << reserva << ' ' << usado << '\n';
                        vista_reserva = $marker;
                        if (reserva != $marker || vista_doble != $marker || lectura != $marker
                            || copia != $reserve || usado != $used) return 97;
                        std::cout << "Vistas: " << reserva << ' ' << vista_doble << ' ' << lectura
                                  << ";Copia: " << copia << '\n';
                        vista_doble = $reserve;
                        if (reserva != $reserve || vista_reserva != $reserve || lectura != $reserve) return 98;
                    }
                """.trimIndent())
                append('\n')
            }
            append("    return 0;\n}\n")
        }

        val aliasOutput = buildString {
            aliasCases.forEach { (reserve, used) ->
                val marker = if (reserve == 12) -12 else 12
                append("Distintos: $used $reserve\nCompartido: $used $reserve\nRestaurado: $reserve $used\n")
                append("Vistas: $marker $marker $marker;Copia: $reserve\n")
            }
        }

        withLab(swap) { scenario, lesson, swapBlock ->
            assertSingleSourceSkeleton(scenario, swapBlock)
            val aliasBlock = blockAt(lesson, aliases.page, aliases.block)
            assertEquals("The alias construction has its own lab coordinate", "cpp_compiler", aliasBlock.getString("type"))
            assertFalse(aliasBlock.has("projectFiles"))
            val swapReference = referenceCode(lesson, 9, 3)
            val aliasReference = referenceCode(lesson, 14, 3)
            scenario.onActivity { editor(it).setText(swapReference) }
            assertSuccessful(run(scenario), "Antes: 2 -9\nDespues: -9 2\nDos llamadas: 2 -9\nMismo: 2")
            val swapBoundaries = swapSource(swapReference)
            scenario.onActivity { editor(it).setText(swapBoundaries) }
            assertSuccessful(run(scenario), swapOutput)

            // This later prediction supplies the new const-reference versus
            // independent-copy contrast; it runs in the real first lab editor.
            val constPrediction = blockAt(lesson, 16, 3)
            assertEquals("output_prediction", constPrediction.getString("type"))
            assertEquals("9 0 0 9", constPrediction.getJSONArray("answers").getString(0))
            scenario.onActivity { editor(it).setText(constPrediction.getString("code")) }
            assertSuccessful(run(scenario), "9 0 0 9\n")
            val swapDraft = swapBoundaries + "\n// Authored previous-values swap draft\n"
            scenario.onActivity {
                editor(it).setText(swapDraft)
                showPage(it, aliases.page - 1)
            }

            assertSingleSourceSkeleton(scenario, aliasBlock)
            scenario.onActivity { editor(it).setText(aliasReference) }
            assertSuccessful(run(scenario), "Distintos: 1 -8\nCompartido: 1 -8\nRestaurado: -8 1\n")
            val aliasBoundaries = aliasSource(aliasReference)
            scenario.onActivity { editor(it).setText(aliasBoundaries) }
            assertSuccessful(run(scenario), aliasOutput)
            val aliasDraft = aliasBoundaries + "\n// Authored shared-object views draft\n"
            scenario.onActivity { editor(it).setText(aliasDraft) }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(aliases.page - 1, field(it, "currentPage"))
                assertEquals(aliasDraft, editor(it).text.toString())
                showPage(it, swap.page - 1)
                assertEquals(swapDraft, editor(it).text.toString())
            }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(swap.page - 1, field(it, "currentPage"))
                assertEquals(swapDraft, editor(it).text.toString())
            }
            assertSuccessful(run(scenario), swapOutput)
            scenario.onActivity {
                showPage(it, aliases.page - 1)
                assertEquals(aliasDraft, editor(it).text.toString())
            }
        }
    }

    @Test fun diagnosticIntervalLabReproducesTheCauseAndPreservesItsRepairDraft() {
        withLab(DIAGNOSTIC_INTERVAL) { scenario, lesson, block ->
            assertSingleSourceSkeleton(scenario, block)
            val initial = block.getString("initialCode")
            val reference = referenceCode(lesson, 7, 3)
            assertSuccessful(run(scenario), "Dentro:3;Datos:6")

            val initialData = "const std::vector<int> datos{2, 4, 6, 8, 9, 4};"
            assertEquals("Reduce only the actual initial data", 1, initial.split(initialData).size - 1)
            val counterexample = initial.replaceFirst(initialData, "const std::vector<int> datos{8};")
            scenario.onActivity { editor(it).setText(counterexample) }
            assertSuccessful(run(scenario), "Dentro:0;Datos:1")

            scenario.onActivity { editor(it).setText(reference) }
            assertSuccessful(run(scenario), "Dentro:4;Datos:5")

            // Expected counts come from the closed interval in the problem,
            // including both ends, duplicates and the empty collection.
            val cases = listOf(
                listOf(2, 4, 6, 8, 9, 4) to 4,
                listOf(8) to 1, emptyList<Int>() to 0, listOf(3) to 1,
                listOf(8, 8) to 2, listOf(2, 9) to 0, listOf(4, 6) to 2)
            val main = "int main() {"
            assertEquals("Use the actual reference function", 1, reference.split(main).size - 1)
            val boundaries = reference.substringBefore(main) + buildString {
                append("int main() {\n")
                cases.forEachIndexed { index, (values, expected) ->
                    append("""
                        {
                            const std::vector<int> datos{${values.joinToString(", ")}};
                            const std::vector<int> anteriores = datos;
                            const int dentro = contar_en_intervalo(datos, 3, 8);
                            if (datos != anteriores || dentro != $expected) return 91;
                            std::cout << "Caso:$index;Dentro:" << dentro << ";Datos:" << datos.size() << '\n';
                        }
                    """.trimIndent())
                    append('\n')
                }
                append("""
                    {
                        const std::vector<int> datos{4, 5, 5, 6};
                        const std::vector<int> anteriores = datos;
                        const int dentro = contar_en_intervalo(datos, 5, 5);
                        if (datos != anteriores || dentro != 2) return 92;
                        std::cout << "Iguales;Dentro:" << dentro << ";Datos:" << datos.size() << '\n';
                    }
                    {
                        const std::vector<int> datos{0, 10, 0, 10, 5, 5, 1, 9};
                        const std::vector<int> anteriores = datos;
                        const int dentro = contar_en_intervalo(datos, 0, 10);
                        if (datos != anteriores || dentro != 8) return 93;
                        std::cout << "Extremos;Dentro:" << dentro << ";Datos:" << datos.size() << '\n';
                    }
                    return 0;
                }
                """.trimIndent())
                append('\n')
            }
            val boundaryOutput = buildString {
                cases.forEachIndexed { index, (values, expected) ->
                    append("Caso:$index;Dentro:$expected;Datos:${values.size}\n")
                }
                append("Iguales;Dentro:2;Datos:4\nExtremos;Dentro:8;Datos:8\n")
            }
            scenario.onActivity { editor(it).setText(boundaries) }
            assertSuccessful(run(scenario), boundaryOutput)

            val faultyCondition = "valor >= inferior && valor < superior"
            assertEquals("Repair one actual cause without replacing the data", 1,
                initial.split(faultyCondition).size - 1)
            val draft = initial.replaceFirst(faultyCondition, "valor >= inferior && valor <= superior") + """

                // EXPECTATIVA anotada: Dentro:4;Datos:6.
                // CONTRAEJEMPLO: {8}, intervalo 3..8; esperado 1, observado antes 0.
                // HIPOTESIS: la comparacion excluye el extremo superior permitido.
                // TRAZA: 8 | falso antes | contador 0 | contador 0.
                // CAMBIO: incluir 8; conservar el recorrido y los datos originales.
                // REGRESION: vacio 0; {3} 1; {8,8} 2; inicial 4.
            """.trimIndent() + "\n"
            scenario.onActivity { editor(it).setText(draft) }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(DIAGNOSTIC_INTERVAL.page - 1, field(it, "currentPage"))
                assertEquals(draft, editor(it).text.toString())
                assertEquals("", input(it).text.toString())
            }
            assertSuccessful(run(scenario), "Dentro:4;Datos:6")
        }
    }

    @Test fun refactoringReportLabPreservesEveryLabelOrderAndItsDraft() {
        // This oracle follows the published domain and presentation contract,
        // rather than treating the first implementation as the expected result.
        fun expectedReport(values: List<Int>) = buildString {
            values.forEach { value ->
                val label = when {
                    value !in -4..9 -> "Fuera"
                    value < 0 -> "Fria"
                    value == 0 -> "Neutra"
                    else -> "Calida"
                }
                append("Dato=$value:$label\n")
            }
            val valid = values.filter { it in -4..9 }
            append("Resumen=${valid.size}:${values.size - valid.size}:${valid.sum()}")
        }
        val originalValues = listOf(-4, 0, 7, 10, -5)
        val referenceValues = listOf(9, -5, -1, 0)
        val cases = listOf(originalValues, emptyList(), listOf(-5, -4, -3), listOf(8, 9, 10),
            listOf(-1, 0, 1), listOf(-5, 10), listOf(9, 9, -4, 0), listOf(0, -4, 9, 0, 10, -5))

        fun casesSource(source: String): String {
            val main = "int main() {"
            val data = Regex("    std::vector<int> lecturas\\{[^\\n]*\\};\\n")
            assertEquals("Use the published main body without changing its decisions", 1, source.split(main).size - 1)
            assertEquals("Only the fixed example collection is replaced", 1, data.findAll(source).count())
            val actual = data.replaceFirst(source, "").replaceFirst(main,
                "int emitir_informe(const std::vector<int>& lecturas) {")
            return actual + buildString {
                append("\nint main() {\n")
                cases.forEachIndexed { index, values ->
                    append("""
                        {
                            const std::vector<int> lecturas{${values.joinToString(", ")}};
                            const std::vector<int> anteriores = lecturas;
                            std::cout << "Caso:$index\n";
                            const int salida = emitir_informe(lecturas);
                            std::cout << '\n';
                            if (salida != 0 || lecturas != anteriores) return 91;
                        }
                    """.trimIndent())
                    append('\n')
                }
                append("    return 0;\n}\n")
            }
        }
        val casesOutput = buildString {
            cases.forEachIndexed { index, values -> append("Caso:$index\n${expectedReport(values)}\n") }
        }

        withLab(REFACTOR_REPORT) { scenario, lesson, block ->
            assertSingleSourceSkeleton(scenario, block)
            val initial = block.getString("initialCode")
            val reference = referenceCode(lesson, 7, 4)
            assertSuccessful(run(scenario), expectedReport(originalValues))
            val initialCases = casesSource(initial)
            scenario.onActivity { editor(it).setText(initialCases) }
            assertSuccessful(run(scenario), casesOutput)
            scenario.onActivity { editor(it).setText(reference) }
            assertSuccessful(run(scenario), expectedReport(referenceValues))
            val referenceCases = casesSource(reference)
            scenario.onActivity { editor(it).setText(referenceCases) }
            assertSuccessful(run(scenario), casesOutput)

            val draft = referenceCases + """

                // Extraccion: etiqueta_de_lectura recibe un dato valido y devuelve texto.
                // main comprueba el dominio antes de llamar y conserva el informe.
                // Evidencia conservada: fronteras, cero, rechazos, repetidos, orden y vacio.
            """.trimIndent() + "\n"
            scenario.onActivity { editor(it).setText(draft) }
            scenario.recreate()
            scenario.onActivity {
                assertEquals(REFACTOR_REPORT.page - 1, field(it, "currentPage"))
                assertEquals(draft, editor(it).text.toString())
                assertEquals("", input(it).text.toString())
            }
            assertSuccessful(run(scenario), casesOutput)
        }
    }

    @Test fun nestedReviewLabsMatchBothPoliciesAcrossAllDimensions() {
        // Explicit accepted pairs define the two policies independently of the
        // reference loops. Only the available dimensions truncate these rows.
        val firstRows = listOf(listOf(1), listOf(1, 2), listOf(1, 2, 3))
        val parityRows = listOf(listOf(1, 3), listOf(2, 4), listOf(1, 3))

        fun dimensionedSource(reference: String, publishedTeams: Int, publishedTools: Int,
                              teams: Int, tools: Int): String {
            val teamDeclaration = "    int equipos{$publishedTeams};"
            val toolDeclaration = "    int herramientas{$publishedTools};"
            assertEquals("Replace exactly one published team dimension", 1,
                reference.split(teamDeclaration).size - 1)
            assertEquals("Replace exactly one published tool dimension", 1,
                reference.split(toolDeclaration).size - 1)
            // Every other character, including main and both loop bodies, is
            // preserved. No driver or replacement algorithm is injected.
            val source = reference.replaceFirst(teamDeclaration, "    int equipos{$teams};")
                .replaceFirst(toolDeclaration, "    int herramientas{$tools};")
            if (teams == publishedTeams && tools == publishedTools) assertEquals(reference, source)
            return source
        }

        fun verifyAllDimensions(scenario: ActivityScenario<LessonActivity>, reference: String,
                                publishedTeams: Int, publishedTools: Int,
                                rows: List<List<Int>>, policy: String): String {
            var cases = 0
            var lastSource = reference
            for (teams in 0..3) {
                for (tools in 0..4) {
                    val source = dimensionedSource(reference, publishedTeams, publishedTools, teams, tools)
                    scenario.onActivity { editor(it).setText(source) }
                    try {
                        assertSuccessful(run(scenario), nestedReviewOutput(teams, tools, rows))
                    } catch (failure: AssertionError) {
                        // Add the case identity while retaining the original
                        // strict assertion as the cause; never retry the job.
                        throw AssertionError("$policy equipos=$teams herramientas=$tools", failure)
                    }
                    ++cases
                    lastSource = source
                }
            }
            assertEquals("All four by five dimension combinations ran", 20, cases)
            return lastSource
        }

        withLab(NESTED_REVIEW) { scenario, lesson, firstBlock ->
            assertSingleSourceSkeleton(scenario, firstBlock)
            val parityBlock = blockAt(lesson, NESTED_PARITY.page, NESTED_PARITY.block)
            assertEquals("The transfer has its own real lab coordinate", "cpp_compiler", parityBlock.getString("type"))
            assertFalse(parityBlock.has("projectFiles"))
            val skeleton = "#include <iostream>\n\nint main() {\n" +
                "    // Construye aqui el programa a partir del contrato.\n    return 0;\n}\n"
            assertEquals("The first construction starts without an algorithm", skeleton, firstBlock.getString("initialCode"))
            assertEquals("The transfer also starts without an algorithm", skeleton, parityBlock.getString("initialCode"))
            val firstReference = referenceCode(lesson, 9, 3)
            val parityReference = referenceCode(lesson, 12, 3)

            val firstCases = verifyAllDimensions(scenario, firstReference, 2, 3, firstRows, "herramienta<=equipo")
            val firstDraft = firstCases + "\n// First nested-review lab draft\n"
            scenario.onActivity {
                editor(it).setText(firstDraft)
                showPage(it, NESTED_PARITY.page - 1)
            }
            assertSingleSourceSkeleton(scenario, parityBlock)
            val parityCases = verifyAllDimensions(scenario, parityReference, 3, 3, parityRows, "paridad de equipo+herramienta")
            val parityDraft = parityCases + "\n// Independent parity-transfer lab draft\n"
            assertNotEquals(firstDraft, parityDraft)
            scenario.onActivity { editor(it).setText(parityDraft) }
        }
    }

    @Test fun nestedReviewLabsRestoreIndependentDraftsAfterPageChangesAndRecreation() {
        val firstRows = listOf(listOf(1), listOf(1, 2), listOf(1, 2, 3))
        val parityRows = listOf(listOf(1, 3), listOf(2, 4), listOf(1, 3))
        withLab(NESTED_REVIEW) { scenario, lesson, firstBlock ->
            assertSingleSourceSkeleton(scenario, firstBlock)
            val parityBlock = blockAt(lesson, NESTED_PARITY.page, NESTED_PARITY.block)
            assertEquals("The transfer has its own real lab coordinate", "cpp_compiler", parityBlock.getString("type"))
            assertFalse(parityBlock.has("projectFiles"))
            val firstReference = referenceCode(lesson, 9, 3)
            val parityReference = referenceCode(lesson, 12, 3)
            scenario.onActivity { editor(it).setText(firstReference) }
            assertSuccessful(run(scenario), nestedReviewOutput(2, 3, firstRows))
            val firstDraft = firstReference + "\n// First nested-review lab draft\n"
            scenario.onActivity {
                editor(it).setText(firstDraft)
                showPage(it, NESTED_PARITY.page - 1)
            }
            assertSingleSourceSkeleton(scenario, parityBlock)
            scenario.onActivity { editor(it).setText(parityReference) }
            assertSuccessful(run(scenario), nestedReviewOutput(3, 3, parityRows))
            val parityDraft = parityReference + "\n// Independent parity-transfer lab draft\n"
            assertNotEquals(firstDraft, parityDraft)
            scenario.onActivity { editor(it).setText(parityDraft) }
            recreateWithAwakeQaDisplay(scenario)
            scenario.onActivity {
                assertEquals(NESTED_PARITY.page - 1, field(it, "currentPage"))
                assertEquals(parityDraft, editor(it).text.toString())
                assertEquals("", input(it).text.toString())
                showPage(it, NESTED_REVIEW.page - 1)
                assertEquals(firstDraft, editor(it).text.toString())
            }
            assertSuccessful(run(scenario), nestedReviewOutput(2, 3, firstRows))
            recreateWithAwakeQaDisplay(scenario)
            scenario.onActivity {
                assertEquals(NESTED_REVIEW.page - 1, field(it, "currentPage"))
                assertEquals(firstDraft, editor(it).text.toString())
                showPage(it, NESTED_PARITY.page - 1)
                assertEquals(parityDraft, editor(it).text.toString())
                assertEquals("", input(it).text.toString())
            }
            assertSuccessful(run(scenario), nestedReviewOutput(3, 3, parityRows))
        }
    }

    @Test fun depositLabsRestoreIndependentSourceAndInputDraftsAfterRecreation() {
        fun requireDepositScaffold(block: JSONObject) {
            assertEquals("The authored deposit lab has its exact compiler type", "cpp_compiler", block.getString("type"))
            assertFalse("Compiler identity uses coordinates, not an unsupported block ID", block.has("id"))
            assertFalse(block.has("projectFiles"))
            assertFalse(block.has("initialStandardInput"))
            val source = block.getString("initialCode")
            assertTrue("The scaffold retains the previously learned class", source.contains("class Deposito {"))
            assertTrue("The scaffold retains a minimal main", source.contains("int main() {"))
            assertFalse("The lab does not provide Resultado_doble", source.contains("struct Resultado_doble"))
            assertFalse("The lab does not provide the batch record", source.contains("struct Solicitud"))
            assertFalse("The lab does not provide a procesar definition or call",
                Regex("\\bprocesar\\s*\\(").containsMatchIn(source))
            assertFalse("The lab does not provide a loop",
                Regex("\\b(for|while)\\s*\\(").containsMatchIn(source))
            val mainBody = source.substringAfter("int main() {").lineSequence()
                .map { it.substringBefore("//").trim() }.filter { it.isNotEmpty() }.joinToString("\n")
            assertEquals("The main supplies no solution statements", "return 0;\n}", mainBody)
        }

        withLab(DEPOSIT_OPERATIONS) { scenario, lesson, firstBlock ->
            requireDepositScaffold(firstBlock)
            assertSingleSourceSkeleton(scenario, firstBlock)
            val batchBlock = blockAt(lesson, DEPOSIT_BATCH.page, DEPOSIT_BATCH.block)
            requireDepositScaffold(batchBlock)
            val firstReference = referenceCode(lesson, 11, 3)
            val batchReference = referenceCode(lesson, 14, 3)
            assertEquals("cpp", blockAt(lesson, 11, 3).getString("language"))
            assertEquals("cpp", blockAt(lesson, 14, 3).getString("language"))
            val firstOutput = "Distintos\nResultados=1:1\nEstados=1:6\nObservacion=4:1\n" +
                "Mismo\nResultados=1:1\nEstados=5:5\nObservacion=4:5"
            val batchOutput = "Paso=0:1;Estados=2:6\nPaso=1:1;Estados=5:6\n" +
                "Paso=1:0;Estados=4:6\nPaso=0:1;Estados=5:6\n" +
                "Totales=2:3\nObservacion=2:5"
            val firstDraft = firstReference + "\n// Independent deposit-operations draft\n"
            val batchDraft = batchReference + "\n// Independent deposit-batch draft\n"
            // These programs do not read cin; prepared text checks only draft identity.
            val firstInput = "deposit operations draft\ninput A"
            val batchInput = "deposit batch draft\ninput B\n"
            assertNotEquals(firstDraft, batchDraft)
            assertNotEquals(firstInput, batchInput)

            scenario.onActivity { editor(it).setText(firstReference) }
            assertSuccessful(run(scenario), firstOutput)
            scenario.onActivity {
                editor(it).setText(firstDraft)
                input(it).setText(firstInput)
                showPage(it, DEPOSIT_BATCH.page - 1)
            }
            assertSingleSourceSkeleton(scenario, batchBlock)
            scenario.onActivity {
                assertEquals(DEPOSIT_BATCH.page - 1, field(it, "currentPage"))
                assertEquals("", input(it).text.toString())
                editor(it).setText(batchReference)
            }
            assertSuccessful(run(scenario), batchOutput)
            scenario.onActivity {
                editor(it).setText(batchDraft)
                input(it).setText(batchInput)
                showPage(it, DEPOSIT_OPERATIONS.page - 1)
                assertEquals(firstDraft, editor(it).text.toString())
                assertEquals(firstInput, input(it).text.toString())
            }
            recreateWithAwakeQaDisplay(scenario)
            scenario.onActivity {
                assertEquals(DEPOSIT_OPERATIONS.page - 1, field(it, "currentPage"))
                assertEquals(firstDraft, editor(it).text.toString())
                assertEquals(firstInput, input(it).text.toString())
            }
            assertSuccessful(run(scenario), firstOutput)
            scenario.onActivity {
                showPage(it, DEPOSIT_BATCH.page - 1)
                assertEquals(batchDraft, editor(it).text.toString())
                assertEquals(batchInput, input(it).text.toString())
            }
            recreateWithAwakeQaDisplay(scenario)
            scenario.onActivity {
                assertEquals(DEPOSIT_BATCH.page - 1, field(it, "currentPage"))
                assertEquals(batchDraft, editor(it).text.toString())
                assertEquals(batchInput, input(it).text.toString())
            }
            assertSuccessful(run(scenario), batchOutput)
        }
    }

    private fun nestedReviewOutput(teams: Int, tools: Int, rows: List<List<Int>>) = buildString {
        var total = 0
        rows.take(teams).forEachIndexed { index, accepted ->
            val available = accepted.filter { it <= tools }
            available.forEach { tool -> append("Revision=${index + 1}:$tool\n") }
            append("Equipo=${index + 1};Revisadas=${available.size}\n")
            total += available.size
        }
        append("Total=$total")
    }

    private fun recreateWithAwakeQaDisplay(scenario: ActivityScenario<LessonActivity>) {
        // This helper is only for the new short lifecycle check. Every guard
        // precedes a possible wake command and any QA-window modification.
        assertEquals(QA_PACKAGE, context.packageName)
        assertEquals(QA_PACKAGE, BuildConfig.APPLICATION_ID)
        assertEquals(QA_PACKAGE, context.applicationContext.packageName)
        val power = context.getSystemService(Context.POWER_SERVICE) as PowerManager
        if (!power.isInteractive) {
            ParcelFileDescriptor.AutoCloseInputStream(
                instrumentation.uiAutomation.executeShellCommand("input keyevent KEYCODE_WAKEUP")
            ).use { it.readBytes() }
        }
        val wakeDeadline = SystemClock.uptimeMillis() + 5_000L
        while (!power.isInteractive && SystemClock.uptimeMillis() < wakeDeadline) SystemClock.sleep(50L)
        assertTrue("The QA display must be interactive immediately before recreation", power.isInteractive)
        val keyguard = context.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
        assertFalse("Dismiss the visual lock screen before QA recreation; this test does not dismiss it", keyguard.isKeyguardLocked)
        assertFalse("The QA recreation does not unlock device security", keyguard.isDeviceLocked)
        scenario.onActivity {
            assertEquals(QA_PACKAGE, it.packageName)
            it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            assertTrue("Keep the original QA window awake before its lifecycle transition",
                (it.window.attributes.flags and WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0)
            assertFalse("The QA activity must not be finishing", it.isFinishing)
            assertFalse("The QA activity must not be destroyed", it.isDestroyed)
        }
        val resumeDeadline = SystemClock.uptimeMillis() + 5_000L
        instrumentation.waitForIdleSync()
        while (scenario.state != Lifecycle.State.RESUMED && SystemClock.uptimeMillis() < resumeDeadline) {
            SystemClock.sleep(50L)
            instrumentation.waitForIdleSync()
        }
        assertEquals("The same QA scenario must already be RESUMED before recreation; " +
            "interactive=${power.isInteractive}, keyguardLocked=${keyguard.isKeyguardLocked}, " +
            "deviceLocked=${keyguard.isDeviceLocked}",
            Lifecycle.State.RESUMED, scenario.state)
        // A single ordinary recreate retains all ActivityScenario guarantees.
        // Neither the recreation nor any native job is retried automatically.
        scenario.recreate()
    }

    private fun withLab(location: LabLocation,
                        test: (ActivityScenario<LessonActivity>, JSONObject, JSONObject) -> Unit) {
        val lesson = LessonRepository.loadLessonJson(context, ROUTE, location.lesson)
        assertEquals(location.lesson, lesson.getString("id"))
        val block = blockAt(lesson, location.page, location.block)
        assertEquals("The exact published lab coordinate still matches", "cpp_compiler", block.getString("type"))
        ProgressRepository.resetLesson(context, ROUTE, location.lesson)
        ProgressRepository.saveCurrentPage(context, ROUTE, location.lesson, location.page - 1)
        val parts = location.lesson.split("__")
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", ROUTE)
            .putExtra("courseId", parts[0]).putExtra("unitId", parts[1]).putExtra("lessonId", location.lesson)
            .putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { scenario ->
            scenario.onActivity {
                assertEquals("Activity storage remains isolated", QA_PACKAGE, it.packageName)
                it.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                assertEquals(location.lesson, (field(it, "lessonJson") as JSONObject).getString("id"))
                assertEquals(location.page - 1, field(it, "currentPage"))
                assertEquals(block.optString("initialStandardInput", ""), input(it).text.toString())
            }
            instrumentation.waitForIdleSync()
            test(scenario, lesson, block)
        }
    }

    private fun assertSingleSourceSkeleton(scenario: ActivityScenario<LessonActivity>, block: JSONObject) {
        assertFalse(block.has("projectFiles"))
        scenario.onActivity {
            assertEquals(block.getString("initialCode"), editor(it).text.toString())
            assertFalse(descendants(content(it)).any { view -> view.tag?.toString()?.startsWith("cpp_file_") == true })
        }
    }

    private fun assertProjectSkeleton(scenario: ActivityScenario<LessonActivity>, block: JSONObject) {
        val files = block.getJSONArray("projectFiles")
        assertEquals(4, files.length())
        for (i in 0 until files.length()) {
            val file = files.getJSONObject(i)
            assertEquals(file.getString("initialCode"), readFile(scenario, file.getString("path")))
        }
        assertSources(scenario, setOf("conteo.cpp", "estado.cpp", "main.cpp"))
    }

    private fun originalProject(lesson: JSONObject) = linkedMapOf(
        "estante.h" to referenceCode(lesson, 6, 3), "conteo.cpp" to referenceCode(lesson, 6, 5),
        "estado.cpp" to referenceCode(lesson, 7, 3), "main.cpp" to referenceCode(lesson, 7, 5))

    private fun blockAt(lesson: JSONObject, page: Int, block: Int): JSONObject = lesson.getJSONArray("pages")
        .getJSONObject(page - 1).getJSONArray("blocks").getJSONObject(block - 1)

    private fun referenceCode(lesson: JSONObject, page: Int, block: Int): String {
        val source = blockAt(lesson, page, block)
        assertEquals("The later reference is a published code block", "code", source.getString("type"))
        return source.getString("code")
    }

    private fun fillProject(scenario: ActivityScenario<LessonActivity>, files: Map<String, String>) =
        files.forEach { (path, source) -> editFile(scenario, path) { source } }

    private fun editFile(scenario: ActivityScenario<LessonActivity>, path: String, transform: (String) -> String) {
        scenario.onActivity {
            tagged(it, "cpp_file_$path").performClick()
            editor(it).setText(transform(editor(it).text.toString()))
        }
        instrumentation.waitForIdleSync()
    }

    private fun readFile(scenario: ActivityScenario<LessonActivity>, path: String): String {
        var source = ""
        scenario.onActivity {
            tagged(it, "cpp_file_$path").performClick()
            source = editor(it).text.toString()
        }
        return source
    }

    private fun enableSource(scenario: ActivityScenario<LessonActivity>, path: String, enabled: Boolean) {
        scenario.onActivity {
            tagged(it, "cpp_file_$path").performClick()
            val checkbox = tagged(it, "cpp_source_enabled") as CheckBox
            if (checkbox.isChecked != enabled) checkbox.performClick()
            assertEquals(enabled, checkbox.isChecked)
        }
    }

    private fun assertSources(scenario: ActivityScenario<LessonActivity>, expected: Set<String>) {
        scenario.onActivity {
            listOf("conteo.cpp", "estado.cpp", "main.cpp").forEach { path ->
                tagged(it, "cpp_file_$path").performClick()
                assertEquals("Selection for $path", path in expected, (tagged(it, "cpp_source_enabled") as CheckBox).isChecked)
            }
        }
    }

    private fun showPage(activity: LessonActivity, page: Int) {
        invoke(activity, "saveCurrentPageState")
        LessonActivity::class.java.getDeclaredField("currentPage").apply { isAccessible = true }.set(activity, page)
        ProgressRepository.saveCurrentPage(context, ROUTE, field(activity, "currentLessonId") as String, page)
        invoke(activity, "renderCurrentPage")
    }

    private fun runWithInput(scenario: ActivityScenario<LessonActivity>, source: String, expected: String) {
        scenario.onActivity { input(it).setText(source) }
        assertSuccessful(run(scenario), expected)
    }

    private fun run(scenario: ActivityScenario<LessonActivity>): RunResult {
        var completedBefore = 0
        var reserveBefore = 0f
        lateinit var startedActivity: LessonActivity
        lateinit var startedButton: View
        lateinit var startedStatus: TextView
        lateinit var startedCompilation: TextView
        lateinit var startedStdout: TextView
        lateinit var startedStderr: TextView
        lateinit var startedEditor: EditText
        lateinit var startedInput: EditText
        var startedPage = -1
        var startedLesson = ""
        var startedSource = ""
        var startedStandardInput = ""
        var lastStatus = ""
        var lastPhase = ""
        instrumentation.waitForIdleSync()
        scenario.onActivity {
            startedActivity = it
            startedPage = field(it, "currentPage") as Int
            startedLesson = field(it, "currentLessonId") as String
            startedEditor = editor(it)
            startedInput = input(it)
            startedSource = startedEditor.text.toString()
            startedStandardInput = startedInput.text.toString()
            startedButton = tagged(it, "cpp_run")
            startedStatus = tagged(it, "cpp_status") as TextView
            startedCompilation = tagged(it, "cpp_compilation_output") as TextView
            startedStdout = tagged(it, "cpp_stdout") as TextView
            startedStderr = tagged(it, "cpp_stderr") as TextView
            completedBefore = field(it, "completedExercises") as Int
            reserveBefore = context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE).getFloat("reserve_$ROUTE", 0f)
            assertTrue("The real renderer exposes an attached, enabled Run button",
                startedButton.isAttachedToWindow && startedButton.isEnabled)
            startedButton.performClick()
            assertFalse("The native job actually starts", startedButton.isEnabled)
            assertEquals("The clicked renderer marks this status as the active job", "BUILDING",
                startedStatus.getTag(R.id.cpp_lab_build_phase))
            assertEquals(it.getString(R.string.exercise_compiling), startedStatus.text.toString())
        }
        val job = "$startedLesson p${startedPage + 1} activity=${System.identityHashCode(startedActivity)} " +
            "run=${System.identityHashCode(startedButton)} status=${System.identityHashCode(startedStatus)}"
        val deadline = SystemClock.uptimeMillis() + 120_000L
        var finished = false
        do {
            instrumentation.waitForIdleSync()
            scenario.onActivity {
                assertSame("Activity changed while awaiting $job", startedActivity, it)
                assertEquals("Lesson changed while awaiting $job", startedLesson, field(it, "currentLessonId"))
                assertEquals("Page changed while awaiting $job", startedPage, field(it, "currentPage"))
                assertSame("Run view changed while awaiting $job", startedButton, tagged(it, "cpp_run"))
                assertSame("Status view changed while awaiting $job", startedStatus, tagged(it, "cpp_status"))
                assertSame("Compilation view changed while awaiting $job", startedCompilation, tagged(it, "cpp_compilation_output"))
                assertSame("Stdout view changed while awaiting $job", startedStdout, tagged(it, "cpp_stdout"))
                assertSame("Stderr view changed while awaiting $job", startedStderr, tagged(it, "cpp_stderr"))
                assertSame("Editor view changed while awaiting $job", startedEditor, editor(it))
                assertSame("Input view changed while awaiting $job", startedInput, input(it))
                assertTrue("The active renderer detached while awaiting $job",
                    startedButton.isAttachedToWindow && startedStatus.isAttachedToWindow)
                assertEquals("Source changed while awaiting $job", startedSource, startedEditor.text.toString())
                assertEquals("Prepared input changed while awaiting $job", startedStandardInput, startedInput.text.toString())
                lastStatus = startedStatus.text.toString()
                lastPhase = startedStatus.getTag(R.id.cpp_lab_build_phase)?.toString().orEmpty()
                finished = startedButton.isEnabled && lastStatus != it.getString(R.string.cpp_runtime_ready) &&
                    lastStatus != it.getString(R.string.exercise_compiling)
            }
            if (!finished) SystemClock.sleep(100L)
        } while (!finished && SystemClock.uptimeMillis() < deadline)
        assertTrue("The same native job finishes within 120 seconds: $job " +
            "status=$lastStatus phase=$lastPhase", finished)
        var result: RunResult? = null
        scenario.onActivity {
            assertSame("The completed job still belongs to $job", startedActivity, it)
            assertSame(startedStatus, tagged(it, "cpp_status"))
            assertSame(startedCompilation, tagged(it, "cpp_compilation_output"))
            assertSame(startedStdout, tagged(it, "cpp_stdout"))
            assertSame(startedStderr, tagged(it, "cpp_stderr"))
            result = RunResult(startedStatus.text.toString(),
                startedStatus.getTag(R.id.cpp_lab_build_phase)?.toString().orEmpty(),
                startedCompilation.text.toString(), startedStdout.text.toString(), startedStderr.text.toString(),
                it.getString(R.string.cpp_runtime_finished, 0))
            assertEquals("Compiler labs do not complete evaluated exercises", completedBefore, field(it, "completedExercises"))
            assertTrue("Compiler labs do not charge attempts", context.getSharedPreferences(ATTEMPTS, Context.MODE_PRIVATE)
                .getFloat("reserve_$ROUTE", 0f) >= reserveBefore)
        }
        return requireNotNull(result).also {
            assertFalse("Diagnostics must use virtual paths", it.compilation.contains("cpp-jobs/"))
            assertFalse(it.compilation.contains(context.cacheDir.absolutePath))
        }
    }

    private fun assertSuccessful(result: RunResult, stdout: String) {
        assertEquals("Successful execution: $result", result.finishedStatus, result.status)
        assertEquals("", result.compilation)
        assertEquals("Exact stdout, including line endings", stdout, result.stdout)
        assertEquals("", result.stderr)
    }

    private fun assertCompilationFailure(result: RunResult, file: String, symbol: String) {
        assertEquals(result.compilation, "COMPILATION", result.phase)
        assertTrue(result.compilation, result.compilation.contains(file))
        assertTrue(result.compilation, result.compilation.contains(symbol))
        assertEquals("", result.stdout)
        assertEquals("", result.stderr)
    }

    private fun assertLinkFailure(result: RunResult, symbol: String) {
        assertEquals(result.compilation, "LINK", result.phase)
        assertTrue(result.compilation, result.compilation.contains("undefined symbol", ignoreCase = true))
        assertTrue(result.compilation, result.compilation.contains(symbol))
        assertEquals("", result.stdout)
        assertEquals("", result.stderr)
    }

    private fun linesOutput(rows: List<Pair<String, Int>>, accepted: Int, occupied: Int) = buildString {
        rows.forEach { (label, value) -> append("$label | Ocupadas: $value\n") }
        append("Aceptadas: $accepted\nOcupadas: $occupied\n")
    }

    private fun boundaryMain(cases: List<Pair<Int, Int>>) = buildString {
        append("#include \"estante.h\"\n#include <iostream>\n\nint main() {\n")
        cases.forEach { (small, large) ->
            append("""
                {
                    const int pequenas{$small};
                    const int grandes{$large};
                    if (!cantidades_validas(pequenas, grandes)) {
                        std::cout << "Cantidades invalidas\n";
                    } else {
                        std::cout << "Espacios: " << espacios_ocupados(pequenas, grandes) << '\n';
                        std::cout << "Carga: " << descripcion_carga(pequenas, grandes) << '\n';
                    }
                }
            """.trimIndent())
            append('\n')
        }
        append("    return 0;\n}\n")
    }

    private fun content(activity: LessonActivity): ViewGroup = activity.findViewById(R.id.lessonContent)
    private fun editor(activity: LessonActivity) = tagged(activity, "cpp_code_input") as EditText
    private fun input(activity: LessonActivity) = tagged(activity, "cpp_input") as EditText
    private fun text(activity: LessonActivity, tag: String) = (tagged(activity, tag) as TextView).text.toString()
    private fun tagged(activity: LessonActivity, tag: String): View = descendants(content(activity)).single { it.tag == tag }
    private fun descendants(root: View): List<View> = buildList {
        add(root)
        if (root is ViewGroup) for (index in 0 until root.childCount) addAll(descendants(root.getChildAt(index)))
    }
    private fun field(activity: LessonActivity, name: String): Any? = LessonActivity::class.java.getDeclaredField(name)
        .apply { isAccessible = true }.get(activity)
    private fun invoke(activity: LessonActivity, name: String) = LessonActivity::class.java.getDeclaredMethod(name)
        .apply { isAccessible = true }.invoke(activity)

    private data class RunResult(val status: String, val phase: String, val compilation: String,
                                 val stdout: String, val stderr: String, val finishedStatus: String)
    private data class LabLocation(val lesson: String, val page: Int, val block: Int) {
        fun draftPrefix() = "$ROUTE/$lesson/${page - 1}/${block - 1}/"
    }

    companion object {
        private const val QA_PACKAGE = "com.learngraphicsprogramming.app.curricularqa"
        private const val ROUTE = "programming"
        private const val DRAFTS = "cpp_lesson_drafts"
        private const val ATTEMPTS = "attempt_reserve_seconds_test"
        private val THREE_INPUTS = LabLocation("cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida", 10, 4)
        private val LINES = LabLocation("cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_lector_de_entrada_preparada", 4, 6)
        private val PROJECT = LabLocation("cpp_fundamentals__funciones_y_contratos__construir_un_proyecto_desde_su_interfaz", 4, 3)
        private val TRANSFER = LabLocation(PROJECT.lesson, 10, 4)
        private val OVERLOAD_FOUR = LabLocation("cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas", 8, 5)
        private val OVERLOAD_EIGHT = LabLocation(OVERLOAD_FOUR.lesson, 11, 4)
        private val RECURSIVE_PAIRS = LabLocation("cpp_fundamentals__funciones_y_contratos__recursion_finita_y_comparacion", 7, 5)
        private val RECURSIVE_GROUPS = LabLocation(RECURSIVE_PAIRS.lesson, 10, 4)
        private val REFERENCE_SWAP = LabLocation("cpp_fundamentals__funciones_y_contratos__referencias_para_modificar", 8, 5)
        private val REFERENCE_ALIASES = LabLocation(REFERENCE_SWAP.lesson, 13, 4)
        private val DIAGNOSTIC_INTERVAL = LabLocation("cpp_fundamentals__errores_y_construccion_gradual__distinguir_fallos_y_localizar_la_causa", 6, 5)
        private val REFACTOR_REPORT = LabLocation("cpp_fundamentals__errores_y_construccion_gradual__limpiar_sin_cambiar_el_comportamiento", 6, 5)
        private val NESTED_REVIEW = LabLocation("cpp_fundamentals__repeticion_y_terminacion__recorridos_anidados", 8, 5)
        private val NESTED_PARITY = LabLocation(NESTED_REVIEW.lesson, 11, 5)
        private val DEPOSIT_OPERATIONS = LabLocation("cpp_fundamentals__datos_propios_y_estado_valido__transferir_invariantes_a_un_deposito", 10, 7)
        private val DEPOSIT_BATCH = LabLocation(DEPOSIT_OPERATIONS.lesson, 13, 6)
        private val LABS = listOf(THREE_INPUTS, LINES, PROJECT, TRANSFER, OVERLOAD_FOUR, OVERLOAD_EIGHT,
            RECURSIVE_PAIRS, RECURSIVE_GROUPS, REFERENCE_SWAP, REFERENCE_ALIASES, DIAGNOSTIC_INTERVAL, REFACTOR_REPORT,
            NESTED_REVIEW, NESTED_PARITY, DEPOSIT_OPERATIONS, DEPOSIT_BATCH)
    }
}

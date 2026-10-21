package com.learngraphicsprogramming.app

import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.view.View
import android.widget.EditText
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.learngraphicsprogramming.app.runtime.CppCmakePhase
import com.learngraphicsprogramming.app.runtime.CppCmakeResult
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID

/** Real CMake targets at all seven recorded locations; only optional oracles are injected in memory. */
@RunWith(AndroidJUnit4::class)
class CppCmakeTargetOutputCheckDeviceTest : CppOutputCheckQaSupport() {
    private var heartLedger = emptyMap<String, Any?>()

    @Before fun finiteHeartsCannotHideAnAccidentalDeduction() {
        PremiumRepository.setVerifiedFree(context)
        assertFalse(AttemptRepository.getState(context, "programming").unlimited)
        heartLedger = context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all.toMap()
    }

    @Test fun sevenPublishedCmakeLabsCheckBothConsumersAndRestoreTheSelectedTargetWithoutAnOldVerdict() {
        val observations = JSONArray()
        for (location in LOCATIONS) {
            val fixture = practiceFixture(location)
            val expectedFiles = files(fixture.block)
            val originalSources = expectedFiles.filterKeys { it.endsWith(".cpp") || it.endsWith(".h") }
            val repairedCmake = referenceCmake(fixture)
            withPractice(fixture) { host ->
                host.onActivity { activity ->
                    assertEquals(fixture.block.getJSONObject("cmake").getString("target"), target(activity).text.toString())
                    assertSingleRunButton(activity, checking = true)
                    assertEquals("These seven activities declare no stdin", View.GONE, input(activity).visibility)
                }
                // Complete/correct the authored CMake exercise with its later published reference.
                // The engine must still run CMake; no source code or curricular asset is replaced.
                setFile(host, "CMakeLists.txt", repairedCmake)
                completeChildCmake(host, fixture)
                val (battery, client) = expectations(location)
                setTarget(host, battery.first)
                observations.put(execute(host, fixture, battery.first, battery.second, true, "authored_battery"))
                val batterySource = if (location.page == 9) "pruebas/lotes_casos.cpp" else "pruebas/cajas_casos.cpp"
                setFile(host, batterySource,
                    "#include <iostream>\nint main() { std::cout << \"1:2\"; return 0; }\n")
                observations.put(execute(host, fixture, battery.first, client.second, false,
                    "battery_cannot_pass_with_the_other_consumers_output"))
                setFile(host, batterySource, originalSources.getValue(batterySource))
                observations.put(execute(host, fixture, battery.first, battery.second, true,
                    "restored_authored_battery_requires_a_new_execution"))
                setTarget(host, client.first)
                observations.put(execute(host, fixture, client.first, client.second, true, "authored_client"))
                host.onActivity { activity ->
                    setField(activity, "currentPage", location.page - 2)
                    invoke(activity, "renderCurrentPage")
                    setField(activity, "currentPage", location.page - 1)
                    invoke(activity, "renderCurrentPage")
                }
                await(host, "The original CMake page reattaches") { activity ->
                    tree(content(activity)).any { it.tag == "cpp_run" } && runButton(activity).isEnabled
                }
                host.onActivity { activity ->
                    assertEquals("Returning to a cached page cannot recheck its console", View.GONE, result(activity).visibility)
                }
                assertSources(host, originalSources)
            }
            withPractice(fixture) { host ->
                host.onActivity { activity ->
                    assertEquals(expectations(location).last().first, target(activity).text.toString())
                    assertEquals("Reopening retains the target but never a verdict", View.GONE, result(activity).visibility)
                    assertTrue(runButton(activity).isEnabled)
                }
                assertSources(host, originalSources)
                host.onActivity { activity ->
                    tagged(activity, "cpp_file_CMakeLists.txt").performClick()
                    assertEquals(repairedCmake, editor(activity).text.toString())
                }
            }
            unchangedHearts()
        }
        saveEvidence("cmake-seven-locations-twenty-eight-target-checks", JSONObject().put("executions", observations)
            .put("oracles", "Literal outputs recorded by Work in target-dependent-p2.json; no inference from a console or a neighbouring prediction")
            .put("scope", "Seven original lesson pages; QA-only outputCheck plus student edits using the later published CMake reference; authored C++ sources preserved"))
    }

    @Test fun anotherConsumersOutputCannotAccreditTheSelectedTargetAndTechnicalErrorsHaveNoVerdict() {
        val fixture = practiceFixture(LOCATIONS.first())
        val battery = expectations(LOCATIONS.first()).first()
        val client = expectations(LOCATIONS.first()).last()
        val original = files(fixture.block).getValue("pruebas/cajas_casos.cpp")
        val observations = JSONArray()
        withPractice(fixture) { host ->
            setFile(host, "CMakeLists.txt", referenceCmake(fixture))
            completeChildCmake(host, fixture)
            setTarget(host, battery.first)
            observations.put(execute(host, fixture, battery.first, battery.second, true, "original_battery"))
            setTarget(host, client.first)
            setFile(host, "pruebas/cajas_casos.cpp", "#include <iostream>\nint main() { std::cout << \"1:2\"; return 0; }\n")
            setTarget(host, battery.first)
            observations.put(execute(host, fixture, battery.first, client.second, false, "battery_wrongly_prints_client_output"))
            setTarget(host, client.first)
            observations.put(execute(host, fixture, client.first, client.second, true, "client_still_has_its_own_oracle"))
            setFile(host, "pruebas/cajas_casos.cpp", original + "\nesto no es C++;\n")
            setTarget(host, battery.first)
            observations.put(execute(host, fixture, battery.first, null, null, "genuine_build_failure"))
            setFile(host, "pruebas/cajas_casos.cpp", original)
            observations.put(execute(host, fixture, battery.first, battery.second, true, "corrected_source_runs_again"))
            unchangedHearts()
        }
        saveEvidence("cmake-target-mismatch-and-correction", JSONObject().put("executions", observations))
    }

    private data class Location(val lesson: String, val page: Int, val block: Int,
                                val referencePage: Int, val referenceBlock: Int)
    private data class PracticeFixture(val location: Location, val lesson: JSONObject, val block: JSONObject,
                                       val originalId: String, val id: String)
    private fun practiceFixture(location: Location): PracticeFixture {
        val lesson = original(location.lesson)
        val originalId = lesson.getString("id")
        val id = originalId + "__target_output_qa_" + UUID.randomUUID().toString().replace("-", "")
        lesson.put("id", id)
        val block = lesson.getJSONArray("pages").getJSONObject(location.page - 1)
            .getJSONArray("blocks").getJSONObject(location.block - 1)
        assertEquals("cpp_compiler", block.getString("type"))
        block.put("outputCheck", JSONObject().put("cases", JSONArray().apply {
            expectations(location).forEach { (name, expected) -> put(JSONObject().put("target", name)
                .put("standardInput", "").put("outputs", JSONArray().put(expected))) }
        }))
        assertNotNull(CppOutputCheck.fromBlock(block))
        return PracticeFixture(location, lesson, block, originalId, id)
    }
    private fun original(lesson: String) = JSONObject(context.assets.open(
        "content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_$lesson.json")
        .bufferedReader().use { it.readText() })
    private fun referenceCmake(fixture: PracticeFixture) = original(fixture.location.lesson)
        .getJSONArray("pages").getJSONObject(fixture.location.referencePage - 1)
        .getJSONArray("blocks").getJSONObject(fixture.location.referenceBlock - 1).getString("code")
    private fun completeChildCmake(host: ActivityScenario<LessonActivity>, fixture: PracticeFixture) {
        if (fixture.location.lesson != REORGANIZE) return
        val lots = fixture.location.page == 9
        val reference = original(fixture.location.lesson).getJSONArray("pages").getJSONObject(if (lots) 9 else 4)
            .getJSONArray("blocks").getJSONObject(6).getString("code")
        setFile(host, if (lots) "codigo/lotes/CMakeLists.txt" else "componente/CMakeLists.txt", reference)
        host.onActivity { activity -> tagged(activity, "cpp_file_CMakeLists.txt").performClick() }
    }
    private fun files(block: JSONObject): Map<String, String> = block.getJSONArray("projectFiles").let { values ->
        (0 until values.length()).associate { values.getJSONObject(it).let { file -> file.getString("path") to file.getString("initialCode") } }
    }
    private fun withPractice(fixture: PracticeFixture, body: (ActivityScenario<LessonActivity>) -> Unit) {
        ProgressRepository.saveCurrentPage(context, "programming", fixture.originalId, 0)
        val intent = Intent(context, LessonActivity::class.java).putExtra("routeId", "programming")
            .putExtra("courseId", "programming_practice_tools").putExtra("unitId", "comprobaciones_repetibles")
            .putExtra("lessonId", fixture.originalId).putExtra("lessonNumber", 1).putExtra("lessonTotal", 1)
        ActivityScenario.launch<LessonActivity>(intent).use { host ->
            var stableSince = 0L
            var prior = 0
            await(host, "The actual practice-tools lesson is stable before isolated oracle injection") { activity ->
                val loaded = field<JSONObject?>(activity, "lessonJson")
                val ready = loaded?.optString("id") == fixture.originalId && activity.hasWindowFocus() &&
                    content(activity).width > 0 && !content(activity).isLayoutRequested
                val key = System.identityHashCode(activity) xor System.identityHashCode(loaded)
                if (!ready || key != prior) { prior = key; stableSince = if (ready) SystemClock.uptimeMillis() else 0L }
                ready && SystemClock.uptimeMillis() - stableSince > 500L
            }
            host.onActivity { activity ->
                setField(activity, "currentLessonId", fixture.id)
                setField(activity, "lessonJson", JSONObject(fixture.lesson.toString()))
                setField(activity, "currentPage", fixture.location.page - 1)
                setField(activity, "totalPages", fixture.lesson.getJSONArray("pages").length())
                listOf("pageViewCache", "pageRequiredExercises", "pageCompletedExercises").forEach {
                    field<MutableMap<Int, Any>>(activity, it).clear()
                }
                invoke(activity, "renderCurrentPage")
            }
            await(host, "The original compiler and its target control are laid out") { activity ->
                content(activity).width > 0 && !content(activity).isLayoutRequested &&
                    tree(content(activity)).any { it.tag == "cpp_cmake_target" }
            }
            body(host)
        }
    }
    private fun setFile(host: ActivityScenario<LessonActivity>, path: String, text: String) = host.onActivity { activity ->
        tagged(activity, "cpp_file_$path").performClick()
        if (editor(activity).text.toString() != text) editor(activity).setText(text)
        assertEquals(View.GONE, result(activity).visibility)
    }
    private fun setTarget(host: ActivityScenario<LessonActivity>, name: String) = host.onActivity { activity ->
        if (target(activity).text.toString() != name) {
            target(activity).setText(name)
            assertEquals("An executable selection cannot preserve the previous verdict", View.GONE, result(activity).visibility)
        }
        assertTrue(runButton(activity).isEnabled)
    }
    private fun target(activity: LessonActivity) = tagged(activity, "cpp_cmake_target") as EditText
    private fun assertSources(host: ActivityScenario<LessonActivity>, sources: Map<String, String>) = host.onActivity { activity ->
        sources.forEach { (path, code) ->
            tagged(activity, "cpp_file_$path").performClick()
            assertEquals("Original source preserved: $path", code, editor(activity).text.toString())
        }
    }
    private fun execute(host: ActivityScenario<LessonActivity>, fixture: PracticeFixture, expectedTarget: String,
                        expectedOutput: String?, correct: Boolean?, stage: String): JSONObject {
        host.onActivity { activity ->
            assertSingleRunButton(activity, checking = true)
            assertEquals(expectedTarget, target(activity).text.toString())
            assertTrue(runButton(activity).isEnabled)
            runButton(activity).performClick()
            assertFalse(runButton(activity).isEnabled)
            assertEquals(View.GONE, result(activity).visibility)
        }
        await(host, "Real CMake configure/build/run finishes for $expectedTarget: $stage", 180_000L) { activity ->
            runButton(activity).isEnabled && tagged(activity, "cpp_status").getTag(R.id.cpp_lab_cmake_result) is CppCmakeResult
        }
        var evidence = JSONObject()
        host.onActivity { activity ->
            val actual = tagged(activity, "cpp_status").getTag(R.id.cpp_lab_cmake_result) as CppCmakeResult
            assertTrue("CMake generated real Ninja rules", File(actual.workspace, "build/build.ninja").isFile)
            assertTrue("CMake built the selected executable", actual.records.any { record ->
                record.phase == CppCmakePhase.BUILD && "--build" in record.command &&
                    record.command.windowed(2).any { it == listOf("--target", expectedTarget) }
            })
            if (expectedOutput == null) {
                assertEquals(CppCmakePhase.BUILD, actual.phase)
                assertNotEquals(0, actual.exitCode)
                assertNull(actual.execution)
                assertEquals(View.GONE, result(activity).visibility)
            } else {
                assertEquals(CppCmakePhase.EXECUTION, actual.phase)
                assertEquals(0, actual.exitCode)
                assertEquals(expectedOutput, actual.execution!!.stdout)
                assertEquals(View.VISIBLE, result(activity).visibility)
                assertEquals(expectedVerdict(activity, correct == true), result(activity).text.toString())
            }
            unchangedHearts()
            evidence = JSONObject().put("lesson", fixture.originalId).put("page", fixture.location.page)
                .put("block", fixture.location.block).put("target", expectedTarget).put("stage", stage)
                .put("phase", actual.phase.name).put("exitCode", actual.exitCode)
                .put("stdout", actual.execution?.stdout ?: JSONObject.NULL).put("verdict", result(activity).text.toString())
                .put("projectFingerprint", actual.projectFingerprint).put("records", JSONArray().apply {
                    actual.records.forEach { record -> put(JSONObject().put("phase", record.phase.name)
                        .put("command", JSONArray(record.command)).put("stdout", record.stdout).put("stderr", record.stderr)) }
                })
        }
        return evidence
    }
    private fun unchangedHearts() = assertEquals("Output checks never deduct finite hearts", heartLedger,
        context.getSharedPreferences("attempt_reserve_seconds_test", Context.MODE_PRIVATE).all)
    private fun expectations(location: Location): List<Pair<String, String>> = if (location.page == 9)
        listOf("verificar_lotes" to LOTES_OUTPUT, "consultar_lotes" to "1:2") else
        listOf("comprobar_cajas" to CAJAS_OUTPUT, "consultar_cajas" to "1:2")

    companion object {
        private const val LISTS = "datos_y_listas_de_objetivos"
        private const val REORGANIZE = "reorganizar_la_descripcion_conservando_el_comprobador"
        private val LOCATIONS = listOf(Location(LISTS, 4, 5, 5, 2), Location(LISTS, 5, 8, 5, 2),
            Location(LISTS, 7, 5, 8, 2), Location(LISTS, 9, 5, 10, 2), Location(REORGANIZE, 4, 5, 5, 5),
            Location(REORGANIZE, 7, 4, 5, 5), Location(REORGANIZE, 9, 4, 10, 5))
        // Exact Work-verified oracles from target-dependent-p2.json, not inferred at runtime.
        private const val CAJAS_OUTPUT = "piezas=0 esperado=0 obtenido=0 OK\npiezas=4 esperado=1 obtenido=1 OK\npiezas=8 esperado=2 obtenido=2 OK\npiezas=20 esperado=5 obtenido=5 OK\npiezas=1 esperado=1 obtenido=1 OK\npiezas=5 esperado=2 obtenido=2 OK\npiezas=17 esperado=5 obtenido=5 OK\nBateria: OK\n"
        private const val LOTES_OUTPUT = "piezas=0 esperado=0 obtenido=0 OK\npiezas=6 esperado=1 obtenido=1 OK\npiezas=7 esperado=2 obtenido=2 OK\npiezas=18 esperado=3 obtenido=3 OK\nBateria: OK\n"
    }
}

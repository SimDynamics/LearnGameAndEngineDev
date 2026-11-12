package com.learngraphicsprogramming.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** Historical U11 C++ compiler extensions are independent of source formatting. */
class PublishedCompilerRevisionAndroidTest {
    private val lessonNames = listOf(
        "construir_un_lector_de_entrada_preparada",
        "diagnosticar_y_recuperar_hasta_fin_de_operacion",
        "construir_un_interprete_con_historial_y_pruebas"
    )
    private fun document(name: String): JSONObject {
        val relative = "src/main/assets/content/es/programming/cpp_fundamentals/" +
            "texto_interpretacion_y_flujos/lesson_$name.json"
        val file = listOf(File(relative), File("app/$relative")).firstOrNull(File::isFile)
            ?: error("Missing published compiler history: $name")
        return JSONObject(file.readText(Charsets.UTF_8))
    }
    private fun find(lesson: JSONObject, identity: String): Pair<Int, Int> {
        val pages = lesson.getJSONObject("stateLayout").getJSONArray("current")
        for (page in 0 until pages.length()) {
            val ids = pages.getJSONObject(page).getJSONArray("blocks")
            for (block in 0 until ids.length()) if (ids.getString(block) == identity)
                return page to block
        }
        error("Missing compiler revision owner: $identity")
    }
    private fun block(lesson: JSONObject, identity: String): JSONObject {
        val (p,b) = find(lesson, identity)
        return lesson.getJSONArray("pages").getJSONObject(p).getJSONArray("blocks").getJSONObject(b)
    }

    @Test fun reviewedCompilerAdditionsAndTheirReferenceProjectionsPreserveOldAddresses() {
        for (name in lessonNames) {
            val lesson = document(name)
            val layout = requireNotNull(LessonStateLayoutJson.read(lesson))
            val revisions = lesson.getJSONObject("stateLayout").getJSONArray("compilerRevisions")
            assertTrue(revisions.length() >= 1)
            for (i in 0 until revisions.length()) {
                val entry = revisions.getJSONObject(i)
                val id = entry.getString("id")
                val historical = entry.getJSONObject("block")
                val expanded = block(lesson, id)
                assertEquals(LessonStateLayoutJson.stateFingerprint(historical),
                    LessonStateLayoutJson.stateFingerprint(
                        CurricularCompilerRevision.projectCompiler(expanded, historical)))
                val (p,b) = find(lesson, id)
                assertEquals(LessonStatePosition(p,b), layout.storagePosition(p,b))
                val references = entry.getJSONArray("references")
                for (index in 0 until references.length()) {
                    val reference = references.getJSONObject(index)
                    val refId = reference.getString("id")
                    val previous = reference.getJSONObject("block")
                    val actual = block(lesson, refId)
                    assertEquals(LessonStateLayoutJson.stateFingerprint(previous),
                        LessonStateLayoutJson.stateFingerprint(
                            CurricularCompilerRevision.projectReference(actual, previous)))
                    val (rp, rb) = find(lesson, refId)
                    assertEquals(LessonStatePosition(rp, rb), layout.storagePosition(rp, rb))
                }
            }
        }
    }

    @Test fun forgedRuntimeOutputAndHistoricalReferenceDoNotReceiveCredit() {
        for (name in lessonNames) {
            val original = document(name)
            val revisions = original.getJSONObject("stateLayout").getJSONArray("compilerRevisions")
            val id = revisions.getJSONObject(0).getString("id")
            val tampering: List<(JSONObject) -> Unit> = listOf(
                { it.getJSONObject("stateLayout").remove("compilerRevisions") },
                { block(it, id).getJSONObject("outputCheck").getJSONArray("cases")
                    .getJSONObject(0).getJSONArray("outputs").put(0, "forged expected output") },
                { it.getJSONObject("stateLayout").getJSONArray("compilerRevisions")
                    .getJSONObject(0).getJSONObject("block").put("initialCode", "int main(){return 9;}") },
                { val references = it.getJSONObject("stateLayout").getJSONArray("compilerRevisions")
                    .getJSONObject(0).getJSONArray("references")
                    if (references.length() > 0) {
                        val refId = references.getJSONObject(0).getString("id")
                        block(it, refId).put("code", "int main(){return 9;}")
                    } }
            )
            for (attack in tampering) {
                val forged = JSONObject(original.toString())
                attack(forged)
                assertTrue("Altered reviewed compiler was accepted: $name",
                    runCatching { LessonStateLayoutJson.read(forged) }.exceptionOrNull()
                        is LessonStateLayoutException)
            }
        }
    }

    @Test fun safePhysicalLineSpacingCanJoinTheSameLessonAsCompilerAndChoiceArchives() {
        val published = document(lessonNames.first())
        val oldLayout = requireNotNull(LessonStateLayoutJson.read(published))
        val revised = JSONObject(published.toString())
        val metadata = revised.getJSONObject("stateLayout")
        val formattedId = "block_0000_0002"
        val previous = JSONObject(block(revised, formattedId).toString())
        val current = block(revised, formattedId)
        current.put("code", previous.getString("code") + "\n")
        metadata.put("sourceFormatRevisions", JSONArray().put(
            JSONObject().put("id", formattedId).put("block", previous)))
        metadata.getJSONArray("editorial").put(JSONObject()
            .put("id", formattedId)
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(previous))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(current)))
        val newLayout = requireNotNull(LessonStateLayoutJson.read(revised))
        assertEquals(oldLayout.lineage, newLayout.lineage)
        assertEquals(oldLayout.legacyLineage, newLayout.legacyLineage)
        assertEquals(LessonStateLayoutJson.stateFingerprint(previous),
            LessonStateLayoutJson.stateFingerprint(
                CurricularSourceFormatCompat.project(current, previous)))
    }
}

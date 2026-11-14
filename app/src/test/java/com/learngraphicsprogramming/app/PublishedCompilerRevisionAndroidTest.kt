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
    private fun originalPosition(lesson: JSONObject, identity: String): LessonStatePosition {
        val origin = lesson.getJSONObject("stateLayout").getJSONArray("origin")
        for (page in 0 until origin.length()) {
            val blocks = origin.getJSONObject(page).getJSONArray("blocks")
            for (index in 0 until blocks.length()) {
                if (blocks.getJSONObject(index).getString("id") == identity)
                    return LessonStatePosition(page, index)
            }
        }
        error("Missing historical storage position: $identity")
    }

    // The active compiler/reference may have a later, independently published
    // physical formatting revision. Restore *that* authenticated source first,
    // exactly as the lesson state loader does, before reviewing old exercises.
    private fun beforePhysicalFormatting(
        lesson: JSONObject, identity: String, current: JSONObject
    ): JSONObject {
        val records = lesson.getJSONObject("stateLayout")
            .optJSONArray("sourceFormatRevisions") ?: return current
        for (index in 0 until records.length()) {
            val entry = records.getJSONObject(index)
            if (entry.getString("id") == identity)
                return CurricularSourceFormatCompat.project(current, entry.getJSONObject("block"))
        }
        return current
    }

    @Test fun reviewedCompilerAdditionsAndTheirReferenceProjectionsPreserveOldAddresses() {
        for (name in lessonNames) {
            val lesson = document(name)
            val layout = try {
                requireNotNull(LessonStateLayoutJson.read(lesson))
            } catch (error: LessonStateLayoutException) {
                throw AssertionError("Compiler history in $name: " +
                    (error.cause?.message ?: error.message), error)
            }
            val revisions = lesson.getJSONObject("stateLayout").getJSONArray("compilerRevisions")
            assertTrue(revisions.length() >= 1)
            for (i in 0 until revisions.length()) {
                val entry = revisions.getJSONObject(i)
                val id = entry.getString("id")
                val historical = entry.getJSONObject("block")
                val expanded = block(lesson, id)
                assertEquals(LessonStateLayoutJson.stateFingerprint(historical),
                    LessonStateLayoutJson.stateFingerprint(
                        CurricularCompilerRevision.projectCompiler(
                            beforePhysicalFormatting(lesson, id, expanded), historical)))
                val (p,b) = find(lesson, id)
                assertEquals(originalPosition(lesson, id), layout.storagePosition(p,b))
                val references = entry.optJSONArray("references") ?: JSONArray()
                for (index in 0 until references.length()) {
                    val reference = references.getJSONObject(index)
                    val refId = reference.getString("id")
                    val previous = reference.getJSONObject("block")
                    val actual = block(lesson, refId)
                    assertEquals(LessonStateLayoutJson.stateFingerprint(previous),
                        LessonStateLayoutJson.stateFingerprint(
                            CurricularCompilerRevision.projectReference(
                                beforePhysicalFormatting(lesson, refId, actual), previous)))
                    val (rp, rb) = find(lesson, refId)
                    assertEquals(originalPosition(lesson, refId), layout.storagePosition(rp, rb))
                }
            }
        }
    }

    @Test fun reviewedCompilerCanAlsoReceiveAuthenticatedPhysicalNewlines() {
        val published = document(lessonNames.first())
        val originalLayout = requireNotNull(LessonStateLayoutJson.read(published))
        val formattedLesson = JSONObject(published.toString())
        val metadata = formattedLesson.getJSONObject("stateLayout")
        val owner = metadata.getJSONArray("compilerRevisions")
            .getJSONObject(0).getString("id")
        // Work published this physical source archive already. Keep every
        // original archive and add only an honest later editorial edge.
        val archives = metadata.getJSONArray("sourceFormatRevisions")
        val sourceBeforeFormatting = (0 until archives.length())
            .map { archives.getJSONObject(it) }
            .single { it.getString("id") == owner }
            .getJSONObject("block")
        val previous = JSONObject(block(formattedLesson, owner).toString())
        val active = block(formattedLesson, owner)
        val priorSource = previous.getString("initialCode")
        assertFalse(priorSource.contains("\\\n"))
        assertNotEquals(sourceBeforeFormatting.getString("initialCode"), priorSource)
        active.put("initialCode", priorSource + "\n")
        metadata.getJSONArray("editorial").put(JSONObject()
            .put("id", owner)
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(previous))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(active)))
        val reopened = requireNotNull(LessonStateLayoutJson.read(formattedLesson))
        assertEquals(originalLayout.lineage, reopened.lineage)
        assertEquals(originalLayout.legacyLineage, reopened.legacyLineage)
        assertEquals(sourceBeforeFormatting.getString("initialCode"),
            CurricularSourceFormatCompat.project(active, sourceBeforeFormatting)
                .getString("initialCode"))
        val altered = JSONObject(formattedLesson.toString())
        block(altered, owner).put("initialCode", "int main() { return 99; }")
        assertTrue(runCatching { LessonStateLayoutJson.read(altered) }.exceptionOrNull()
            is LessonStateLayoutException)
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
                    .getJSONObject(0).optJSONArray("references")
                    if (references != null && references.length() > 0) {
                        val refId = references.getJSONObject(0).getString("id")
                        block(it, refId).put("code", "int main(){return 9;}")
                    } else {
                        block(it, id).put("initialCode", "int main(){return 9;}")
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
        val references = mutableSetOf<String>()
        val compilerRevisions = metadata.getJSONArray("compilerRevisions")
        for (index in 0 until compilerRevisions.length()) {
            val entries = compilerRevisions.getJSONObject(index)
                .optJSONArray("references") ?: JSONArray()
            for (n in 0 until entries.length())
                references.add(entries.getJSONObject(n).getString("id"))
        }
        // All the independent source examples in this published lesson have
        // already been formatted by Work. Reuse a genuine archived predecessor
        // instead of forging a second copy of the same source-format record.
        val archived = metadata.getJSONArray("sourceFormatRevisions")
        val sourceEntry = (0 until archived.length())
            .map { archived.getJSONObject(it) }
            .first { entry ->
                val id = entry.getString("id")
                id !in references && block(revised, id).optString("type") == "code" &&
                    !block(revised, id).getString("code").contains("__LINE__")
            }
        val selected = sourceEntry.getString("id")
        val original = sourceEntry.getJSONObject("block")
        val previous = JSONObject(block(revised, selected).toString())
        val current = block(revised, selected)
        assertNotEquals(original.getString("code"), previous.getString("code"))
        current.put("code", previous.getString("code") + "\n")
        metadata.getJSONArray("editorial").put(JSONObject()
            .put("id", selected)
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(previous))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(current)))
        val newLayout = requireNotNull(LessonStateLayoutJson.read(revised))
        assertEquals(oldLayout.lineage, newLayout.lineage)
        assertEquals(oldLayout.legacyLineage, newLayout.legacyLineage)
        assertEquals(original.getString("code"),
            CurricularSourceFormatCompat.project(current, original).getString("code"))
    }
}

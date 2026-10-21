package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.LessonStateLayoutJson
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

/** Current curricular documents are read directly; no historical copies are shipped. */
class LocalizedNarrationStableResumeTest {
    private val lessonId = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida"
    private val asset = "programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json"
    private val selected = NarrationSettings(engine = NarrationEngine.KOKORO,
        language = NarrationLanguage.SPANISH, kokoroVoice = NarrationLanguage.SPANISH.defaultVoice)
    private fun family() = listOf("es", "en", "fr").associateWith {
        JSONObject(File("src/main/assets/content/$it/$asset").readText())
    }
    private fun pageId(p: Int) = "page_%04d".format(java.util.Locale.ROOT, p)
    private fun blockId(p: Int, b: Int) = "block_%04d_%04d".format(java.util.Locale.ROOT, p, b)
    private fun manifest(source: JSONObject) = JSONArray().also { origin ->
        val pages = source.getJSONArray("pages")
        repeat(pages.length()) { p ->
            val blocks = pages.getJSONObject(p).getJSONArray("blocks")
            origin.put(JSONObject().put("id", pageId(p)).put("blocks", JSONArray().also { records ->
                repeat(blocks.length()) { b -> records.put(JSONObject().put("id", blockId(p, b))
                    .put("sha256", LessonStateLayoutJson.stateFingerprint(blocks.getJSONObject(b)))) }
            }))
        }
    }
    private fun mapped(family: Map<String, JSONObject>, edit: Boolean = true,
                       swap: Boolean = false, newSpeech: Boolean = false): JSONObject {
        val original = family.getValue("es")
        val result = JSONObject(original.toString())
        val pages = result.getJSONArray("pages")
        val origin = manifest(original)
        val current = JSONArray().also { records ->
            repeat(origin.length()) { p -> records.put(JSONObject().put("id", pageId(p)).put("blocks", JSONArray().also { ids ->
                repeat(origin.getJSONObject(p).getJSONArray("blocks").length()) { b -> ids.put(blockId(p, b)) }
            })) }
        }
        val old = pages.getJSONObject(4).getJSONArray("blocks")
        val before = old.getJSONObject(1)
        val paragraph = JSONObject(before.toString())
        if (edit) paragraph.put("text", paragraph.getString("text") + " Conserva todo lo demás.")
        val added = JSONObject(old.getJSONObject(2).toString()).apply { remove("outputCheck") }
        val blocks = JSONArray().put(old.getJSONObject(0)).put(old.getJSONObject(3)).put(paragraph)
            .put(added).put(old.getJSONObject(2)).put(old.getJSONObject(4))
        val ids = JSONArray(listOf(blockId(4, 0), blockId(4, 3), blockId(4, 1),
            "qa_original_execution", blockId(4, 2), blockId(4, 4)))
        val additions = JSONArray().put(JSONObject().put("id", "qa_original_execution")
            .put("sha256", LessonStateLayoutJson.stateFingerprint(added))
            .put("storage", JSONObject().put("page", 6).put("block", 0)).put("progressPage", pageId(4)))
        if (newSpeech) {
            val extra = JSONObject().put("type", "paragraph").put("text", "Otra instrucción que todavía debe leerse.")
            blocks.put(extra); ids.put("qa_new_instruction")
            additions.put(JSONObject().put("id", "qa_new_instruction")
                .put("sha256", LessonStateLayoutJson.stateFingerprint(extra))
                .put("storage", JSONObject().put("page", 6).put("block", 1)).put("progressPage", pageId(4)))
        }
        pages.getJSONObject(4).put("blocks", blocks)
        current.getJSONObject(4).put("blocks", ids)
        if (swap) {
            val a = pages.getJSONObject(4); val b = pages.getJSONObject(5)
            pages.put(4, b); pages.put(5, a)
            val ma = current.getJSONObject(4); val mb = current.getJSONObject(5)
            current.put(4, mb); current.put(5, ma)
        }
        val relatives = JSONObject().also { data -> family.forEach { (locale, source) ->
            data.put(locale, JSONObject().put("sha256", LessonStateLayoutJson.fingerprint(source))
                .put("origin", manifest(source)))
        } }
        val editorial = JSONArray()
        if (edit) editorial.put(JSONObject().put("id", blockId(4, 1))
            .put("beforeSha256", LessonStateLayoutJson.fingerprint(before))
            .put("afterSha256", LessonStateLayoutJson.fingerprint(paragraph)))
        return result.put("stateLayout", JSONObject().put("version", 2).put("locale", "es")
            .put("origin", origin).put("current", current).put("family", relatives)
            .put("additions", additions).put("editorial", editorial))
    }
    private fun script(source: JSONObject, p: Int) =
        LessonNarrationScript.page(source, p, selected.language, forKokoro = true)
    private fun page(source: JSONObject, p: Int) = requireNotNull(NarrationStableLesson.page(
        requireNotNull(NarrationStableLesson.layout(source)), "programming", lessonId, p, script(source, p), selected))
    private fun rawKey(source: JSONObject, p: Int) = NarrationResumePolicy.key("programming/$lessonId/$p",
        selected.language.tag, selected.engine.name, selected.kokoroVoice,
        NarrationSegments.sentences(script(source, p).texts))
    private fun history(key: String, point: NarrationResumePoint, stable: NarrationStablePage? = null): NarrationResumeHistory {
        var result = NarrationResumePolicy.begin(NarrationResumeHistory(), "older", "older-owner", NarrationResumePoint())
        result = NarrationResumePolicy.begin(result, key, "source-owner", point, stable)
        return NarrationResumePolicy.begin(result, "newer", "newer-owner", NarrationResumePoint())
    }
    private fun legacy(source: JSONObject, wanted: String) = NarrationStableLesson.legacy(source,
        requireNotNull(NarrationStableLesson.layout(source)), "programming", lessonId, selected,
        setOf(wanted), true, { "" }, { _, _, block -> block })

    @Test fun unchangedRealPageFollowsItsBlocksWithoutAnExtraBookmarkOrTranslatedCopies() {
        val family = family()
        val beforeDocuments = family.mapValues { it.value.toString() }
        val original = family.getValue("es")
        val current = mapped(family, swap = true)
        val key = rawKey(original, 5)
        val target = page(current, 4)
        val anchors = legacy(current, key)
        assertTrue(anchors.containsKey(key))
        val point = NarrationResumePoint(1, 537)
        val expected = requireNotNull(NarrationStableResume.remap(point, anchors.getValue(key), target.stable))
        val moved = NarrationStableResume.prepare(history(key, point), target.key, target.stable, anchors)
        assertEquals(listOf("newer", target.key, "older"), moved.entries.map { it.key })
        assertEquals(expected, moved.entries[1].point)
        assertEquals(beforeDocuments, family.mapValues { it.value.toString() })
    }

    @Test fun changedLegacyFullPageAudioKeepsItsOldPositionRatherThanInventingNewMilliseconds() {
        val family = family()
        val oldKey = rawKey(family.getValue("es"), 4)
        val changed = mapped(family, swap = true)
        val target = page(changed, 5)
        val anchors = legacy(changed, oldKey)
        assertFalse(anchors.containsKey(oldKey))
        val previous = history(oldKey, NarrationResumePoint(2, 537))
        assertEquals(previous, NarrationStableResume.prepare(previous, target.key, target.stable, anchors))
    }

    @Test fun anExistingV2AnchorCanFollowUnchangedAudioAfterAnotherMoveAndEditorialChange() {
        val family = family()
        val before = page(mapped(family, edit = false), 4)
        val after = page(mapped(family, edit = true, swap = true), 5)
        val index = before.stable.parts.indexOfFirst { it.block == blockId(4, 3) && !it.feedback }
        assertTrue(index >= 0)
        val previous = history(before.key, NarrationResumePoint(index, 537), before.stable)
        val moved = NarrationStableResume.prepare(previous, after.key, after.stable)
        assertEquals(3, moved.entries.size)
        assertEquals(after.key, moved.entries[1].key)
        assertEquals(537L, moved.entries[1].point.positionMs)
        assertEquals(before.stable.parts[index], after.stable.parts[moved.entries[1].point.segment])
    }

    @Test fun aNewSpokenInstructionNeverInheritsTheFinishedFlagOrFeedbackCredit() {
        val family = family()
        val before = page(mapped(family), 4)
        val after = page(mapped(family, newSpeech = true), 4)
        val owner = "finished-owner"
        var previous = NarrationResumePolicy.begin(NarrationResumeHistory(), before.key, owner, NarrationResumePoint(), before.stable)
        previous = NarrationResumePolicy.update(previous, before.key, owner,
            NarrationResumePoint(before.stable.parts.lastIndex, 537, finished = true))
        val moved = NarrationStableResume.prepare(previous, after.key, after.stable)
        assertEquals(1, moved.entries.size)
        assertEquals(after.key, moved.entries.single().key)
        assertFalse(moved.entries.single().point.finished)
        assertEquals(0, moved.entries.single().point.feedbackHeardThrough)
    }
}

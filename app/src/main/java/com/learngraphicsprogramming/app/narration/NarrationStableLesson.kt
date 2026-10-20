package com.learngraphicsprogramming.app.narration

import com.learngraphicsprogramming.app.LessonStateLayoutJson
import com.learngraphicsprogramming.app.LessonStatePage
import com.learngraphicsprogramming.app.StableLessonStateLayout
import com.learngraphicsprogramming.app.math.MathNarrationPresentation
import org.json.JSONArray
import org.json.JSONObject
import java.security.MessageDigest
import java.util.WeakHashMap

/** Reconstruct the legacy order from authored immutable slots; no old APK is required. */
internal object NarrationStableLesson {
    data class Page(val key: String, val stable: NarrationStablePage, val texts: List<String>)
    private val identities = WeakHashMap<StableLessonStateLayout, MutableMap<Int, String>>()

    /** Equal wording in different activities must not share a playback identity. */
    @Synchronized
    fun identity(layout: StableLessonStateLayout, route: String, lessonId: String, pageIndex: Int): String {
        val digest = identities.getOrPut(layout) { mutableMapOf() }.getOrPut(pageIndex) {
            val record = layout.current[pageIndex]
            val order = (listOf(record.id) + record.blocks).joinToString(",")
            MessageDigest.getInstance("SHA-256").digest(order.toByteArray(Charsets.UTF_8))
                .joinToString("") { "%02x".format(it) }
        }
        return "$route/$lessonId/state-layout-v1/${layout.lineage}/$digest"
    }

    fun page(layout: StableLessonStateLayout, route: String,
             lessonId: String, pageIndex: Int, script: LessonNarrationScript,
             selected: NarrationSettings): Page? {
        val record = layout.current.getOrNull(pageIndex) ?: return null
        return page("$route/$lessonId/${layout.lineage}", identity(layout, route, lessonId, pageIndex),
            record, script, selected)
    }

    private fun page(scope: String, identity: String, page: LessonStatePage,
                     script: LessonNarrationScript, selected: NarrationSettings): Page? {
        if (script.origins.size != script.texts.size) return null
        val texts = split(script.texts, selected.engine)
        val expanded = mutableListOf<String>()
        val parts = mutableListOf<NarrationStablePart>()
        val occurrence = mutableMapOf<Triple<String, Boolean, String>, Int>()
        script.texts.forEachIndexed { index, text ->
            val origin = script.origins[index]
            val block = if (origin.blockIndex == -1 && !origin.feedback) "#title"
                else page.blocks.getOrNull(origin.blockIndex) ?: return null
            split(listOf(text), selected.engine).forEach { sentence ->
                expanded += sentence
                val audio = key("stable-audio-v1", selected, listOf(sentence))
                val token = Triple(block, origin.feedback, audio)
                val ordinal = occurrence[token] ?: 0
                occurrence[token] = ordinal + 1
                parts += NarrationStablePart(block, origin.feedback, audio, ordinal)
            }
        }
        if (expanded != texts) return null
        val stable = NarrationStablePage(scope, key("stable-configuration-v1", selected, emptyList()), parts)
        return if (stable.valid()) Page(key(identity, selected, texts), stable, texts) else null
    }

    fun layout(lesson: JSONObject): StableLessonStateLayout? = LessonStateLayoutJson.read(lesson)

    /**
     * Only exact legacy playback hashes are candidates. The same reconstructable
     * block IDs then survive subsequent releases without storing lesson prose.
     */
    fun legacy(lesson: JSONObject, layout: StableLessonStateLayout, route: String,
               lessonId: String, selected: NarrationSettings, wantedKeys: Set<String>,
               forKokoro: Boolean, instruction: (String) -> String,
               presentation: (Int, Int, JSONObject) -> JSONObject): Map<String, NarrationStablePage> {
        if (wantedKeys.isEmpty()) return emptyMap()
        val pages = lesson.getJSONArray("pages")
        val shells = layout.current.mapIndexed { p, record -> record.id to pages.getJSONObject(p) }.toMap()
        val blocks = layout.current.flatMapIndexed { p, record -> record.blocks.mapIndexed { b, id ->
            id to Triple(p, b, pages.getJSONObject(p).getJSONArray("blocks").getJSONObject(b))
        }}.toMap()
        val originalPages = JSONArray()
        layout.origin.forEach { record ->
            val shell = JSONObject(shells.getValue(record.id).toString())
            val originalBlocks = JSONArray()
            record.blocks.forEach { originalBlocks.put(blocks.getValue(it).third) }
            originalPages.put(shell.put("blocks", originalBlocks))
        }
        val original = JSONObject(lesson.toString()).put("pages", originalPages).apply { remove("stateLayout") }
        val result = linkedMapOf<String, NarrationStablePage>()
        val ambiguous = mutableSetOf<String>()
        layout.origin.forEachIndexed { p, record ->
            fun presented(b: Int, block: JSONObject): JSONObject {
                val current = blocks.getValue(record.blocks[b])
                return presentation(current.first, current.second, block)
            }
            val variants = mutableListOf<LessonNarrationScript>()
            variants += LessonNarrationScript.presentedPage(original, p, selected.language, forKokoro,
                instruction, ::presented)
            if (LessonNarrationScript.hasJoinedTableRows(original, p)) {
                variants += LessonNarrationScript.presentedPageVersioned(original, p, selected.language,
                    forKokoro, instruction, completeVisibleLabels = true, separateTableCells = false,
                    presentation = ::presented)
            }
            variants += LessonNarrationScript.presentedPageVersioned(original, p, selected.language,
                forKokoro, instruction, completeVisibleLabels = false, presentation = ::presented)
            variants += LessonNarrationScript.presentedPageVersioned(original, p, selected.language,
                forKokoro, { type -> if (type in setOf("order_code", "drag_fill_code")) instruction(type) else "" },
                completeVisibleLabels = false, presentation = { _, block -> MathNarrationPresentation.legacyBlock(block) })
            for (script in variants) {
                val candidate = page("$route/$lessonId/${layout.lineage}", "$route/$lessonId/$p", record,
                    script, selected) ?: continue
                if (candidate.key in wantedKeys && candidate.key !in ambiguous) {
                    val known = result[candidate.key]
                    if (known != null && known != candidate.stable) {
                        // Identical historical audio with different block provenance
                        // cannot be resolved by guessing an extractor version.
                        result.remove(candidate.key)
                        ambiguous += candidate.key
                    } else result[candidate.key] = candidate.stable
                }
            }
            if (result.keys.containsAll(wantedKeys)) return result
        }
        return result
    }

    private fun split(texts: List<String>, engine: NarrationEngine): List<String> =
        if (engine == NarrationEngine.ANDROID) NarrationSegments.prepare(texts, 250)
        else NarrationSegments.sentences(texts)

    private fun key(identity: String, selected: NarrationSettings, texts: List<String>): String =
        NarrationResumePolicy.key(identity, selected.language.tag, selected.engine.name,
            if (selected.engine == NarrationEngine.ANDROID) selected.androidVoice.orEmpty() else selected.kokoroVoice,
            texts)
}

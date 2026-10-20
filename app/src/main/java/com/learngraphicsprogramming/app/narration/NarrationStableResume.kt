package com.learngraphicsprogramming.app.narration

import org.json.JSONArray
import org.json.JSONObject

/** Stable provenance inside one of the existing three bookmarks; never an extra history. */
internal data class NarrationStablePart(
    val block: String, val feedback: Boolean, val audio: String, val occurrence: Int
)

internal data class NarrationStablePage(
    val scope: String, val configuration: String, val parts: List<NarrationStablePart>
) {
    fun valid(): Boolean = scope.isNotBlank() && scope.length <= 1024 &&
        HASH.matches(configuration) && parts.isNotEmpty() && parts.size <= 20_000 &&
        parts.distinct().size == parts.size && parts.all {
            (it.block == "#title" || ID.matches(it.block)) &&
                HASH.matches(it.audio) && it.occurrence in 0..20_000 &&
                (!it.feedback || it.block != "#title")
        }
    private companion object {
        val HASH = Regex("[0-9a-f]{64}")
        val ID = Regex("[A-Za-z0-9_-]{1,128}")
    }
}

/**
 * A pause follows its exact block/recording even when the containing page changes.
 * A matching sentence in another block, lesson, voice or language cannot claim it.
 */
internal object NarrationStableResume {
    fun remap(point: NarrationResumePoint, before: NarrationStablePage,
              after: NarrationStablePage): NarrationResumePoint? {
        if (!before.valid() || !after.valid() || before.scope != after.scope ||
            before.configuration != after.configuration || point.positionMs < 0 ||
            point.segment !in before.parts.indices || point.feedbackHeardThrough < 0) return null
        val source = before.parts[point.segment]
        val destination = after.parts.indexOf(source)
        // A changed recording has no trustworthy millisecond correspondence. Keep
        // its old bookmark instead of transferring its time to a different passage.
        if (destination < 0) return null
        val previouslyHeard = before.parts.take(point.feedbackHeardThrough).filter { it.feedback }.toSet()
        var heard = 0
        for (index in after.parts.indices) {
            val part = after.parts[index]
            if (!part.feedback) continue
            if (part !in previouslyHeard) break
            heard = index + 1
        }
        // The lesson heading is presentation shared by the whole lesson. New body
        // material must never inherit the finished flag of a different page.
        val previousParts = before.parts.toSet()
        val sameBody = after.parts.filter { it.block != "#title" }.all { it in previousParts }
        return point.copy(segment = destination,
            finished = point.finished && point.segment == before.parts.lastIndex &&
                destination == after.parts.lastIndex && sameBody,
            feedbackHeardThrough = heard)
    }

    /** Adoption is silent, keeps recency, and never evicts an unrelated bookmark. */
    fun prepare(history: NarrationResumeHistory, key: String, current: NarrationStablePage,
                legacy: Map<String, NarrationStablePage> = emptyMap()): NarrationResumeHistory {
        if (key.isBlank() || !current.valid()) return history
        val attached = history.copy(entries = history.entries.map { entry ->
            if (entry.stable != null) entry else legacy[entry.key]?.takeIf { it.valid() }?.let {
                entry.copy(stable = it)
            } ?: entry
        })
        val existing = attached.entries.firstOrNull { it.key == key }
        if (existing != null) {
            // Its exact full-page playback hash already identifies the current
            // recording. Only add provenance; never replace its newer position.
            return attached.copy(entries = attached.entries.map {
                if (it.key == key) it.copy(stable = current) else it
            })
        }
        val candidate = attached.entries.firstNotNullOfOrNull { entry ->
            entry.stable?.let { previous -> remap(entry.point, previous, current)?.let { entry to it } }
        } ?: return attached
        val (source, point) = candidate
        return attached.copy(entries = attached.entries.map {
            if (it.key == source.key) NarrationResumeEntry(key, point, current) else it
        }, activeKey = if (attached.activeKey == source.key) key else attached.activeKey)
    }
}

/** Additive metadata: malformed provenance never deletes the original positional bookmark. */
internal object NarrationStablePageJson {
    fun encode(page: NarrationStablePage): String {
        require(page.valid())
        val parts = JSONArray()
        page.parts.forEach { parts.put(JSONArray().put(it.block).put(it.feedback).put(it.audio).put(it.occurrence)) }
        return JSONObject().put("version", 1).put("scope", page.scope)
            .put("configuration", page.configuration).put("parts", parts).toString()
    }

    fun decode(value: String?): NarrationStablePage? = runCatching {
        if (value == null || value.length > 4_000_000) return@runCatching null
        val json = JSONObject(value)
        require(json.keys().asSequence().toSet() == setOf("version", "scope", "configuration", "parts"))
        require(json.get("version") == 1)
        val array = json.getJSONArray("parts")
        require(array.length() in 1..20_000)
        val page = NarrationStablePage(json.get("scope") as String, json.get("configuration") as String,
            (0 until array.length()).map { index ->
                val part = array.getJSONArray(index)
                require(part.length() == 4)
                NarrationStablePart(part.get(0) as String, part.get(1) as Boolean,
                    part.get(2) as String, part.get(3) as Int)
            })
        page.takeIf { it.valid() }
    }.getOrNull()
}

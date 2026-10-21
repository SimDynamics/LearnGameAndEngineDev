package com.learngraphicsprogramming.app

import android.content.Context
import android.content.SharedPreferences
import android.content.res.AssetManager
import org.json.JSONObject
import java.io.FileNotFoundException
import java.util.WeakHashMap

/**
 * Translation from the displayed position to a stable storage slot.
 * No answer, source, input, project or heart balance is copied or deleted here.
 */
internal object LessonStateCoordinates {
    private const val PREFS = "learning_progress"
    private data class Key(val language: String, val lesson: String)
    private data class Resolved(val layout: StableLessonStateLayout?, val language: String)
    private val layouts = WeakHashMap<AssetManager, MutableMap<Key, Resolved>>()

    @Synchronized
    fun register(context: Context, lessonId: String, json: JSONObject,
                 sourceLanguage: String = CurriculumAssetLocale.current(context)) {
        if (json.has("stateLayout") && json.optString("id") != lessonId) {
            throw LessonStateLayoutException("State map belongs to another lesson")
        }
        val layout = LessonStateLayoutJson.read(json)
        if (layout?.version == 2 && layout.locale != sourceLanguage) {
            throw LessonStateLayoutException("State map belongs to another asset language")
        }
        checkAndSeed(context, lessonId, layout, sourceLanguage)
        layouts.getOrPut(context.assets) { mutableMapOf() }[key(context, lessonId)] = Resolved(layout, sourceLanguage)
    }

    private fun key(context: Context, lessonId: String) = Key(CurriculumAssetLocale.current(context), lessonId)
    private fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private fun lineageKey(lessonId: String, layout: StableLessonStateLayout? = null) =
        "lesson_${lessonId}_state_lineage_" + suffix(layout)
    private fun pagesKey(lessonId: String, layout: StableLessonStateLayout? = null) =
        "lesson_${lessonId}_read_pages_" + suffix(layout)
    private fun blocksKey(lessonId: String, layout: StableLessonStateLayout? = null) =
        "lesson_${lessonId}_read_blocks_" + suffix(layout)
    private fun suffix(layout: StableLessonStateLayout?) =
        if (layout?.version == 2) "v2_${layout.locale}" else "v1"
    private fun additionsKey(lessonId: String, layout: StableLessonStateLayout) =
        "lesson_${lessonId}_state_additions_v2_${layout.locale}"
    private fun v1OwnerKey(lessonId: String) = "lesson_${lessonId}_state_v1_owner_locale"
    private fun v2Pin(lessonId: String, language: String) = "lesson_${lessonId}_state_lineage_v2_$language"

    private fun additionRecords(layout: StableLessonStateLayout) = org.json.JSONArray().apply {
        layout.additions.forEach { addition -> put(org.json.JSONArray().put(addition.id).put(addition.sha256)
            .put(addition.storage.page).put(addition.storage.block).put(addition.progressPage)) }
    }

    @Synchronized
    private fun layout(context: Context, lessonId: String): StableLessonStateLayout? {
        val cache = layouts.getOrPut(context.assets) { mutableMapOf() }
        val key = key(context, lessonId)
        if (!cache.containsKey(key)) {
            val parts = lessonId.split("__", limit = 3)
            if (parts.size == 3) {
                var found: JSONObject? = null
                var sourceLanguage = CurriculumAssetLocale.current(context)
                for (route in listOf(AppearanceSettingsRepository.ROUTE_PROGRAMMING,
                                     AppearanceSettingsRepository.ROUTE_MATH_PHYSICS)) {
                    val path = "$route/${parts[0]}/${parts[1]}/lesson_${parts[2]}.json"
                    val asset = try { CurriculumAssetLocale.readResolved(context, path) }
                    catch (_: FileNotFoundException) { continue }
                    if (found != null) throw LessonStateLayoutException("Ambiguous lesson identity")
                    found = JSONObject(asset.text)
                    sourceLanguage = asset.language
                }
                val parsed = found?.let { json ->
                    if (json.has("stateLayout") && json.optString("id") != lessonId) {
                        throw LessonStateLayoutException("State map belongs to another lesson")
                    }
                    LessonStateLayoutJson.read(json)
                }
                if (parsed?.version == 2 && parsed.locale != sourceLanguage) {
                    throw LessonStateLayoutException("State map belongs to another asset language")
                }
                cache[key] = Resolved(parsed, sourceLanguage)
            } else {
                cache[key] = Resolved(null, CurriculumAssetLocale.current(context))
            }
        }
        val resolved = cache.getValue(key)
        checkAndSeed(context, lessonId, resolved.layout, resolved.language)
        return resolved.layout
    }

    /** Add migration metadata and reading coverage together; retain all legacy keys. */
    private fun checkAndSeed(context: Context, lessonId: String, layout: StableLessonStateLayout?, language: String) {
        try {
            checkAndSeedValues(context, lessonId, layout, language)
        } catch (error: LessonStateLayoutException) {
            throw error
        } catch (_: RuntimeException) {
            throw LessonStateLayoutException("Saved state metadata is unreadable; keep existing data")
        }
    }

    private fun checkAndSeedValues(context: Context, lessonId: String, layout: StableLessonStateLayout?, language: String) {
        val preferences = prefs(context)
        if (layout?.version != 2 && preferences.contains(v2Pin(lessonId, language))) {
            throw LessonStateLayoutException("State layout missing for its asset language")
        }
        val oldPin = preferences.getString(lineageKey(lessonId), null)
        if (layout == null) {
            val owner = preferences.getString(v1OwnerKey(lessonId), null)
            if (oldPin != null && (owner == null || owner == language)) {
                throw LessonStateLayoutException("State layout missing; do not use display positions")
            }
            return
        }
        if (layout.version == 2 && oldPin != null && oldPin != layout.legacyLineage) {
            val owner = preferences.getString(v1OwnerKey(lessonId), null)
            val ownerOrigin = owner?.let { layout.family[it]?.origin }
            val provenOtherLocale = owner != null && owner != language && ownerOrigin != null &&
                StableLessonStateLayout(ownerOrigin, ownerOrigin).lineage == oldPin
            if (!provenOtherLocale) {
                throw LessonStateLayoutException("Existing legacy ownership does not match this family")
            }
        }
        val pinKey = lineageKey(lessonId, layout)
        val readPagesKey = pagesKey(lessonId, layout)
        val readBlocksKey = blocksKey(lessonId, layout)
        val pinned = preferences.getString(pinKey, null)
        if (pinned != null) {
            if (pinned != layout.lineage) throw LessonStateLayoutException("Original state identities changed")
            if (!preferences.contains(readPagesKey) || !preferences.contains(readBlocksKey)) {
                throw LessonStateLayoutException("Saved reading coverage is incomplete")
            }
            layout.validateReadState(preferences.getStringSet(readPagesKey, emptySet())!!.toSet(),
                preferences.getStringSet(readBlocksKey, emptySet())!!.toSet())
            if (layout.version == 2) {
                val old = org.json.JSONArray(preferences.getString(additionsKey(lessonId, layout), null)
                    ?: throw LessonStateLayoutException("New activity ownership is incomplete"))
                val current = additionRecords(layout)
                if (old.length() > current.length() || (0 until old.length()).any {
                        old.getJSONArray(it).toString() != current.getJSONArray(it).toString() }) {
                    throw LessonStateLayoutException("New activity ownership changed")
                }
                if (old.length() != current.length() && !preferences.edit()
                        .putString(additionsKey(lessonId, layout), current.toString()).commit()) {
                    throw LessonStateLayoutException("Could not persist new activity ownership")
                }
            }
            return
        }
        if (preferences.contains(readPagesKey) || preferences.contains(readBlocksKey) ||
            (layout.version == 2 && preferences.contains(additionsKey(lessonId, layout)))) {
            throw LessonStateLayoutException("Incomplete state migration metadata")
        }
        val started = preferences.getBoolean("lesson_${lessonId}_started", false)
        val saved = preferences.getInt("lesson_${lessonId}_page", 0)
        var readPages = layout.legacyReadPages(started, saved)
        var readBlocks = layout.legacyReadBlocks(started, saved)
        val adoptV1 = layout.version == 2 && oldPin == layout.legacyLineage
        if (adoptV1) {
            if (!preferences.contains(pagesKey(lessonId)) || !preferences.contains(blocksKey(lessonId))) {
                throw LessonStateLayoutException("Saved reading coverage is incomplete")
            }
            readPages = preferences.getStringSet(pagesKey(lessonId), emptySet())!!.toSet()
            readBlocks = preferences.getStringSet(blocksKey(lessonId), emptySet())!!.toSet()
            layout.validateReadState(readPages, readBlocks)
        }
        val editor = preferences.edit().putString(pinKey, layout.lineage)
            .putStringSet(readPagesKey, readPages).putStringSet(readBlocksKey, readBlocks)
        if (layout.version == 2) {
            editor.putString(additionsKey(lessonId, layout), additionRecords(layout).toString())
            if (adoptV1) editor.putString(v1OwnerKey(lessonId), language)
        }
        if (started && !preferences.contains("lesson_${lessonId}_page")) {
            editor.putInt("lesson_${lessonId}_page", 0)
        }
        if (!editor.commit()) throw LessonStateLayoutException("Could not persist state migration metadata")
    }

    fun rethrowIfMapped(context: Context, lessonId: String, error: Exception) {
        if (error is LessonStateLayoutException) throw error
        if (prefs(context).all.keys.any { it == lineageKey(lessonId) ||
                it.startsWith("lesson_${lessonId}_state_lineage_v2_") }) {
            throw LessonStateLayoutException("Mapped lesson could not be loaded; keep existing data")
        }
    }

    @Synchronized
    fun prepare(context: Context, lessonId: String) {
        layout(context, lessonId)
    }

    @Synchronized
    fun narrationPageIdentity(
        context: Context, routeId: String, lessonId: String, page: Int
    ): String {
        val resolved = layout(context, lessonId) ?: return "$routeId/$lessonId/$page"
        return com.learngraphicsprogramming.app.narration.NarrationStableLesson.identity(
            resolved, routeId, lessonId, page
        )
    }

    fun attemptKey(context: Context, routeId: String, lessonId: String, page: Int, block: Int): ExerciseAttemptKey {
        val resolved = layout(context, lessonId)
        val origin = resolved?.storagePosition(page, block) ?: LessonStatePosition(page, block)
        val namespace = resolved?.additionNamespace(page, block)
        return ExerciseAttemptKey(routeId, if (namespace == null) lessonId else
            "${lessonId}__state_v2_$namespace", origin.page, origin.block)
    }

    @Synchronized
    fun block(context: Context, lessonId: String, page: Int, block: Int): LessonStatePosition =
        layout(context, lessonId)?.storagePosition(page, block) ?: LessonStatePosition(page, block)

    @Synchronized
    fun draftKey(context: Context, routeId: String, lessonId: String, page: Int, block: Int): String {
        val resolved = layout(context, lessonId)
        val origin = resolved?.storagePosition(page, block) ?: LessonStatePosition(page, block)
        val namespace = resolved?.additionNamespace(page, block)
        return if (namespace == null) "$routeId/$lessonId/${origin.page}/${origin.block}" else
            "$routeId/$lessonId/state-layout-v2/$namespace/${origin.page}/${origin.block}"
    }

    @Synchronized
    fun currentPage(context: Context, lessonId: String, saved: Int): Int {
        val layout = layout(context, lessonId) ?: return saved
        // A fresh student starts at the first DISPLAYED page, not original slot zero.
        if (!prefs(context).contains("lesson_${lessonId}_page")) return 0
        return layout.currentPage(saved)
    }

    /** One preference update stores the bookmark and its corresponding read coverage. */
    @Synchronized
    fun savePage(context: Context, lessonId: String, currentPage: Int) {
        val layout = layout(context, lessonId)
        val preferences = prefs(context)
        val editor = preferences.edit()
        editor.putInt("lesson_${lessonId}_page", layout?.storagePage(currentPage) ?: currentPage)
        if (layout != null) {
            val (pages, blocks) = layout.visited(currentPage,
                preferences.getStringSet(pagesKey(lessonId, layout), emptySet())!!.toSet(),
                preferences.getStringSet(blocksKey(lessonId, layout), emptySet())!!.toSet())
            editor.putStringSet(pagesKey(lessonId, layout), pages).putStringSet(blocksKey(lessonId, layout), blocks)
        }
        editor.apply()
    }

    /** Null keeps the historical behavior for a lesson not opted into stable layout. */
    @Synchronized
    fun isTheoryPageRead(context: Context, lessonId: String, currentPage: Int): Boolean? {
        val layout = layout(context, lessonId) ?: return null
        val preferences = prefs(context)
        if (!preferences.getBoolean("lesson_${lessonId}_started", false)) return false
        return layout.pageRead(currentPage,
            preferences.getStringSet(pagesKey(lessonId, layout), emptySet())!!.toSet(),
            preferences.getStringSet(blocksKey(lessonId, layout), emptySet())!!.toSet())
    }

    /** Keep the original progress weights when exercises move between visible pages. */
    @Synchronized
    fun progressGroups(context: Context, lessonId: String): List<List<LessonStatePosition>>? =
        layout(context, lessonId)?.progressGroups

    @Synchronized
    fun isTheoryProgressPageRead(context: Context, lessonId: String, originalPage: Int): Boolean? {
        val layout = layout(context, lessonId) ?: return null
        val preferences = prefs(context)
        if (!preferences.getBoolean("lesson_${lessonId}_started", false)) return false
        return layout.progressPageRead(originalPage,
            preferences.getStringSet(pagesKey(lessonId, layout), emptySet())!!.toSet(),
            preferences.getStringSet(blocksKey(lessonId, layout), emptySet())!!.toSet())
    }

    /** Namespaced new activities never claim an existing positional answer. */
    @Synchronized
    fun exerciseKey(context: Context, lessonId: String, page: Int, block: Int): String {
        val resolved = layout(context, lessonId)
        val origin = resolved?.storagePosition(page, block) ?: LessonStatePosition(page, block)
        val namespace = resolved?.additionNamespace(page, block)
        val scope = if (namespace == null) "exercise_${lessonId}" else "exercise_${lessonId}_state_v2_$namespace"
        return "${scope}_page_${origin.page}_block_${origin.block}"
    }

    /** Reset coverage for every known locale, retaining immutable ownership metadata. */
    fun clearReading(context: Context, lessonId: String, editor: SharedPreferences.Editor) {
        val preferences = prefs(context)
        if (preferences.contains(lineageKey(lessonId))) {
            editor.putStringSet(pagesKey(lessonId), emptySet()).putStringSet(blocksKey(lessonId), emptySet())
        }
        for (language in listOf("es", "en", "fr")) if (preferences.contains(v2Pin(lessonId, language))) {
            editor.putStringSet("lesson_${lessonId}_read_pages_v2_$language", emptySet())
                .putStringSet("lesson_${lessonId}_read_blocks_v2_$language", emptySet())
        }
    }
}

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
    private val layouts = WeakHashMap<AssetManager, MutableMap<Key, StableLessonStateLayout?>>()

    @Synchronized
    fun register(context: Context, lessonId: String, json: JSONObject) {
        if (json.has("stateLayout") && json.optString("id") != lessonId) {
            throw LessonStateLayoutException("State map belongs to another lesson")
        }
        val layout = LessonStateLayoutJson.read(json)
        checkAndSeed(context, lessonId, layout)
        layouts.getOrPut(context.assets) { mutableMapOf() }[key(context, lessonId)] = layout
    }

    private fun key(context: Context, lessonId: String) = Key(CurriculumAssetLocale.current(context), lessonId)
    private fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private fun lineageKey(lessonId: String) = "lesson_${lessonId}_state_lineage_v1"
    private fun pagesKey(lessonId: String) = "lesson_${lessonId}_read_pages_v1"
    private fun blocksKey(lessonId: String) = "lesson_${lessonId}_read_blocks_v1"

    @Synchronized
    private fun layout(context: Context, lessonId: String): StableLessonStateLayout? {
        val cache = layouts.getOrPut(context.assets) { mutableMapOf() }
        val key = key(context, lessonId)
        if (!cache.containsKey(key)) {
            val parts = lessonId.split("__", limit = 3)
            if (parts.size == 3) {
                var found: JSONObject? = null
                for (route in listOf(AppearanceSettingsRepository.ROUTE_PROGRAMMING,
                                     AppearanceSettingsRepository.ROUTE_MATH_PHYSICS)) {
                    val path = "$route/${parts[0]}/${parts[1]}/lesson_${parts[2]}.json"
                    val text = try { CurriculumAssetLocale.read(context, path) }
                    catch (_: FileNotFoundException) { continue }
                    if (found != null) throw LessonStateLayoutException("Ambiguous lesson identity")
                    found = JSONObject(text)
                }
                cache[key] = found?.let { json ->
                    if (json.has("stateLayout") && json.optString("id") != lessonId) {
                        throw LessonStateLayoutException("State map belongs to another lesson")
                    }
                    LessonStateLayoutJson.read(json)
                }
            } else {
                cache[key] = null
            }
        }
        return cache[key].also { checkAndSeed(context, lessonId, it) }
    }

    /** Add migration metadata and reading coverage together; retain all legacy keys. */
    private fun checkAndSeed(context: Context, lessonId: String, layout: StableLessonStateLayout?) {
        try {
            checkAndSeedValues(context, lessonId, layout)
        } catch (error: LessonStateLayoutException) {
            throw error
        } catch (_: RuntimeException) {
            throw LessonStateLayoutException("Saved state metadata is unreadable; keep existing data")
        }
    }

    private fun checkAndSeedValues(context: Context, lessonId: String, layout: StableLessonStateLayout?) {
        val preferences = prefs(context)
        val pinned = preferences.getString(lineageKey(lessonId), null)
        if (layout == null) {
            if (pinned != null) throw LessonStateLayoutException("State layout missing; do not use display positions")
            return
        }
        if (pinned != null) {
            if (pinned != layout.lineage) throw LessonStateLayoutException("Original state identities changed")
            if (!preferences.contains(pagesKey(lessonId)) || !preferences.contains(blocksKey(lessonId))) {
                throw LessonStateLayoutException("Saved reading coverage is incomplete")
            }
            layout.validateReadState(
                preferences.getStringSet(pagesKey(lessonId), emptySet())!!.toSet(),
                preferences.getStringSet(blocksKey(lessonId), emptySet())!!.toSet()
            )
            return
        }
        // Do not merge a torn/foreign metadata fragment with a new initial snapshot.
        if (preferences.contains(pagesKey(lessonId)) || preferences.contains(blocksKey(lessonId))) {
            throw LessonStateLayoutException("Incomplete state migration metadata")
        }
        val started = preferences.getBoolean("lesson_${lessonId}_started", false)
        val saved = preferences.getInt("lesson_${lessonId}_page", 0)
        val editor = preferences.edit()
            .putString(lineageKey(lessonId), layout.lineage)
            .putStringSet(pagesKey(lessonId), layout.legacyReadPages(started, saved))
            .putStringSet(blocksKey(lessonId), layout.legacyReadBlocks(started, saved))
        if (started && !preferences.contains("lesson_${lessonId}_page")) {
            editor.putInt("lesson_${lessonId}_page", 0)
        }
        val savedOk = editor.commit()
        if (!savedOk) throw LessonStateLayoutException("Could not persist state migration metadata")
    }

    fun rethrowIfMapped(context: Context, lessonId: String, error: Exception) {
        if (error is LessonStateLayoutException) throw error
        if (prefs(context).contains(lineageKey(lessonId))) {
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
        val origin = block(context, lessonId, page, block)
        return ExerciseAttemptKey(routeId, lessonId, origin.page, origin.block)
    }

    @Synchronized
    fun block(context: Context, lessonId: String, page: Int, block: Int): LessonStatePosition =
        layout(context, lessonId)?.storagePosition(page, block) ?: LessonStatePosition(page, block)

    @Synchronized
    fun draftKey(context: Context, routeId: String, lessonId: String, page: Int, block: Int): String {
        val origin = block(context, lessonId, page, block)
        return "$routeId/$lessonId/${origin.page}/${origin.block}"
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
                preferences.getStringSet(pagesKey(lessonId), emptySet())!!.toSet(),
                preferences.getStringSet(blocksKey(lessonId), emptySet())!!.toSet())
            editor.putStringSet(pagesKey(lessonId), pages).putStringSet(blocksKey(lessonId), blocks)
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
            preferences.getStringSet(pagesKey(lessonId), emptySet())!!.toSet(),
            preferences.getStringSet(blocksKey(lessonId), emptySet())!!.toSet())
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
            preferences.getStringSet(pagesKey(lessonId), emptySet())!!.toSet(),
            preferences.getStringSet(blocksKey(lessonId), emptySet())!!.toSet())
    }

    /** An explicit lesson reset clears coverage, not the ownership of the storage slots. */
    fun clearReading(context: Context, lessonId: String, editor: SharedPreferences.Editor) {
        if (prefs(context).contains(lineageKey(lessonId))) {
            editor.putStringSet(pagesKey(lessonId), emptySet())
                .putStringSet(blocksKey(lessonId), emptySet())
        }
    }
}

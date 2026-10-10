package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.CourseRepository
import com.learngraphicsprogramming.app.LessonRepository
import com.learngraphicsprogramming.app.UnitRepository
import java.io.FileNotFoundException

/** Stable curriculum order: route > course > unit > lesson > page > sentence. */
internal object NarrationCurriculum {
    private val failures = NarrationCurriculumFailurePolicy()

    private fun unavailable(context: Context, message: String, error: Throwable, fields: Map<String, Any>) {
        val path = error.message?.takeIf { it.isNotBlank() }
            ?: fields.values.joinToString("/")
        when (failures.classify(path, error)) {
            NarrationCurriculumFailurePolicy.Decision.FIRST_MISSING ->
                KokoroNarrationDiagnostics.event(context, "KokoroBackground", "Curriculum content not packaged",
                    fields = fields + mapOf("path" to path, "missingPaths" to failures.missingCount))
            NarrationCurriculumFailurePolicy.Decision.REPEATED_MISSING -> Unit
            NarrationCurriculumFailurePolicy.Decision.UNEXPECTED ->
                KokoroNarrationDiagnostics.event(context, "KokoroBackground", message, error, fields)
        }
    }

    data class Sentence(
        val text: String,
        val language: NarrationLanguage,
        val voice: String,
        val location: String
    )

    fun sentences(context: Context): Sequence<Sentence> = sequence {
        val preference = NarrationSettingsRepository.get(context)
        val routes = listOf(
            AppearanceSettingsRepository.ROUTE_PROGRAMMING,
            AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
        )
        for (route in routes) {
            val courses = runCatching { CourseRepository.loadCourses(context, route) }
                .onFailure { unavailable(context, "Unavailable curriculum route", it,
                    mapOf("route" to route)) }
                .getOrDefault(emptyList()).sortedBy { it.number }
            for (course in courses) {
                val units = runCatching { UnitRepository.loadUnits(context, route, course.id) }
                    .onFailure { unavailable(context, "Unavailable curriculum course", it,
                        mapOf("route" to route, "course" to course.id)) }
                    .getOrDefault(emptyList()).sortedBy { it.number }
                for (unit in units) {
                    val lessons = runCatching {
                        LessonRepository.loadLessons(context, route, course.id, unit.id)
                    }.onFailure { unavailable(context, "Unavailable curriculum unit", it,
                        mapOf("route" to route, "course" to course.id, "unit" to unit.id)) }
                        .getOrDefault(emptyList()).sortedBy { it.number }
                    for (lesson in lessons) {
                        val parsed = runCatching {
                            LessonRepository.loadLessonJson(context, route, lesson.id)
                        }
                        if (parsed.isFailure) {
                            unavailable(context, "Skipping unavailable lesson", requireNotNull(parsed.exceptionOrNull()),
                                mapOf("lesson" to lesson.id))
                            continue
                        }
                        val json = parsed.getOrThrow()
                        val pages = json.optJSONArray("pages") ?: continue
                        for (pageIndex in 0 until pages.length()) {
                            val language = NarrationLanguage.forLesson(json, pageIndex, preference.language)
                            val options = NarrationSettingsRepository.forLanguage(context, language)
                            val texts = LessonNarrationText.page(json, pageIndex, language) { blockType ->
                                NarrationUiInstructions.forBlock(context, blockType)
                            }
                            for (text in NarrationSegments.sentences(texts)) {
                                yield(Sentence(
                                    text, language, options.kokoroVoice,
                                    "$route/${course.id}/${unit.id}/${lesson.id}/${pageIndex + 1}"
                                ))
                            }
                        }
                    }
                }
            }
        }
    }
}

/** Optional future-course assets are expected; corrupt installed JSON must keep its full error. */
internal class NarrationCurriculumFailurePolicy {
    enum class Decision { FIRST_MISSING, REPEATED_MISSING, UNEXPECTED }
    private val missing = mutableSetOf<String>()
    val missingCount: Int get() = synchronized(this) { missing.size }

    @Synchronized fun classify(path: String, error: Throwable): Decision {
        if (error !is FileNotFoundException) return Decision.UNEXPECTED
        return if (missing.add(path)) Decision.FIRST_MISSING else Decision.REPEATED_MISSING
    }
}

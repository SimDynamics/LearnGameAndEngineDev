package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.util.Log
import com.learngraphicsprogramming.app.AppearanceSettingsRepository
import com.learngraphicsprogramming.app.CourseRepository
import com.learngraphicsprogramming.app.LessonRepository
import com.learngraphicsprogramming.app.UnitRepository

/** Stable curriculum order: route > course > unit > lesson > page > sentence. */
internal object NarrationCurriculum {
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
                .getOrDefault(emptyList()).sortedBy { it.number }
            for (course in courses) {
                val units = runCatching { UnitRepository.loadUnits(context, route, course.id) }
                    .getOrDefault(emptyList()).sortedBy { it.number }
                for (unit in units) {
                    val lessons = runCatching {
                        LessonRepository.loadLessons(context, route, course.id, unit.id)
                    }.getOrDefault(emptyList()).sortedBy { it.number }
                    for (lesson in lessons) {
                        val parsed = runCatching {
                            LessonRepository.loadLessonJson(context, route, lesson.id)
                        }
                        if (parsed.isFailure) {
                            Log.w("KokoroBackground", "Skipping unavailable lesson: ${lesson.id}", parsed.exceptionOrNull())
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

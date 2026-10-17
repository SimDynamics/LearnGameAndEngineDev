package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.CourseRepository

/** Snapshot of the packaged curriculum versus verified persistent Kokoro files.
 * It is computed only off the UI thread, and never changes audio or lesson data.
 *
 * The course percentages concern the language/voice configuration currently used
 * to prepare lessons. A shared sentence can belong to several courses, so the
 * displayed per-course sizes are not independent reclaimable storage amounts.
 */
internal object KokoroCourseAudioStats {
    data class Course(
        val routeId: String,
        val courseId: String,
        val number: Int,
        val title: String,
        val coverage: KokoroCourseAudioCoverage.Result
    )
    data class Report(
        val courses: List<Course>,
        val totalVerifiedFiles: Int,
        val totalVerifiedBytes: Long
    )

    fun scan(context: Context, routeFilter: String? = null, cancelled: () -> Boolean = {
        Thread.currentThread().isInterrupted
    }): Report {
        val app = context.applicationContext
        fun check() { if (cancelled()) throw InterruptedException("Course audio scan cancelled") }
        check()
        val cache = KokoroNarrationCache.verifiedInventory(app, cancelled)
        check()

        val definitions = NarrationAudioPlan.ROUTES.filter { routeFilter == null || it == routeFilter }.flatMap { route ->
            check()
            CourseRepository.loadCourses(app, route).sortedBy { it.number }.map { course ->
                Triple(route, course.id, course)
            }
        }
        // Construct entries for all courses, including courses without generated audio.
        val expected = linkedMapOf<Pair<String, String>, MutableSet<String>>()
        definitions.forEach { (route, courseId, _) -> expected[route to courseId] = linkedSetOf() }

        // One curricular pass, instead of reparsing every course for each row.
        for (page in NarrationCurriculum.pages(app, routeFilter = routeFilter)) {
            check()
            val keys = expected[page.routeId to page.courseId] ?: continue
            for (sentence in page.sentences) {
                check()
                keys.add(NarrationPageQueue.hash(sentence))
            }
        }
        check()

        return Report(
            courses = definitions.map { (route, id, course) ->
                check()
                Course(route, id, course.number, course.title,
                    KokoroCourseAudioCoverage.calculate(expected[route to id].orEmpty(), cache))
            },
            totalVerifiedFiles = cache.values.sumOf { it.files },
            totalVerifiedBytes = cache.values.sumOf { it.bytes }
        )
    }
}

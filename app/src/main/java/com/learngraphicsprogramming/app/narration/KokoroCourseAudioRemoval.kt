package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.CourseRepository
import com.learngraphicsprogramming.app.CurriculumAssetLocale

/** Explicit, confirmed deletion of ONLY uniquely owned, verified Kokoro files.
 *
 * No cache eviction, background deletion or curriculum modification. The preview
 * is derived on demand from the current localized curriculum and voice choices.
 * Historical recordings from other languages/voices are deliberately retained:
 * their source ownership cannot be inferred safely from an opaque hash.
 */
internal object KokoroCourseAudioRemoval {
    data class Preview(
        val course: KokoroCourseRef,
        val curriculumLanguage: String,
        val narratorLanguage: String,
        val narratorVoice: String,
        val ownership: KokoroCourseAudioOwnership.Plan
    )

    enum class Status { COMPLETED, BUSY, CHANGED, CANCELLED }

    data class Result(
        val status: Status,
        val deletedFiles: Int = 0,
        val deletedBytes: Long = 0L,
        val failedFiles: Int = 0,
        val stoppedEarly: Boolean = false
    )

    fun preview(
        context: Context, routeId: String, courseId: String,
        cancelled: () -> Boolean = { Thread.currentThread().isInterrupted }
    ): Preview {
        val app = context.applicationContext
        require(routeId in NarrationAudioPlan.ROUTES && courseId.isNotBlank())
        fun check() { if (cancelled()) throw InterruptedException("Course audio preview cancelled") }
        check()
        require(CourseRepository.loadCourses(app, routeId).any { it.id == courseId }) {
            "Course not found in the current curriculum"
        }
        val locale = CurriculumAssetLocale.current(app)
        val settings = NarrationSettingsRepository.get(app)
        val inventory = KokoroNarrationCache.verifiedInventory(app, cancelled)
        val references = linkedMapOf<KokoroCourseRef, MutableSet<String>>()
        // Scan both learning paths, never just the selected list: a common
        // sentence in another course must retain its physical audio file.
        for (page in NarrationCurriculum.pages(app)) {
            check()
            val owner = KokoroCourseRef(page.routeId, page.courseId)
            val keys = references.getOrPut(owner) { linkedSetOf() }
            for (sentence in page.sentences) {
                check()
                val hash = NarrationPageQueue.hash(sentence)
                if (hash in inventory) keys += hash
            }
        }
        check()
        val latest = NarrationSettingsRepository.get(app)
        require(locale == CurriculumAssetLocale.current(app) &&
            settings.language == latest.language &&
            settings.kokoroVoice == latest.kokoroVoice) {
            "Narration language or voice changed during audio preview"
        }
        val course = KokoroCourseRef(routeId, courseId)
        return Preview(course, locale, settings.language.tag, settings.kokoroVoice,
            KokoroCourseAudioOwnership.plan(course, references, inventory))
    }

    /**
     * Called only following an explicit confirmation showing the exact preview.
     * A maintenance lease cancels background generation and refuses to start
     * if playback/compilation/other interactive work is already active.
     */
    fun remove(context: Context, preview: Preview,
               cancelled: () -> Boolean = { Thread.currentThread().isInterrupted }): Result {
        val app = context.applicationContext
        if (cancelled()) return Result(Status.CANCELLED)
        val current = NarrationSettingsRepository.get(app)
        if (CurriculumAssetLocale.current(app) != preview.curriculumLanguage ||
            current.language.tag != preview.narratorLanguage ||
            current.kokoroVoice != preview.narratorVoice) return Result(Status.CHANGED)
        val lease = NarrationWorkPriority.beginMaintenanceIfIdle() ?: return Result(Status.BUSY)
        try {
            if (cancelled() || !NarrationWorkPriority.isMaintenanceExclusive)
                return Result(Status.CANCELLED)
            // This pause is disclosed in the confirmation. It prevents
            // immediately regenerating what the user explicitly removed.
            NarrationSettingsRepository.saveCourseGenerationPaused(
                app, preview.course.routeId, preview.course.courseId, true)
            val actual = KokoroNarrationCache.deleteExclusiveVerified(
                app, preview.ownership.removableKeys) {
                !cancelled() && NarrationWorkPriority.isMaintenanceExclusive
            }
            return Result(Status.COMPLETED, actual.deletedFiles, actual.deletedBytes,
                actual.failedFiles, actual.interrupted)
        } finally {
            lease.close()
        }
    }
}

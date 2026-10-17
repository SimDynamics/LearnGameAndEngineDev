package com.learngraphicsprogramming.app.narration

/** Immutable, verified cache inventory entry. A WAV and an Ogg of the same
 * sentence are two physical files but only one generated sentence. */
internal data class KokoroCachedAudio(val files: Int, val bytes: Long)

/** Pure accounting shared by the per-course report and its JVM tests. */
internal object KokoroCourseAudioCoverage {
    data class Result(
        val expectedSentences: Int,
        val generatedSentences: Int,
        val files: Int,
        val bytes: Long
    ) {
        val percent: Int? get() = if (expectedSentences == 0) null
            else ((generatedSentences.toLong() * 100 + expectedSentences / 2) /
                expectedSentences).toInt()
    }

    fun calculate(expectedKeys: Collection<String>,
                  cached: Map<String, KokoroCachedAudio>): Result {
        val unique = expectedKeys.toSet()
        var generated = 0
        var files = 0
        var bytes = 0L
        for (key in unique) {
            val available = cached[key] ?: continue
            if (available.files <= 0 || available.bytes <= 0) continue
            generated++
            files += available.files
            bytes += available.bytes
        }
        return Result(unique.size, generated, files, bytes)
    }
}

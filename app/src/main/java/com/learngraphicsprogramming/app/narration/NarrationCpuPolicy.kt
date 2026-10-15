package com.learngraphicsprogramming.app.narration

/** CPU identifiers come from the calling thread's real affinity mask, never its array position. */
internal data class NarrationCpuPerformance(
    val cpu: Int,
    val capacity: Long? = null,
    val maxFrequencyKhz: Long? = null
)

internal data class NarrationCpuSelection(
    val allowed: List<Int>,
    val selected: List<Int>,
    val ranking: String
)

internal object NarrationCpuPolicy {
    fun select(allowed: List<Int>, performance: List<NarrationCpuPerformance>): NarrationCpuSelection {
        val candidates = allowed.distinct().sorted()
        require(candidates.isNotEmpty() && candidates.first() >= 0) { "No usable CPU mask" }
        val scores = performance.associateBy { it.cpu }
        val capacityKnown = candidates.all { (scores[it]?.capacity ?: 0) > 0 }
        val frequencyKnown = candidates.all { (scores[it]?.maxFrequencyKhz ?: 0) > 0 }
        val ranking = when {
            capacityKnown -> "cpu_capacity"
            frequencyKnown -> "cpuinfo_max_freq"
            else -> "stable_cpu_id_fallback_performance_unknown"
        }
        val ordered = when {
            capacityKnown -> candidates.sortedWith(compareByDescending<Int> { scores.getValue(it).capacity }
                .thenByDescending { scores[it]?.maxFrequencyKhz ?: 0 }.thenBy { it })
            frequencyKnown -> candidates.sortedWith(compareByDescending<Int> { scores.getValue(it).maxFrequencyKhz }
                .thenBy { it })
            else -> candidates
        }
        return NarrationCpuSelection(candidates, ordered.take(2).sorted(), ranking)
    }
}

/** Testable system boundary: each operation addresses only the caller's native thread. */
internal interface NarrationCpuAffinity {
    fun currentTid(): Long
    fun allowedCpus(): List<Int>
    fun setAllowedCpus(cpus: List<Int>)
}

internal class NarrationCpuLimitException(message: String, cause: Throwable? = null) :
    IllegalStateException(message, cause)

/**
 * All optional narration jobs share one pair. An overlapping scope cannot silently select a
 * second, disjoint pair when Android changes its allowed mask. There is no process-wide affinity.
 */
internal class NarrationCpuScopes(
    private val affinity: NarrationCpuAffinity,
    private val performance: (List<Int>) -> List<NarrationCpuPerformance>,
    private val report: (NarrationCpuSelection) -> Unit = {}
) {
    private val lock = Any()
    private var shared: NarrationCpuSelection? = null
    private var active = 0

    fun capBackground(): AutoCloseable = synchronized(lock) {
        val tid = affinity.currentTid()
        val original = affinity.allowedCpus().distinct().sorted()
        if (original.isEmpty() || original.any { it < 0 }) {
            throw NarrationCpuLimitException("Android did not provide a usable thread CPU mask")
        }
        val previous = shared
        val selection = when {
            active == 0 -> NarrationCpuPolicy.select(original, performance(original))
            previous != null && original.containsAll(previous.selected) ->
                NarrationCpuSelection(original, previous.selected, previous.ranking)
            previous == null -> throw NarrationCpuLimitException("Missing active narration CPU pair")
            else -> {
                val intersection = previous.selected.filter { it in original }
                if (intersection.isEmpty()) throw NarrationCpuLimitException(
                    "The active narration CPU pair is unavailable to this thread")
                NarrationCpuSelection(original, intersection, "shared_${previous.ranking}")
            }
        }
        try {
            affinity.setAllowedCpus(selection.selected)
            val actual = affinity.allowedCpus().distinct().sorted()
            if (actual != selection.selected || actual.size !in 1..2) {
                throw NarrationCpuLimitException("Android did not apply the requested two-CPU limit")
            }
        } catch (error: Throwable) {
            try { affinity.setAllowedCpus(original) }
            catch (restoreError: Throwable) { error.addSuppressed(restoreError) }
            throw if (error is NarrationCpuLimitException) error else
                NarrationCpuLimitException("Cannot guarantee the background narration CPU limit", error)
        }
        if (active == 0) shared = selection
        active++
        runCatching { report(selection) }
        object : AutoCloseable {
            private var closed = false
            override fun close() = synchronized(lock) {
                if (closed) return@synchronized
                if (affinity.currentTid() != tid) throw NarrationCpuLimitException(
                    "Narration CPU affinity must be restored by its original thread")
                try {
                    affinity.setAllowedCpus(original)
                    if (affinity.allowedCpus().distinct().sorted() != original) {
                        throw NarrationCpuLimitException("Android did not restore the original narration thread CPU mask")
                    }
                } catch (error: Throwable) {
                    throw if (error is NarrationCpuLimitException) error else
                        NarrationCpuLimitException("Cannot restore the narration thread CPU mask", error)
                } finally {
                    closed = true
                    active--
                }
            }
        }
    }
}

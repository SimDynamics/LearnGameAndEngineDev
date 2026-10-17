package com.learngraphicsprogramming.app.narration

/** One route/course pair. IDs alone are not globally unique across learning paths. */
internal data class KokoroCourseRef(val routeId: String, val courseId: String)

/** Select only actual verified recordings uniquely owned by the requested course.
 * Read-only: this algorithm never looks at the filesystem or changes the cache.
 * The caller must build references for EVERY available course, not just one route.
 */
internal object KokoroCourseAudioOwnership {
    data class Plan(
        val removableKeys: Set<String>,
        val removableFiles: Int,
        val removableBytes: Long,
        val sharedKeys: Set<String>,
        val sharedFiles: Int,
        val sharedBytes: Long
    )

    fun plan(
        target: KokoroCourseRef,
        references: Map<KokoroCourseRef, Set<String>>,
        inventory: Map<String, KokoroCachedAudio>
    ): Plan {
        val ours = references[target].orEmpty().filterTo(linkedSetOf()) { key ->
            inventory[key]?.let { it.files > 0 && it.bytes > 0 } == true
        }
        val shared = linkedSetOf<String>()
        references.forEach { (owner, keys) ->
            if (owner != target) {
                for (key in keys) if (key in ours) shared += key
            }
        }
        val exclusive = ours - shared
        fun count(keys: Set<String>) = keys.sumOf { inventory.getValue(it).files }
        fun bytes(keys: Set<String>) = keys.sumOf { inventory.getValue(it).bytes }
        return Plan(exclusive, count(exclusive), bytes(exclusive),
            shared, count(shared), bytes(shared))
    }
}

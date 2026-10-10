package com.learngraphicsprogramming.app.narration

import ai.onnxruntime.OrtException
import android.util.Log
import java.io.File
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantLock

/** One shared neural session, with cancellation scoped to the request that owns it. */
internal object KokoroEngineCoordinator {
    private val exclusive = ReentrantLock(true)
    private val state = Any()
    private var engine: KokoroOnnxEngine? = null
    private var directory: File? = null
    private var foregroundProfile: Boolean? = null
    private val failedNnapi = mutableSetOf<File>()
    private var active: Lease? = null

    private class Lease(val owner: Any) {
        @Volatile var cancelled = false
    }

    fun <T> withEngine(
        owner: Any,
        root: File,
        isCancelled: () -> Boolean,
        foreground: Boolean = false,
        onProviderFailure: (Throwable) -> Unit = {},
        block: (KokoroOnnxEngine) -> T
    ): T {
        while (!exclusive.tryLock(50, TimeUnit.MILLISECONDS)) {
            if (isCancelled()) throw InterruptedException("Kokoro request cancelled while waiting")
        }
        val lease = Lease(owner)
        try {
            if (isCancelled()) throw InterruptedException("Kokoro request cancelled before acquisition")
            synchronized(state) { active = lease }
            val canonical = root.canonicalFile
            if (directory != canonical || engine == null || foregroundProfile != foreground) {
                engine?.close()
                engine = null
                directory = canonical
                foregroundProfile = foreground
                // Preserve the existing fast page profile and economical curriculum
                // profile, but close the old session before opening another one.
                engine = KokoroOnnxEngine(canonical,
                    preferAccelerator = foreground && canonical !in failedNnapi,
                    cpuThreads = if (foreground) null else 2)
            }
            if (lease.cancelled || isCancelled()) throw InterruptedException("Kokoro request cancelled during initialization")
            val selected = requireNotNull(engine)
            return try {
                block(selected)
            } catch (error: Exception) {
                val inferenceError = generateSequence(error as Throwable) { it.cause }.any { it is OrtException }
                if (!foreground || !selected.usesNnapi || !inferenceError || lease.cancelled || isCancelled()) throw error
                // Some drivers accept the graph, then reject an operator on run().
                // Retry the complete sentence on CPU while retaining exclusive ownership.
                Log.w("KokoroInference", "NNAPI inference failed; retrying the full request on CPU", error)
                runCatching { onProviderFailure(error) }
                failedNnapi.add(canonical)
                selected.close()
                engine = null
                val cpu = KokoroOnnxEngine(canonical, preferAccelerator = false).also { engine = it }
                if (lease.cancelled || isCancelled()) throw InterruptedException("Kokoro request cancelled during CPU retry")
                block(cpu)
            }
        } catch (error: Exception) {
            // ORT reports termination as OrtException. It is a scheduling event,
            // not a synthesis failure that warrants changing to Android TTS.
            if (lease.cancelled || isCancelled()) {
                throw InterruptedException("Kokoro request preempted").apply { initCause(error) }
            }
            throw error
        } finally {
            synchronized(state) { if (active === lease) active = null }
            exclusive.unlock()
        }
    }

    fun cancel(owner: Any) = synchronized(state) {
        active?.takeIf { it.owner === owner }?.let { lease ->
            lease.cancelled = true
            engine?.cancelCurrentRun()
        }
    }

    /** Called on a worker after its session ends; never closes an engine in use. */
    fun releaseIfIdle() {
        if (!exclusive.tryLock()) return
        try {
            engine?.close()
            engine = null
            directory = null
            foregroundProfile = null
        } finally {
            exclusive.unlock()
        }
    }
}

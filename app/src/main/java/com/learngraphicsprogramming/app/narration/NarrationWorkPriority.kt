package com.learngraphicsprogramming.app.narration

import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Central arbitration for *optional* offline narration generation.
 * Live Kokoro playback, compilation and graphics always win.
 * Each interactive lease must be closed in finally or on lifecycle pause.
 */
object NarrationWorkPriority {
    private val interactiveCount = AtomicInteger()
    private val interrupts = CopyOnWriteArrayList<() -> Unit>()

    val isBusy: Boolean get() = interactiveCount.get() > 0

    fun beginInteractive(): AutoCloseable {
        interactiveCount.incrementAndGet()
        // Cancel any background ONNX Run immediately; it restarts from the
        // first missing sentence using the persistent WAV cache.
        interrupts.forEach { listener -> runCatching { listener() } }
        val closed = AtomicBoolean()
        return AutoCloseable {
            if (closed.compareAndSet(false, true)) interactiveCount.decrementAndGet()
        }
    }

    fun onPreempt(listener: () -> Unit): AutoCloseable {
        interrupts.add(listener)
        if (isBusy) listener()
        return AutoCloseable { interrupts.remove(listener) }
    }
}

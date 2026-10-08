package com.learngraphicsprogramming.app.narration

import android.os.Handler
import android.os.Looper
import java.util.concurrent.CopyOnWriteArraySet

/** In-process lifecycle signal: release synthesis sessions before removing their files. */
internal object NarrationResourceEvents {
    private val main = Handler(Looper.getMainLooper())
    private val listeners = CopyOnWriteArraySet<() -> Unit>()
    fun addListener(listener: () -> Unit): () -> Unit {
        listeners.add(listener)
        return { listeners.remove(listener); Unit }
    }
    fun removingResources() { main.post { listeners.forEach { it() } } }
}

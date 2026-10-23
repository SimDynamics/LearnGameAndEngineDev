package com.learngraphicsprogramming.app.narration

import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Central arbitration for optional background narration.
 * Interactive playback, compilation and graphics always take precedence.
 */
object NarrationWorkPriority {
    private val interactiveCount = AtomicInteger()
    private val interrupts = CopyOnWriteArrayList<() -> Unit>()
    private val leaseLock = Any()
    // Once a reader enters, this maintenance attempt may never resume deletion.
    private var maintenanceActive = false
    private var maintenancePreempted = false

    val isBusy: Boolean get() = interactiveCount.get() > 0

    fun beginInteractive(): AutoCloseable {
        synchronized(leaseLock) {
            interactiveCount.incrementAndGet()
            if (maintenanceActive) maintenancePreempted = true
        }
        interrupts.forEach { listener -> runCatching { listener() } }
        val closed = AtomicBoolean()
        return AutoCloseable {
            if (closed.compareAndSet(false, true)) {
                synchronized(leaseLock) { interactiveCount.decrementAndGet() }
            }
        }
    }

    fun beginMaintenanceIfIdle(): AutoCloseable? {
        synchronized(leaseLock) {
            if (interactiveCount.get() != 0 || maintenanceActive) return null
            maintenanceActive = true
            maintenancePreempted = false
            interactiveCount.incrementAndGet()
        }
        // Preempt background generation without keeping the bookkeeping lock.
        interrupts.forEach { listener -> runCatching { listener() } }
        val closed = AtomicBoolean()
        return AutoCloseable {
            if (closed.compareAndSet(false, true)) {
                synchronized(leaseLock) {
                    maintenanceActive = false
                    interactiveCount.decrementAndGet()
                }
            }
        }
    }

    val isMaintenanceExclusive: Boolean get() = synchronized(leaseLock) {
        maintenanceActive && !maintenancePreempted && interactiveCount.get() == 1
    }

    /**
     * Hold the short critical section for one confirmed, verified deletion.
     * A reader cannot acquire a lease midway through removing that file.
     */
    internal fun runMaintenanceStep(action: () -> Unit): Boolean = synchronized(leaseLock) {
        if (!maintenanceActive || maintenancePreempted || interactiveCount.get() != 1) return false
        action()
        true
    }

    fun onPreempt(listener: () -> Unit): AutoCloseable {
        interrupts.add(listener)
        if (isBusy) listener()
        return AutoCloseable { interrupts.remove(listener) }
    }
}

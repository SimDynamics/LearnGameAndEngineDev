package lgaed.narration.windows

/** Explicit desktop platform adapters; no fake android.* classes are supplied. */
object WindowsWorkerThread {
    fun isWorker(): Boolean = Thread.currentThread().name == "windows-kokoro-worker"
}
object WindowsHost {
    const val nnapiAvailable = false // Android-only provider, never reported as Windows acceleration.
    fun elapsedRealtime(): Long = System.nanoTime() / 1_000_000
}
object WindowsLog {
    fun i(tag: String, message: String): Int { System.err.println("$tag: $message"); return 0 }
    fun w(tag: String, message: String, error: Throwable): Int {
        System.err.println("$tag: $message (${error.message})"); return 0
    }
}

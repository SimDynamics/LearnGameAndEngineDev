package lgaed.narration.windows

import java.util.concurrent.FutureTask

/** The calculator mirrors Android's Future.cancel(true), including queued work.
 * No synthesis session, editor executor or CPU policy is owned by this task. */
internal class WindowsCalculatorTask(work: Runnable,
    private val cancelled: () -> Unit,
    private val completed: () -> Unit
) : FutureTask<Unit>(work,Unit) {
    override fun done() {
        try { if(isCancelled)cancelled() } finally { completed() }
    }
}

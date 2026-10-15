package com.learngraphicsprogramming.app.runtime

import android.content.Context
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import com.google.android.play.core.splitinstall.SplitInstallManager
import com.google.android.play.core.splitinstall.SplitInstallManagerFactory
import com.google.android.play.core.splitinstall.SplitInstallRequest
import com.google.android.play.core.splitinstall.SplitInstallSessionState
import com.google.android.play.core.splitinstall.SplitInstallStateUpdatedListener
import com.google.android.play.core.splitinstall.model.SplitInstallSessionStatus
import com.learngraphicsprogramming.app.BuildConfig
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery.Phase
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery.State
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery.Companion.MODULE_NAME

/**
 * Installation transport for the optional FoldCode Clang 21 dynamic feature.
 *
 * No download occurs in the constructor. An Activity may call requestInstall()
 * only after the user explicitly chooses to obtain the alternative compiler.
 *
 * This does not select Clang 21 for compilation: native split loading and
 * compiler selection are separate, later stages of the migration.
 */
internal class PlayClang21Delivery(context: Context) : Clang21Delivery {

    private val applicationContext = context.applicationContext
    private val manager: SplitInstallManager =
        SplitInstallManagerFactory.create(applicationContext)

    private var listener: SplitInstallStateUpdatedListener? = null
    private var sessionId: Int? = null
    private var awaitingConfirmation: SplitInstallSessionState? = null

    // In --local-testing, Play's fake manager can report a SplitCompat-emulated
    // feature as installed even though PackageManager has no executable split.
    // Clang launches a subprocess: extracted app-private files cannot be exec'd
    // on Android 10+, so delivery and executable installation must be distinct.
    private fun isPlatformSplitInstalled(): Boolean = runCatching {
        applicationContext.packageManager
            .getPackageInfo(applicationContext.packageName, 0)
            .splitNames?.contains(MODULE_NAME) == true
    }.getOrDefault(false)

    override fun currentState(): State = when {
        !BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED ->
            State(Phase.NOT_IN_BUILD)
        isPlatformSplitInstalled() -> State(Phase.INSTALLED)
        manager.installedModules.contains(MODULE_NAME) -> State(Phase.EMULATED)
        else -> State(Phase.NOT_INSTALLED)
    }

    /**
     * Must be invoked from a foreground Activity, after a user action.
     * The callback receives all relevant phases, download progress and errors.
     *
     * Register before startInstall to avoid losing initial state updates.
     */
    override fun requestInstall(onUpdate: (State) -> Unit) {
        val current = currentState()
        if (current.phase != Phase.NOT_INSTALLED) {
            onUpdate(current)
            return
        }
        if (listener != null) {
            onUpdate(State(Phase.REQUESTED, sessionId = sessionId))
            return
        }

        val stateListener = SplitInstallStateUpdatedListener { update ->
            if (!update.moduleNames().contains(MODULE_NAME)) {
                return@SplitInstallStateUpdatedListener
            }
            sessionId = update.sessionId()
            awaitingConfirmation =
                if (update.status() == SplitInstallSessionStatus.REQUIRES_USER_CONFIRMATION)
                    update
                else
                    null

            val phase = when (update.status()) {
                SplitInstallSessionStatus.PENDING -> Phase.PENDING
                SplitInstallSessionStatus.DOWNLOADING -> Phase.DOWNLOADING
                SplitInstallSessionStatus.DOWNLOADED -> Phase.DOWNLOADED
                SplitInstallSessionStatus.INSTALLING -> Phase.INSTALLING
                SplitInstallSessionStatus.INSTALLED -> Phase.INSTALLED
                SplitInstallSessionStatus.REQUIRES_USER_CONFIRMATION ->
                    Phase.NEEDS_CONFIRMATION
                SplitInstallSessionStatus.FAILED -> Phase.FAILED
                SplitInstallSessionStatus.CANCELING -> Phase.CANCELING
                SplitInstallSessionStatus.CANCELED -> Phase.CANCELED
                else -> Phase.UNKNOWN
            }

            onUpdate(
                State(
                    phase = if (phase == Phase.INSTALLED && !isPlatformSplitInstalled())
                        Phase.EMULATED
                    else
                        phase,
                    downloadedBytes = update.bytesDownloaded(),
                    totalBytes = update.totalBytesToDownload(),
                    sessionId = update.sessionId(),
                    errorCode = if (phase == Phase.FAILED) update.errorCode() else null
                )
            )

            if (phase == Phase.INSTALLED || phase == Phase.FAILED ||
                phase == Phase.CANCELED
            ) {
                stopObserving()
            }
        }

        listener = stateListener
        manager.registerListener(stateListener)
        onUpdate(State(Phase.REQUESTED))

        val request = SplitInstallRequest.newBuilder()
            .addModule(MODULE_NAME)
            .build()

        try {
            manager.startInstall(request)
                .addOnSuccessListener { id ->
                    sessionId = id
                    // Google Play returns zero when the module is already present.
                    if (id == 0) {
                        onUpdate(currentState())
                        stopObserving()
                    }
                }
                .addOnFailureListener { error ->
                    onUpdate(State(Phase.FAILED, message = error.message))
                    stopObserving()
                }
        } catch (error: RuntimeException) {
            onUpdate(State(Phase.FAILED, message = error.message))
            stopObserving()
        }
    }

    /**
     * Called only after NEEDS_CONFIRMATION, from an Activity which has already
     * registered an ActivityResultLauncher<IntentSenderRequest>.
     */
    override fun requestUserConfirmation(
        launcher: ActivityResultLauncher<IntentSenderRequest>
    ): Boolean {
        val pending = awaitingConfirmation ?: return false
        return manager.startConfirmationDialogForResult(pending, launcher)
    }

    /**
     * Cancellation is best effort; the listener reports CANCELED if accepted.
     */
    override fun cancel(onFailure: (String?) -> Unit) {
        val active = sessionId ?: return
        manager.cancelInstall(active)
            .addOnFailureListener { error -> onFailure(error.message) }
    }

    private fun stopObserving() {
        listener?.let(manager::unregisterListener)
        listener = null
        sessionId = null
        awaitingConfirmation = null
    }

    /** Call from Activity.onDestroy to avoid leaking its callback. */
    override fun close() {
        stopObserving()
    }
}

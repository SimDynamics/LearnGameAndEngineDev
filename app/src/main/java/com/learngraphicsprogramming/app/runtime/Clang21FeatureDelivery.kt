package com.learngraphicsprogramming.app.runtime

import android.content.Context
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import com.learngraphicsprogramming.app.BuildConfig

/** Distribution selection is a build decision; Google Play does not accept a URL provider. */
internal class Clang21FeatureDelivery(context: Context) : AutoCloseable {
    companion object { const val MODULE_NAME = "clang21" }

    enum class Phase {
        NOT_IN_BUILD, NOT_INSTALLED, REQUESTED, PENDING, DOWNLOADING, DOWNLOADED,
        INSTALLING, NEEDS_CONFIRMATION, INSTALLED, EMULATED, FAILED, CANCELING,
        CANCELED, UNKNOWN
    }

    data class State(
        val phase: Phase,
        val downloadedBytes: Long = 0L,
        val totalBytes: Long = 0L,
        val sessionId: Int? = null,
        val errorCode: Int? = null,
        val message: String? = null
    )

    // The HTTPS implementation and REQUEST_INSTALL_PACKAGES permission are absent
    // from Play builds. Reflection keeps this facade independent of that source set.
    private val delivery: Clang21Delivery = when (BuildConfig.LGP_CLANG21_DELIVERY_PROVIDER) {
        "play" -> PlayClang21Delivery(context.applicationContext)
        "https" -> runCatching {
            Class.forName("com.learngraphicsprogramming.app.runtime.Clang21HttpsDelivery")
                .getConstructor(Context::class.java)
                .newInstance(context.applicationContext) as Clang21Delivery
        }.getOrElse { DisabledClang21Delivery(it.cause?.message ?: it.message) }
        else -> DisabledClang21Delivery()
    }

    fun currentState(): State = delivery.currentState()
    fun observe(onUpdate: (State) -> Unit) = delivery.observe(onUpdate)
    fun requestInstall(onUpdate: (State) -> Unit) = delivery.requestInstall(onUpdate)
    fun requestUserConfirmation(launcher: ActivityResultLauncher<IntentSenderRequest>): Boolean =
        delivery.requestUserConfirmation(launcher)
    fun cancel(onFailure: (String?) -> Unit = {}) = delivery.cancel(onFailure)
    override fun close() = delivery.close()
}

internal interface Clang21Delivery : AutoCloseable {
    fun currentState(): Clang21FeatureDelivery.State
    fun observe(onUpdate: (Clang21FeatureDelivery.State) -> Unit) = onUpdate(currentState())
    fun requestInstall(onUpdate: (Clang21FeatureDelivery.State) -> Unit)
    fun requestUserConfirmation(launcher: ActivityResultLauncher<IntentSenderRequest>): Boolean
    fun cancel(onFailure: (String?) -> Unit)
}

private class DisabledClang21Delivery(private val error: String? = null) : Clang21Delivery {
    override fun currentState() = Clang21FeatureDelivery.State(
        if (error == null) Clang21FeatureDelivery.Phase.NOT_IN_BUILD else Clang21FeatureDelivery.Phase.FAILED,
        message = error
    )
    override fun requestInstall(onUpdate: (Clang21FeatureDelivery.State) -> Unit) = onUpdate(currentState())
    override fun requestUserConfirmation(launcher: ActivityResultLauncher<IntentSenderRequest>) = false
    override fun cancel(onFailure: (String?) -> Unit) = Unit
    override fun close() = Unit
}

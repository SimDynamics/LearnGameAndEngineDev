package com.learngraphicsprogramming.app.runtime

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Explicit mutable PendingIntent target; not exported to other applications. */
class Clang21InstallReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != "${context.packageName}.CLANG21_INSTALL_RESULT") return
        Clang21HttpsEngine.get(context).installationResult(intent)
    }
}

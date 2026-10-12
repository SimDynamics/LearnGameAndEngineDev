package com.learngraphicsprogramming.app

import android.content.Context
import android.view.View
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity

/** Brief notification, not a blocking dialog and not implicit saving. */
internal object UnsavedSettingsExit {
    fun install(activity: AppCompatActivity, back: View, hasChanges: () -> Boolean) {
        fun leave() {
            if (activity.isFinishing || activity.isDestroyed) return
            if (hasChanges()) notifyDiscarded(activity)
            activity.finish()
        }
        back.setOnClickListener { leave() }
        activity.onBackPressedDispatcher.addCallback(activity, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() = leave()
        })
    }
    fun notifyDiscarded(context: Context) {
        Toast.makeText(context.applicationContext, context.getString(R.string.settings_unsaved_discarded),
            Toast.LENGTH_SHORT).show()
    }
}

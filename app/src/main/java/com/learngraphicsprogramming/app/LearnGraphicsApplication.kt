package com.learngraphicsprogramming.app

import android.app.Application
import android.content.Context
import com.google.android.play.core.splitcompat.SplitCompat

/**
 * Application entry point declared by AndroidManifest.xml.
 *
 * Play Feature Delivery needs SplitCompat before the app accesses a
 * freshly installed dynamic module. The normal Clang 23 build does not
 * require the optional Clang 21 feature and keeps its startup unchanged.
 */
class LearnGraphicsApplication : Application() {

    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(base)

        if (BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED) {
            SplitCompat.install(this)
        }
    }
}

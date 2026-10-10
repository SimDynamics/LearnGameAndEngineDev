package com.learngraphicsprogramming.app

import android.app.Application
import com.learngraphicsprogramming.app.narration.KokoroNarrationSessionMonitor

/**
 * Initializes the offline narration session observer before ANY activity
 * starts. Android can restore LessonActivity directly without MainActivity.
 * Initialization does not synthesize audio or start a foreground service until
 * an app activity actually enters the started state.
 */
class LearnGraphicsApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        KokoroNarrationSessionMonitor.refresh(this)
    }
}

package com.learngraphicsprogramming.app

import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.learngraphicsprogramming.app.databinding.ActivityMainBinding
import com.learngraphicsprogramming.app.graphics.GraphicsDemoActivity
import com.learngraphicsprogramming.app.narration.KokoroBackgroundNarrationWorker
import com.learngraphicsprogramming.app.narration.FirstUnitNarrationPack

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private var restoringNavigation = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // One durable queue, enqueued on first launch and on later launches.
        // WorkManager decides when background constraints permit execution.
        KokoroBackgroundNarrationWorker.start(applicationContext)
        // Asset-to-speech manifest is independent of model installation and
        // is generated off the UI thread for offline APK packaging.
        Thread({
            runCatching { FirstUnitNarrationPack.writeManifest(applicationContext) }
                .onFailure { android.util.Log.w("KokoroFirstUnitPack", "Manifest unavailable", it) }
        }, "FirstUnitSpeechManifest").start()

        /*
         * Al iniciar la aplicación consultamos
         * las compras existentes de Google Play.
         *
         * Si Premium ya fue comprado,
         * PremiumRepository se actualizará.
         */
        PremiumBillingManager.connect(
            this
        )

        binding.buttonProgramming.setOnClickListener { openCourses(AppearanceSettingsRepository.ROUTE_PROGRAMMING) }
        binding.buttonMathPhysics.setOnClickListener { openCourses(AppearanceSettingsRepository.ROUTE_MATH_PHYSICS) }
        binding.buttonOptions.setOnClickListener { startActivity(Intent(this, OptionsActivity::class.java)) }
        binding.buttonCppPrototype.setOnClickListener { startActivity(Intent(this, CppPrototypeActivity::class.java)) }
        binding.buttonGraphicsDemo.setOnClickListener { startActivity(Intent(this, GraphicsDemoActivity::class.java)) }
        binding.buttonCalculator.setOnClickListener { startActivity(Intent(this, CalculatorActivity::class.java)) }

        if (savedInstanceState == null) restoreNavigationState()
    }

    private fun openCourses(routeId: String) {
        NavigationStateRepository.saveCourses(this, routeId)
        startActivity(Intent(this, CoursesActivity::class.java).putExtra("routeId", routeId))
    }

    /** Rebuild the actual route's parent stack once, using the same local repositories as its UI.
     * ProgressRepository owns the remembered lesson page; LessonActivity restores that separately.
     */
    private fun restoreNavigationState() {
        val state = NavigationStateRepository.getState(this)
        if (state.screen == NavigationStateRepository.SCREEN_ROUTES) return
        if (state.routeId !in setOf(AppearanceSettingsRepository.ROUTE_PROGRAMMING, AppearanceSettingsRepository.ROUTE_MATH_PHYSICS)) {
            NavigationStateRepository.saveRoutes(this)
            return
        }
        val route = state.routeId
        val courses = runCatching { CourseRepository.loadCourses(this, route) }.getOrDefault(emptyList())
        if (courses.isEmpty()) { NavigationStateRepository.saveRoutes(this); return }
        val stack = mutableListOf(Intent(this, CoursesActivity::class.java).putExtra("routeId", route))
        if (state.screen != NavigationStateRepository.SCREEN_COURSES) {
            val course = courses.firstOrNull { it.id == state.courseId }
            if (course != null) {
                stack += Intent(this, CourseActivity::class.java)
                    .putExtra("routeId", route).putExtra("courseId", course.id)
                    .putExtra("courseNumber", course.number).putExtra("courseTitle", course.title)
                if (state.screen != NavigationStateRepository.SCREEN_COURSE) {
                    val units = runCatching { UnitRepository.loadUnits(this, route, course.id) }.getOrDefault(emptyList())
                    val unit = units.firstOrNull { it.id == state.unitId }
                    if (unit != null) {
                        stack += Intent(this, UnitActivity::class.java)
                            .putExtra("routeId", route).putExtra("courseId", course.id)
                            .putExtra("unitId", unit.id).putExtra("unitNumber", unit.number).putExtra("unitTitle", unit.title)
                        if (state.screen == NavigationStateRepository.SCREEN_LESSON) {
                            val lessons = runCatching { LessonRepository.loadLessons(this, route, course.id, unit.id) }.getOrDefault(emptyList())
                            val lesson = lessons.firstOrNull { it.id == state.lessonId }
                            if (lesson != null && LessonRepository.lessonExists(this, route, lesson.id)) {
                                stack += Intent(this, LessonActivity::class.java)
                                    .putExtra("routeId", route).putExtra("courseId", course.id).putExtra("unitId", unit.id)
                                    .putExtra("lessonId", lesson.id).putExtra("lessonNumber", lesson.number).putExtra("lessonTotal", lessons.size)
                            }
                        }
                    }
                }
            }
        }
        restoringNavigation = true
        startActivities(stack.toTypedArray())
    }

    override fun onResume() {
        super.onResume()
        // Re-check after installing voices or returning from settings.
        // KEEP prevents two concurrent background curriculum jobs.
        KokoroBackgroundNarrationWorker.start(applicationContext)
        AppearanceUi.apply(this)
        // Do not overwrite the destination while it is being launched from onCreate.
        // The focused route picker is saved by onWindowFocusChanged instead.
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus && !restoringNavigation) NavigationStateRepository.saveRoutes(this)
    }

    override fun onStop() {
        super.onStop()
        restoringNavigation = false
    }
}

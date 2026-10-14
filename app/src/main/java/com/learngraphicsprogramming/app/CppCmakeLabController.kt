package com.learngraphicsprogramming.app

import android.os.Build
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import com.learngraphicsprogramming.app.runtime.CppCmakeEngine
import com.learngraphicsprogramming.app.runtime.CppCmakePhase
import com.learngraphicsprogramming.app.runtime.CppCmakeProject
import com.learngraphicsprogramming.app.runtime.CppCmakeResult
import com.learngraphicsprogramming.app.runtime.CppCompilerVariant
import org.json.JSONObject

/** Optional CMake controls. The ordinary compiler button/path is left untouched for old blocks. */
internal class CppCmakeLabController(
    private val activity: LessonActivity,
    config: JSONObject,
    private val draftKey: String,
    private val files: CppLessonProjectEditor,
    private val sourceEditor: EditText,
    compilerVariant: () -> CppCompilerVariant,
    parent: LinearLayout,
    insertBefore: View,
    private val runButton: Button,
    private val status: TextView,
    private val diagnostics: TextView,
    private val stdout: TextView,
    private val stderr: TextView,
    private val standardInput: EditText
) {
    init {
        require(config.keys().asSequence().all { it in setOf("target", "options", "buildType") })
        require(config.get("target") is String) { "CMake target must be text" }
        require(!config.has("options") || config.get("options") is JSONObject)
        require(!config.has("buildType") || config.get("buildType") is String)
    }
    private val variant = compilerVariant
    private val preferences = activity.getSharedPreferences("cpp_lesson_drafts", 0)
    private val engine = CppCmakeEngine(activity, draftKey)
    private val initialTarget = config.getString("target")
    private val initialOptions = (config.optJSONObject("options") ?: JSONObject()).let { options ->
        options.keys().asSequence().joinToString("\n") { key ->
            val value = options.get(key)
            require(value is String) { "CMake options must be text" }
            CppCmakeProject.validateOption(key, value)
            "$key=$value"
        }
    }
    private val initialType = config.optString("buildType", "Debug").also { require(it in CppCmakeProject.BUILD_TYPES) }
    private val saved = runCatching { JSONObject(preferences.getString("$draftKey/cmake", "{}")!!) }.getOrDefault(JSONObject())
    private val target = EditText(activity).apply {
        tag = "cpp_cmake_target"
        contentDescription = activity.getString(R.string.cpp_cmake_target)
        setSingleLine(true)
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        setText(saved.optString("target", initialTarget))
    }
    private val options = EditText(activity).apply {
        tag = "cpp_cmake_options"
        contentDescription = activity.getString(R.string.cpp_cmake_options)
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        maxLines = 6
        setText(saved.optString("optionsText", initialOptions))
    }
    private val buildType = Spinner(activity).apply {
        tag = "cpp_cmake_build_type"
        contentDescription = activity.getString(R.string.cpp_cmake_build_type)
        adapter = ArrayAdapter(activity, android.R.layout.simple_spinner_dropdown_item, CppCmakeProject.BUILD_TYPES)
        setSelection(CppCmakeProject.BUILD_TYPES.indexOf(saved.optString("buildType", initialType)).coerceAtLeast(0))
    }
    private val configure = Button(activity).apply {
        tag = "cpp_cmake_configure"
        text = activity.getString(R.string.cpp_cmake_configure)
        isAllCaps = false
    }
    private val build = Button(activity).apply {
        tag = "cpp_cmake_build"
        text = activity.getString(R.string.cpp_cmake_build)
        isAllCaps = false
    }
    private var busy = false
    private var displayedFingerprint: String? = null
    private var displayedInput: String? = null

    init {
        val controls = LinearLayout(activity).apply {
            tag = "cpp_cmake_controls"
            orientation = LinearLayout.VERTICAL
            fun label(title: Int) { addView(TextView(activity).apply { text = activity.getString(title); textSize = 18f }) }
            label(R.string.cpp_cmake_target); addView(target)
            label(R.string.cpp_cmake_options); addView(options)
            label(R.string.cpp_cmake_build_type); addView(buildType)
            addView(configure, LinearLayout.LayoutParams(-1, -2))
            addView(build, LinearLayout.LayoutParams(-1, -2))
        }
        parent.addView(controls, parent.indexOfChild(insertBefore).coerceAtLeast(0))
        var subscription: AutoCloseable? = null
        fun observeWhileAttached() {
            if (subscription != null) return
            subscription = engine.observe { result ->
                activity.runOnUiThread {
                    if (activity.isDestroyed || activity.isFinishing || !controls.isAttachedToWindow) return@runOnUiThread
                    if (!busy && result != null) {
                        val current = runCatching { snapshot() }.getOrNull()
                        if (current?.fingerprint == result.projectFingerprint &&
                            (result.execution == null || result.executionInput == standardInput.text.toString())) {
                            displayResult(result)
                        } else clearDisplayedResult()
                    }
                    refresh()
                }
            }
            refresh()
        }
        controls.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(view: View) { observeWhileAttached() }
            override fun onViewDetachedFromWindow(view: View) { subscription?.close(); subscription = null }
        })
        if (controls.isAttachedToWindow) observeWhileAttached()
        runButton.text = activity.getString(R.string.cpp_cmake_execute)
        val watch = object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(s: Editable?) { draftChanged() }
        }
        target.addTextChangedListener(watch)
        options.addTextChangedListener(watch)
        sourceEditor.addTextChangedListener(watch)
        standardInput.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(s: Editable?) {
                if (displayedInput != null && displayedInput != s.toString()) clearDisplayedResult()
            }
        })
        buildType.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) { draftChanged() }
        }
        configure.setOnClickListener { start(CppCmakePhase.CONFIGURATION) }
        build.setOnClickListener { start(CppCmakePhase.BUILD) }
        runButton.setOnClickListener { start(CppCmakePhase.EXECUTION) }
        if (Build.VERSION.SDK_INT < CppCmakeEngine.MIN_ANDROID_API) status.text = activity.getString(R.string.cpp_cmake_android_required)
        draftChanged()
    }

    private fun snapshot() = CppCmakeProject(files.filesSnapshot(), target.text.toString(),
        CppCmakeProject.parseOptions(options.text.toString()), CppCmakeProject.BUILD_TYPES[buildType.selectedItemPosition], variant())

    fun draftChanged() {
        preferences.edit().putString("$draftKey/cmake", JSONObject().put("target", target.text.toString())
            .put("optionsText", options.text.toString()).put("buildType", CppCmakeProject.BUILD_TYPES[buildType.selectedItemPosition]).toString()).apply()
        val project = runCatching { snapshot() }.getOrNull()
        if (displayedFingerprint != null && project?.fingerprint != displayedFingerprint) {
            clearDisplayedResult()
        }
        refresh(project)
    }

    private fun clearDisplayedResult() {
        stdout.text = ""; stderr.text = ""; diagnostics.text = ""
        if (!busy) {
            status.text = activity.getString(R.string.cpp_cmake_changed)
            status.setTag(R.id.cpp_lab_build_phase, "DRAFT_CHANGED")
        }
        displayedFingerprint = null
        displayedInput = null
        status.setTag(R.id.cpp_lab_cmake_result, null)
        status.setTag(R.id.cpp_lab_exit_code, null)
    }

    private fun refresh(project: CppCmakeProject? = runCatching { snapshot() }.getOrNull()) {
        val supported = Build.VERSION.SDK_INT >= CppCmakeEngine.MIN_ANDROID_API
        val sessionBusy = engine.isBusy()
        configure.isEnabled = supported && !busy && !sessionBusy
        build.isEnabled = supported && !busy && !sessionBusy && project != null && engine.isConfigured(project)
        runButton.isEnabled = supported && !busy && !sessionBusy && project != null && engine.hasBuiltArtifact(project, verifyContent = false)
        if (!busy && sessionBusy) {
            status.text = activity.getString(R.string.cpp_cmake_busy)
            status.setTag(R.id.cpp_lab_build_phase, "BUILDING")
        }
    }

    private fun start(phase: CppCmakePhase) {
        stdout.text = ""; stderr.text = ""; diagnostics.text = ""
        val project = runCatching { snapshot() }.getOrElse { error ->
            status.text = error.message ?: activity.getString(R.string.cpp_project_invalid)
            status.setTag(R.id.cpp_lab_build_phase, phase.name)
            return
        }
        val input = standardInput.text.toString()
        busy = true
        refresh(project)
        status.text = activity.getString(when (phase) {
            CppCmakePhase.CONFIGURATION, CppCmakePhase.GENERATION -> R.string.cpp_cmake_configuring
            CppCmakePhase.BUILD -> R.string.cpp_cmake_building
            CppCmakePhase.EXECUTION -> R.string.cpp_cmake_running
        })
        status.setTag(R.id.cpp_lab_build_phase, "BUILDING")
        status.setTag(R.id.cpp_lab_cmake_result, null)
        status.setTag(R.id.cpp_lab_exit_code, null)
        Thread {
            val result = when (phase) {
                CppCmakePhase.CONFIGURATION, CppCmakePhase.GENERATION -> engine.configure(project)
                CppCmakePhase.BUILD -> engine.build(project)
                CppCmakePhase.EXECUTION -> engine.run(project, input)
            }
            activity.runOnUiThread {
                busy = false
                // The old editor must never save over the draft of a recreated Activity.
                if (activity.isDestroyed || activity.isFinishing || !sourceEditor.isAttachedToWindow) return@runOnUiThread
                val current = runCatching { snapshot() }.getOrNull()
                if (current?.fingerprint != project.fingerprint ||
                    (phase == CppCmakePhase.EXECUTION && standardInput.text.toString() != input)) {
                    status.text = activity.getString(R.string.cpp_cmake_changed)
                    status.setTag(R.id.cpp_lab_build_phase, "DRAFT_CHANGED")
                    stdout.text = ""; stderr.text = ""; diagnostics.text = ""
                    refresh(current)
                    return@runOnUiThread
                }
                displayResult(result)
                refresh(current)
            }
        }.start()
    }

    private fun displayResult(result: CppCmakeResult) {
        displayedFingerprint = result.projectFingerprint
        displayedInput = result.executionInput
        status.setTag(R.id.cpp_lab_build_phase, result.phase.name)
        status.setTag(R.id.cpp_lab_exit_code, result.exitCode)
        status.setTag(R.id.cpp_lab_cmake_result, result)
        status.text = when {
            result.exitCode != 0 -> activity.getString(R.string.cpp_cmake_phase_failed, phaseName(result.phase), result.exitCode)
            result.phase == CppCmakePhase.GENERATION -> activity.getString(R.string.cpp_cmake_configured)
            result.phase == CppCmakePhase.BUILD && result.artifact == null -> activity.getString(R.string.cpp_cmake_built_without_program)
            result.phase == CppCmakePhase.BUILD -> activity.getString(R.string.cpp_cmake_built)
            else -> activity.getString(R.string.cpp_runtime_finished, result.exitCode)
        }
        diagnostics.text = buildString {
            append(phaseName(result.phase)); append('\n')
            append(clean(result.stdout)); append(clean(result.stderr))
            if (result.timedOut) { append('\n'); append(activity.getString(R.string.cpp_cmake_timeout)) }
            if (result.toolsInfo.isNotEmpty()) {
                append('\n'); append(result.toolsInfo.filterKeys { it in setOf("cmake", "ninja", "compiler") }
                    .values.joinToString(" · "))
            }
        }.trimEnd()
        stdout.text = result.execution?.stdout.orEmpty()
        stderr.text = result.execution?.stderr.orEmpty()
    }

    private fun clean(value: String) = value.replace(engine.workspace.absolutePath + "/project/", "")
        .replace(engine.workspace.canonicalPath + "/project/", "")

    private fun phaseName(phase: CppCmakePhase): String = activity.getString(when (phase) {
        CppCmakePhase.CONFIGURATION -> R.string.cpp_cmake_configuration_phase
        CppCmakePhase.GENERATION -> R.string.cpp_cmake_generation_phase
        CppCmakePhase.BUILD -> R.string.cpp_cmake_build_phase
        CppCmakePhase.EXECUTION -> R.string.cpp_cmake_execution_phase
    })

    fun reset() {
        target.setText(initialTarget)
        options.setText(initialOptions)
        buildType.setSelection(CppCmakeProject.BUILD_TYPES.indexOf(initialType))
        displayedFingerprint = null
        displayedInput = null
        stdout.text = ""; stderr.text = ""; diagnostics.text = ""
        status.setTag(R.id.cpp_lab_cmake_result, null)
        status.setTag(R.id.cpp_lab_exit_code, null)
        draftChanged()
    }
}

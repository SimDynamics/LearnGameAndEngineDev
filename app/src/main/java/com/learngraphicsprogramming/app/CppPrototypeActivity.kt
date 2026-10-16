package com.learngraphicsprogramming.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.res.Configuration
import androidx.appcompat.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.OnBackPressedCallback
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.learngraphicsprogramming.app.databinding.ActivityCppPrototypeBinding
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import com.learngraphicsprogramming.app.runtime.CppCompiler
import com.learngraphicsprogramming.app.runtime.CppCompilerVariant
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery
import com.learngraphicsprogramming.app.runtime.Clang21FeatureDelivery.Phase
import java.util.concurrent.Executors

class CppPrototypeActivity : AppCompatActivity() {

    private lateinit var binding:
            ActivityCppPrototypeBinding

    private val worker =
        Executors.newSingleThreadExecutor()

    private var clang21FeatureDelivery: Clang21FeatureDelivery? = null
    private var clang21ConfirmationSession: Int? = null
    private var selectedCompiler = CppCompilerVariant.DEFAULT
    private var updatingCompilerSelection = false
    private var clang21FeatureState: Clang21FeatureDelivery.State? = null
    private var compilerSettingsDialog: AlertDialog? = null
    private var cppKeyboardVisible = false
    private lateinit var editorViewport: CppEditorViewportController
    private var fullscreen = false
    private val normalChildVisibilities = mutableMapOf<View, Int>()
    private var normalScreenScrollY = 0
    private var normalSystemBars = 0
    private var normalSystemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_DEFAULT
    private lateinit var fullscreenBack: OnBackPressedCallback

    private val clang21ConfirmationLauncher =
        registerForActivityResult(
            ActivityResultContracts.StartIntentSenderForResult()
        ) { result ->
            if (isFinishing || isDestroyed) return@registerForActivityResult
            // Unknown-source settings commonly returns RESULT_CANCELED even
            // after permission was granted. The installer owns the true state.
            val state = clang21FeatureDelivery?.currentState()
            if (state != null) updateClang21FeaturePanel(state)
            if (result.resultCode != RESULT_OK &&
                state?.phase == Phase.NEEDS_CONFIRMATION && state.sessionId != -1) {
                binding.clang21FeatureStatus.setText(
                    R.string.clang21_confirmation_rejected
                )
            }
        }

    private var lineWrapEnabled =
        false

    private var statusResource = R.string.cpp_runtime_ready
    private var statusExitCode: Int? = null
    private var rawOutput = ""
    private var compilationTimedOut = false

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        super.onCreate(
            savedInstanceState
        )

        binding =
            ActivityCppPrototypeBinding.inflate(
                layoutInflater
            )

        setContentView(
            binding.root
        )

        // Source, selection and scroll are restored from our Bundle; avoid
        // a second native restoration of the Editable and its formatting spans.
        binding.sourceCode.isSaveEnabled = false

        // Reutiliza exactamente las reglas de las consolas y el editor de página 8.
        CppCodeInputRules.install(binding.sourceCode)
        editorViewport = CppEditorViewportController(
            binding.sourceCode, binding.lineNumbers, binding.lineNumberScroll,
            binding.codeEditorBody, binding.editorVerticalBar, binding.editorHorizontalBar,
            { fullscreen }, { cppKeyboardVisible }
        )

        styleCodeCard()

        makeCodeCardFullWidth()

        if (
            savedInstanceState == null
        ) {

            resetEditor()

        } else {

            binding.sourceCode.setText(
                savedInstanceState.getString(
                    STATE_SOURCE
                )
                    ?: SAMPLE_SOURCE
            )

            setStatus(
                savedInstanceState.getInt(STATE_STATUS, R.string.cpp_runtime_ready),
                savedInstanceState.takeIf { it.containsKey(STATE_EXIT_CODE) }?.getInt(STATE_EXIT_CODE)
            )

            rawOutput = savedInstanceState.getString(STATE_OUTPUT).orEmpty()
            compilationTimedOut = savedInstanceState.getBoolean(STATE_COMPILATION_TIMED_OUT)
            showOutput()

            lineWrapEnabled =
                savedInstanceState.getBoolean(
                    STATE_WRAP,
                    false
                )
        }

        binding.sourceCode.addTextChangedListener(
            object : TextWatcher {

                override fun beforeTextChanged(
                    text: CharSequence?,
                    start: Int,
                    count: Int,
                    after: Int
                ) {
                }

                override fun onTextChanged(
                    text: CharSequence?,
                    start: Int,
                    before: Int,
                    count: Int
                ) {

                    updateLineNumbers()
                }

                override fun afterTextChanged(
                    text: Editable?
                ) {

                    if (
                        text == null
                    ) {
                        return
                    }

                    CppSyntaxHighlighter.applyTo(
                        text
                    )

                    compactEditorSpaces(
                        text
                    )
                }
            }
        )

        binding.sourceCode.text?.let { editable ->

            CppSyntaxHighlighter.applyTo(
                editable
            )

            compactEditorSpaces(
                editable
            )
        }

        updateLineNumbers()

        updateWrapMode()

        binding.resetCodeButton.setOnClickListener {

            resetEditor()
        }

        binding.alignCodeButton.setOnClickListener {

            lineWrapEnabled =
                !lineWrapEnabled

            updateWrapMode()
        }

        binding.copyCodeButton.setOnClickListener {

            copySourceCode()
        }

        binding.runButton.setOnClickListener {

            runPrototype()
        }

        selectedCompiler =
            if (BuildConfig.LGP_CLANG23_ENABLED &&
                savedInstanceState?.getBoolean(STATE_CLANG21_SELECTED) == true)
                CppCompilerVariant.OPTIONAL_CLANG_21
            else
                CppCompilerVariant.DEFAULT

        setupClang21FeaturePanel()
        setupCppKeyboardShortcuts()
        setupFullscreen()
        binding.sourceCode.post {
            val textLength = binding.sourceCode.text?.length ?: 0
            val start = savedInstanceState?.getInt(STATE_SELECTION_START) ?: binding.sourceCode.selectionStart
            val end = savedInstanceState?.getInt(STATE_SELECTION_END) ?: binding.sourceCode.selectionEnd
            binding.sourceCode.setSelection(start.coerceIn(0, textLength), end.coerceIn(0, textLength))
            editorViewport.refresh()
            if (savedInstanceState?.getBoolean(STATE_FULLSCREEN) == true) setFullscreen(true, savedInstanceState)
            binding.sourceCode.post {
                updateCompilerAvailability()
                binding.sourceCode.scrollTo(
                    savedInstanceState?.getInt(STATE_SCROLL_X) ?: 0,
                    savedInstanceState?.getInt(STATE_SCROLL_Y) ?: 0
                )
            }
        }
    }

    override fun onRestoreInstanceState(savedInstanceState: Bundle) {
        // RadioGroup restores its hierarchy after onCreate. That callback is
        // not a user choice: the explicit saved compiler remains authoritative.
        val restoredCompiler = selectedCompiler
        val wasUpdatingSelection = updatingCompilerSelection
        updatingCompilerSelection = true
        try {
            super.onRestoreInstanceState(savedInstanceState)
        } finally {
            selectedCompiler = restoredCompiler
            updatingCompilerSelection = wasUpdatingSelection
        }
        updateCompilerAvailability()
    }

    private fun setupFullscreen() {
        fullscreenBack = object : OnBackPressedCallback(false) {
            override fun handleOnBackPressed() { setFullscreen(false) }
        }
        onBackPressedDispatcher.addCallback(this, fullscreenBack)
        binding.fullscreenCodeButton.setOnClickListener { setFullscreen(!fullscreen) }
        binding.compilerScroll.addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
            updateEditorGeometry()
        }
    }

    private fun setFullscreen(enabled: Boolean, restoredState: Bundle? = null) {
        if (fullscreen == enabled) return
        fullscreen = enabled
        fullscreenBack.isEnabled = enabled
        val bars = WindowInsetsCompat.Type.systemBars()
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        if (enabled) {
            normalScreenScrollY = restoredState?.getInt(STATE_NORMAL_SCREEN_SCROLL, binding.compilerScroll.scrollY)
                ?: binding.compilerScroll.scrollY
            normalSystemBarsBehavior = restoredState?.getInt(STATE_NORMAL_BARS_BEHAVIOR, controller.systemBarsBehavior)
                ?: controller.systemBarsBehavior
            normalChildVisibilities.clear()
            for (index in 0 until binding.compilerContent.childCount) {
                val child = binding.compilerContent.getChildAt(index)
                normalChildVisibilities[child] = child.visibility
                if (child !== binding.codeCard) child.visibility = View.GONE
            }
            val insets = ViewCompat.getRootWindowInsets(binding.root)
            normalSystemBars = (if (insets?.isVisible(WindowInsetsCompat.Type.statusBars()) == true)
                WindowInsetsCompat.Type.statusBars() else 0) or
                (if (insets?.isVisible(WindowInsetsCompat.Type.navigationBars()) == true)
                    WindowInsetsCompat.Type.navigationBars() else 0)
            normalSystemBars = restoredState?.getInt(STATE_NORMAL_BARS, normalSystemBars) ?: normalSystemBars
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            controller.hide(bars)
            binding.compilerContent.setPadding(0, 0, 0, 0)
            binding.codeCard.translationX = 0f
            binding.compilerScroll.scrollTo(0, 0)
        } else {
            normalChildVisibilities.forEach { (view, visibility) -> view.visibility = visibility }
            normalChildVisibilities.clear()
            controller.systemBarsBehavior = normalSystemBarsBehavior
            controller.hide(bars and normalSystemBars.inv())
            controller.show(normalSystemBars)
            binding.compilerContent.setPadding(dp(24), dp(24), dp(24), dp(24))
            makeCodeCardFullWidth()
            binding.compilerScroll.post { binding.compilerScroll.scrollTo(0, normalScreenScrollY) }
        }
        binding.fullscreenCodeButton.contentDescription = getString(
            if (enabled) R.string.exercise_exit_fullscreen else R.string.exercise_fullscreen
        )
        updateEditorGeometry()
        binding.sourceCode.post { editorViewport.refresh() }
    }

    private fun updateEditorGeometry() {
        if (!::editorViewport.isInitialized) return
        val landscapeIme = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE && cppKeyboardVisible
        binding.codeHeader.visibility = if (fullscreen && landscapeIme) View.GONE else View.VISIBLE
        val top = if (landscapeIme) 0 else dp(16)
        if (binding.sourceCode.paddingTop != top) {
            binding.sourceCode.setPadding(dp(10), top, dp(16), binding.sourceCode.paddingBottom)
        }
        if (binding.lineNumbers.paddingTop != top) {
            binding.lineNumbers.setPadding(0, top, dp(3), binding.lineNumbers.paddingBottom)
        }
        val card = binding.codeCard.layoutParams as LinearLayout.LayoutParams
        val cardHeight = if (fullscreen && binding.compilerScroll.height > 0)
            binding.compilerScroll.height else ViewGroup.LayoutParams.WRAP_CONTENT
        val margin = if (fullscreen) 0 else dp(24)
        if (card.height != cardHeight || card.topMargin != margin) {
            card.width = ViewGroup.LayoutParams.MATCH_PARENT
            card.height = cardHeight
            card.topMargin = margin
            binding.codeCard.layoutParams = card
        }
        if (fullscreen) {
            val body = binding.codeEditorBody.layoutParams as LinearLayout.LayoutParams
            if (body.height != 0 || body.weight != 1f) {
                body.height = 0
                body.weight = 1f
                binding.codeEditorBody.layoutParams = body
            }
            // Retain focus and selection while reclaiming the resized IME viewport.
            if (binding.codeEditorBody.minimumHeight != 0) binding.codeEditorBody.minimumHeight = 0
        }
        val keyboard = binding.programmingKeyboardBar.layoutParams
        val barHeight = dp(if (landscapeIme) 32 else 52)
        if (keyboard.height != barHeight) {
            keyboard.height = barHeight
            binding.programmingKeyboardBar.layoutParams = keyboard
        }
        for (index in 0 until binding.programmingKeyboardBarContent.childCount) {
            val key = binding.programmingKeyboardBarContent.getChildAt(index) as TextView
            val height = dp(if (landscapeIme) 28 else 40)
            if (key.layoutParams.height != height) {
                key.minimumHeight = height
                key.layoutParams = key.layoutParams.apply { this.height = height }
                key.textSize = if (landscapeIme) 14f else 16f
                key.setPadding(dp(10), dp(if (landscapeIme) 3 else 6), dp(10), dp(if (landscapeIme) 3 else 6))
            }
        }
        editorViewport.refresh()
        if (cppKeyboardVisible && binding.sourceCode.hasFocus() && !binding.sourceCode.suppressCursorAutoScroll) {
            editorViewport.ensureCursorVisible()
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        binding.compilerScroll.post {
            if (!fullscreen) makeCodeCardFullWidth()
            updateEditorGeometry()
        }
    }

    /**
     * The standalone compiler shares the lesson's saved programming shortcuts,
     * including the fixed four-space TAB key. The bottom bar occupies the
     * resized window above the IME instead of covering the source editor.
     */
    private fun setupCppKeyboardShortcuts() {
        rebuildCppKeyboardShortcuts()

        binding.sourceCode.setOnFocusChangeListener { _, _ ->
            updateCppKeyboardShortcutVisibility()
        }

        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { view, insets ->
            cppKeyboardVisible = insets.isVisible(WindowInsetsCompat.Type.ime())
            updateCppKeyboardShortcutVisibility()
            updateEditorGeometry()
            ViewCompat.onApplyWindowInsets(view, insets)
        }

        // Fallback for devices whose IME inset changes only with the layout.
        binding.root.viewTreeObserver.addOnGlobalLayoutListener {
            val visible = ViewCompat.getRootWindowInsets(binding.root)
                ?.isVisible(WindowInsetsCompat.Type.ime()) == true
            if (visible != cppKeyboardVisible) {
                cppKeyboardVisible = visible
                updateCppKeyboardShortcutVisibility()
                updateEditorGeometry()
            }
        }
        ViewCompat.requestApplyInsets(binding.root)
    }

    private fun rebuildCppKeyboardShortcuts() {
        binding.programmingKeyboardBarContent.removeAllViews()
        addCppKeyboardShortcut(
            getString(R.string.lesson_tab_label),
            KeyboardShortcutSettingsRepository.getTabText()
        )
        KeyboardShortcutSettingsRepository.getShortcuts(this).forEach { shortcut ->
            addCppKeyboardShortcut(shortcut, shortcut)
        }
    }

    private fun addCppKeyboardShortcut(label: String, insertedText: String) {
        val shortcut = TextView(this).apply {
            text = label
            typeface = Typeface.MONOSPACE
            textSize = 16f
            gravity = Gravity.CENTER
            setTextColor(Color.WHITE)
            minWidth = dp(
                if (label == getString(R.string.lesson_tab_label)) 56 else 40
            )
            minimumHeight = dp(40)
            setPadding(dp(10), dp(6), dp(10), dp(6))
            isFocusable = false
            isFocusableInTouchMode = false
            isClickable = true
            background = GradientDrawable().apply {
                setColor(Color.rgb(37, 40, 46))
                cornerRadius = dp(8).toFloat()
                setStroke(
                    dp(1),
                    AppearanceSettingsRepository.getRouteAccentColor(
                        this@CppPrototypeActivity,
                        AppearanceSettingsRepository.ROUTE_PROGRAMMING
                    )
                )
            }
            setOnClickListener { insertCppKeyboardShortcut(insertedText) }
        }
        val params = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT,
            dp(40)
        ).apply { setMargins(dp(3), 0, dp(3), 0) }
        binding.programmingKeyboardBarContent.addView(shortcut, params)
    }

    private fun insertCppKeyboardShortcut(text: String) {
        val input = binding.sourceCode
        val editable = input.text ?: return
        val start = minOf(
            input.selectionStart.coerceAtLeast(0),
            input.selectionEnd.coerceAtLeast(0)
        )
        val end = maxOf(
            input.selectionStart.coerceAtLeast(0),
            input.selectionEnd.coerceAtLeast(0)
        )
        editable.replace(start, end, text)
        input.setSelection(start + text.length)
    }

    private fun updateCppKeyboardShortcutVisibility() {
        binding.programmingKeyboardBar.visibility =
            if (cppKeyboardVisible && binding.sourceCode.hasFocus())
                View.VISIBLE
            else
                View.GONE
    }

    /**
     * Merely installing Clang 21 does not switch compilers. The user must
     * choose it explicitly, and only executable system-installed native
     * libraries may back an external clang process.
     */
    private fun setupClang21FeaturePanel() {
        binding.clangCompilerSettingsHost.visibility = View.GONE
        binding.clangCompilerMenuButton.setOnClickListener { showCompilerSettings() }
        binding.clangCompilerSelectorSection.visibility = View.VISIBLE
        binding.clang23RadioButton.setText(
            if (BuildConfig.LGP_CLANG23_ENABLED) R.string.clang_compiler_23
            else R.string.clang_compiler_23_name
        )
        binding.clang21RadioButton.setText(
            if (BuildConfig.LGP_CLANG23_ENABLED) R.string.clang_compiler_21
            else R.string.clang_compiler_21_default
        )
        binding.clangCompilerRadioGroup.setOnCheckedChangeListener { _, checkedId ->
            if (!updatingCompilerSelection) {
                when (checkedId) {
                    R.id.clang21RadioButton -> if (binding.clang21RadioButton.isEnabled) {
                        selectedCompiler = if (BuildConfig.LGP_CLANG23_ENABLED)
                            CppCompilerVariant.OPTIONAL_CLANG_21 else CppCompilerVariant.DEFAULT
                    }
                    R.id.clang23RadioButton -> if (binding.clang23RadioButton.isEnabled) {
                        selectedCompiler = CppCompilerVariant.DEFAULT
                    }
                }
                updateCompilerAvailability()
            }
        }
        if (!BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED) {
            binding.clang21FeatureSection.visibility = View.GONE
            updateCompilerAvailability()
            return
        }

        binding.clang21FeatureSection.visibility = View.VISIBLE
        val installer = Clang21FeatureDelivery(this)
        clang21FeatureDelivery = installer
        installer.observe { state ->
            if (!isFinishing && !isDestroyed) updateClang21FeaturePanel(state)
        }
        updateClang21FeaturePanel(installer.currentState())

        binding.clang21DownloadButton.setOnClickListener {
            AlertDialog.Builder(this)
                .setTitle(R.string.clang21_confirm_title)
                .setMessage(if (BuildConfig.LGP_CLANG21_DELIVERY_PROVIDER == "https")
                    getString(R.string.clang21_https_confirm_message,
                        installer.currentState().totalBytes / 1_048_576.0)
                    else getString(R.string.clang21_confirm_message))
                .setNegativeButton(R.string.clang21_confirm_no, null)
                .setPositiveButton(R.string.clang21_confirm_yes) { _, _ ->
                    installer.requestInstall { state ->
                        runOnUiThread {
                            if (!isFinishing && !isDestroyed) {
                                updateClang21FeaturePanel(state)
                            }
                        }
                    }
                }
                .show()
        }

        binding.clang21CancelButton.setOnClickListener {
            binding.clang21FeatureStatus.setText(R.string.clang21_canceling)
            installer.cancel { reason ->
                runOnUiThread {
                    if (!isFinishing && !isDestroyed) {
                        binding.clang21FeatureStatus.text = getString(
                            R.string.clang21_cancel_failure,
                            reason ?: getString(R.string.clang21_failure_unknown)
                        )
                    }
                }
            }
        }
    }

    /** Reuse the existing radio group and installer controls, including their live state. */
    private fun showCompilerSettings() {
        // onDismiss is queued: its original host must return home before reopening.
        if (compilerSettingsDialog != null || isFinishing || isDestroyed) return
        updateCompilerAvailability()
        val host = binding.clangCompilerSettingsHost
        val home = host.parent as? ViewGroup ?: return
        val index = home.indexOfChild(host)
        val homeLayout = host.layoutParams
        home.removeView(host)
        host.visibility = View.VISIBLE
        val scroll = ScrollView(this).apply {
            addView(host, android.widget.FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        val dialog = AlertDialog.Builder(this)
            .setView(scroll)
            .setPositiveButton(android.R.string.ok, null)
            .create()
        compilerSettingsDialog = dialog
        dialog.setOnDismissListener {
            (host.parent as? ViewGroup)?.removeView(host)
            host.visibility = View.GONE
            home.addView(host, index.coerceIn(0, home.childCount), homeLayout)
            if (compilerSettingsDialog === dialog) compilerSettingsDialog = null
        }
        dialog.show()
        dialog.window?.setBackgroundDrawable(GradientDrawable().apply {
            setColor(Color.rgb(28, 31, 36))
            cornerRadius = 12f * resources.displayMetrics.density
        })
    }

    private fun updateClang21FeaturePanel(state: Clang21FeatureDelivery.State) {
        clang21FeatureState = state
        val phase = state.phase
        val canDownload = phase == Phase.NOT_INSTALLED ||
            phase == Phase.FAILED || phase == Phase.CANCELED
        val active = phase == Phase.REQUESTED || phase == Phase.PENDING ||
            phase == Phase.DOWNLOADING || phase == Phase.DOWNLOADED ||
            phase == Phase.INSTALLING || phase == Phase.NEEDS_CONFIRMATION ||
            phase == Phase.CANCELING

        binding.clang21DownloadButton.isEnabled = canDownload
        binding.clang21CancelButton.visibility =
            if (active && state.sessionId != null) View.VISIBLE else View.GONE
        binding.clang21FeatureProgress.visibility =
            if (active) View.VISIBLE else View.GONE

        val total = state.totalBytes
        val downloaded = state.downloadedBytes
        val determinate = phase == Phase.DOWNLOADING && total > 0L
        binding.clang21FeatureProgress.isIndeterminate = !determinate
        if (determinate) {
            binding.clang21FeatureProgress.progress =
                ((downloaded.coerceIn(0L, total) * 100L) / total).toInt()
        }

        val status = when (phase) {
            Phase.NOT_IN_BUILD, Phase.NOT_INSTALLED ->
                getString(R.string.clang21_not_installed)
            Phase.REQUESTED -> getString(R.string.clang21_requesting)
            Phase.PENDING -> getString(if (BuildConfig.LGP_CLANG21_DELIVERY_PROVIDER == "https")
                R.string.clang21_https_pending else R.string.clang21_pending)
            Phase.DOWNLOADING -> {
                if (total > 0L) {
                    getString(
                        R.string.clang21_downloading,
                        ((downloaded.coerceIn(0L, total) * 100L) / total).toInt()
                    )
                } else {
                    getString(R.string.clang21_requesting)
                }
            }
            Phase.DOWNLOADED -> getString(if (BuildConfig.LGP_CLANG21_DELIVERY_PROVIDER == "https")
                R.string.clang21_https_verifying else R.string.clang21_downloaded)
            Phase.INSTALLING -> getString(R.string.clang21_installing)
            Phase.NEEDS_CONFIRMATION -> getString(if (state.sessionId == -1)
                R.string.clang21_https_permission else R.string.clang21_confirming)
            Phase.INSTALLED ->
                if (CppCompiler.isOptionalClang21Available(this))
                    getString(R.string.clang21_ready)
                else
                    getString(R.string.clang21_installed)
            Phase.EMULATED -> getString(R.string.clang21_local_emulation)
            Phase.FAILED -> getString(
                R.string.clang21_failure,
                state.errorCode?.toString()
                    ?: state.message
                    ?: getString(R.string.clang21_failure_unknown)
            )
            Phase.CANCELING -> getString(R.string.clang21_canceling)
            Phase.CANCELED -> getString(R.string.clang21_canceled)
            Phase.UNKNOWN -> getString(R.string.clang21_other_status)
        }
        binding.clang21FeatureStatus.text = status
        updateCompilerAvailability()

        if (phase == Phase.NEEDS_CONFIRMATION &&
            state.sessionId != clang21ConfirmationSession
        ) {
            clang21ConfirmationSession = state.sessionId
            val shown = runCatching {
                clang21FeatureDelivery?.requestUserConfirmation(
                    clang21ConfirmationLauncher
                ) == true
            }.getOrDefault(false)
            if (!shown) {
                binding.clang21FeatureStatus.setText(
                    R.string.clang21_confirmation_unavailable
                )
            }
        }
        if (phase == Phase.INSTALLED || phase == Phase.FAILED ||
            phase == Phase.CANCELED
        ) {
            clang21ConfirmationSession = null
        }
    }

    private fun updateCompilerAvailability() {
        val available = CppCompiler.isClang21Available(this)
        binding.clang21RadioButton.isEnabled = available && binding.runButton.isEnabled
        binding.clang23RadioButton.isEnabled = BuildConfig.LGP_CLANG23_ENABLED && binding.runButton.isEnabled
        binding.clang21ExecutionUnavailable.visibility =
            if (clang21FeatureState?.phase == Phase.INSTALLED && !available) View.VISIBLE else View.GONE

        binding.clangCompilerAvailability.setText(when {
            selectedCompiler == CppCompilerVariant.OPTIONAL_CLANG_21 && !available ->
                R.string.clang21_selected_unavailable
            available -> R.string.clang21_ready
            !BuildConfig.LGP_CLANG23_ENABLED -> R.string.math_ui_clang_missing
            !BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED -> R.string.clang21_unavailable_in_build
            clang21FeatureState?.phase == Phase.EMULATED -> R.string.clang21_local_emulation
            clang21FeatureState?.phase == Phase.INSTALLED -> R.string.clang21_installed_unavailable
            else -> R.string.clang21_not_installed
        })

        // Availability can change after installation or Activity recreation;
        // never replace a restored Clang 21 choice with Clang 23 silently.
        val wasUpdatingSelection = updatingCompilerSelection
        updatingCompilerSelection = true
        try {
            binding.clangCompilerRadioGroup.check(
                if (!BuildConfig.LGP_CLANG23_ENABLED ||
                    selectedCompiler == CppCompilerVariant.OPTIONAL_CLANG_21)
                    R.id.clang21RadioButton else R.id.clang23RadioButton
            )
        } finally { updatingCompilerSelection = wasUpdatingSelection }
    }

    override fun onResume() {
        super.onResume()
        val installer = clang21FeatureDelivery
        if (installer != null && clang21FeatureState?.phase !in setOf(
                Phase.REQUESTED, Phase.PENDING, Phase.DOWNLOADING, Phase.DOWNLOADED,
                Phase.INSTALLING, Phase.CANCELING
            )) {
            updateClang21FeaturePanel(installer.currentState())
        } else {
            updateCompilerAvailability()
        }
    }

    private fun compactEditorSpaces(
        text: Editable
    ) {

        val previousSpans =
            text.getSpans(
                0,
                text.length,
                android.text.style.ScaleXSpan::class.java
            )

        for (span in previousSpans) {

            text.removeSpan(
                span
            )
        }

        for (i in 0 until text.length) {

            if (text[i] == ' ') {

                text.setSpan(
                    android.text.style.ScaleXSpan(
                        0.80f
                    ),
                    i,
                    i + 1,
                    android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            }
        }
    }

    private fun styleCodeCard() {

        val background =
            GradientDrawable()

        background.setColor(
            Color.rgb(
                28,
                31,
                36
            )
        )

        background.cornerRadius =
            dp(12).toFloat()

        binding.codeCard.background =
            background

        binding.codeCard.clipToOutline =
            true
    }

    private fun makeCodeCardFullWidth() {

        binding.codeCard.post {

            if (fullscreen) return@post

            val parent =
                binding.codeCard.parent
                        as? ViewGroup
                    ?: return@post

            val params =
                binding.codeCard.layoutParams

            params.width =
                parent.width

            binding.codeCard.layoutParams =
                params

            binding.codeCard.translationX =
                -parent.paddingLeft.toFloat()
        }
    }

    private fun resetEditor() {

        binding.sourceCode.setText(
            SAMPLE_SOURCE
        )

        /*
         * Colocamos el cursor dentro de main,
         * justo después de los cuatro espacios.
         */
        val cursorPosition =
            SAMPLE_SOURCE.indexOf(
                FOUR_SPACE_LINE
            ) + 4

        if (
            cursorPosition >= 4
        ) {

            binding.sourceCode.setSelection(
                cursorPosition
            )
        }

        setStatus(
            R.string.cpp_runtime_ready
        )

        rawOutput = ""
        compilationTimedOut = false
        showOutput()

        lineWrapEnabled =
            false

        updateWrapMode()

        updateLineNumbers()
    }

    private fun updateWrapMode() {

        if (::editorViewport.isInitialized) editorViewport.setWrapEnabled(lineWrapEnabled)

        if (
            lineWrapEnabled
        ) {

            binding.sourceCode.setHorizontallyScrolling(
                false
            )

            binding.alignCodeButton
                .setCompoundDrawablesWithIntrinsicBounds(
                    R.drawable.ic_code_wrap_on,
                    0,
                    0,
                    0
                )

            binding.alignCodeButton.contentDescription =
                getString(R.string.exercise_wrap_enabled)

        } else {

            binding.sourceCode.setHorizontallyScrolling(
                true
            )

            binding.alignCodeButton
                .setCompoundDrawablesWithIntrinsicBounds(
                    R.drawable.ic_code_wrap_off,
                    0,
                    0,
                    0
                )

            binding.alignCodeButton.contentDescription =
                getString(R.string.exercise_wrap_disabled)
        }
    }

    private fun copySourceCode() {

        val clipboard =
            getSystemService(
                Context.CLIPBOARD_SERVICE
            ) as ClipboardManager

        clipboard.setPrimaryClip(
            ClipData.newPlainText(
                getString(R.string.exercise_cpp_code),
                binding.sourceCode.text.toString()
            )
        )

        Toast.makeText(
            this,
            R.string.code_copied,
            Toast.LENGTH_SHORT
        ).show()
    }

    private fun updateLineNumbers() {
        if (::editorViewport.isInitialized) editorViewport.updateLineNumbers()
    }

    private fun runPrototype() {

        if (selectedCompiler == CppCompilerVariant.OPTIONAL_CLANG_21 &&
            !CppCompiler.isOptionalClang21Available(this)) {
            updateCompilerAvailability()
            Toast.makeText(this, R.string.clang21_selected_unavailable, Toast.LENGTH_LONG).show()
            return
        }

        val source =
            binding.sourceCode.text.toString()
        val compilerForThisRun = selectedCompiler

        binding.runButton.isEnabled =
            false
        binding.clang23RadioButton.isEnabled = false
        binding.clang21RadioButton.isEnabled = false

        setStatus(
            R.string.cpp_runtime_running
        )

        rawOutput = ""
        compilationTimedOut = false
        showOutput()

        worker.execute {

            val result =
                runCatching {

                    CppExecutionEngine(
                        this
                    ).compileAndRun(
                        source,
                        compilerVariant = compilerForThisRun
                    )
                }

            runOnUiThread {

                if (isFinishing || isDestroyed) return@runOnUiThread

                binding.runButton.isEnabled =
                    true
                updateCompilerAvailability()

                result.onSuccess { engineResult ->

                    val compilation =
                        engineResult.compilation

                    val execution =
                        engineResult.execution

                    if (
                            execution == null
                        ) {

                            setStatus(
                                R.string.cpp_compilation_failed,
                                compilation.exitCode
                            )

                        } else {

                            setStatus(
                                R.string.cpp_runtime_finished,
                                execution.exitCode
                            )
                        }

                    rawOutput =
                        buildString {

                            append(
                                compilation.stdout
                            )

                            append(
                                compilation.stderr
                            )

                            if (
                                execution != null
                            ) {

                                append(
                                    execution.stdout
                                )

                                append(
                                    execution.stderr
                                )
                            }
                        }.trimEnd()
                    compilationTimedOut = compilation.timedOut
                    showOutput()
                }

                result.onFailure { error ->

                    setStatus(
                        R.string.cpp_runtime_failed
                    )

                    rawOutput = ""
                    compilationTimedOut = false
                    showOutput()
                }
            }
        }
    }

    override fun onSaveInstanceState(
        outState: Bundle
    ) {

        outState.putString(
            STATE_SOURCE,
            binding.sourceCode.text.toString()
        )

        outState.putInt(
            STATE_STATUS,
            statusResource
        )
        statusExitCode?.let { outState.putInt(STATE_EXIT_CODE, it) }

        outState.putString(
            STATE_OUTPUT,
            rawOutput
        )
        outState.putBoolean(STATE_COMPILATION_TIMED_OUT, compilationTimedOut)
        outState.putBoolean(
            STATE_CLANG21_SELECTED,
            selectedCompiler == CppCompilerVariant.OPTIONAL_CLANG_21
        )

        outState.putBoolean(
            STATE_WRAP,
            lineWrapEnabled
        )
        outState.putBoolean(STATE_FULLSCREEN, fullscreen)
        outState.putInt(STATE_SELECTION_START, binding.sourceCode.selectionStart)
        outState.putInt(STATE_SELECTION_END, binding.sourceCode.selectionEnd)
        outState.putInt(STATE_SCROLL_X, binding.sourceCode.scrollX)
        outState.putInt(STATE_SCROLL_Y, binding.sourceCode.scrollY)
        outState.putInt(STATE_NORMAL_SCREEN_SCROLL, normalScreenScrollY)
        outState.putInt(STATE_NORMAL_BARS, normalSystemBars)
        outState.putInt(STATE_NORMAL_BARS_BEHAVIOR, normalSystemBarsBehavior)

        super.onSaveInstanceState(
            outState
        )
    }

    override fun onDestroy() {

        compilerSettingsDialog?.dismiss()
        compilerSettingsDialog = null
        if (::editorViewport.isInitialized) editorViewport.dispose()
        clang21FeatureDelivery?.close()
        clang21FeatureDelivery = null
        worker.shutdownNow()

        super.onDestroy()
    }

    private fun dp(
        value: Int
    ): Int {

        return (
                value *
                        resources.displayMetrics.density
                ).toInt()
    }

    private fun setStatus(resource: Int, exitCode: Int? = null) {
        statusResource = resource
        statusExitCode = exitCode
        binding.statusText.text = if (exitCode == null) getString(resource) else getString(resource, exitCode)
    }

    private fun showOutput() {
        binding.outputText.text = when {
            statusResource == R.string.cpp_runtime_failed -> getString(R.string.math_ui_runtime_error)
            compilationTimedOut -> listOf(rawOutput, getString(R.string.math_ui_compiler_timeout))
                .filter { it.isNotEmpty() }.joinToString("\n")
            else -> rawOutput
        }
    }

    private companion object {

        const val STATE_SOURCE =
            "cpp_prototype_source"

        const val STATE_STATUS =
            "cpp_prototype_status"

        const val STATE_EXIT_CODE = "cpp_prototype_exit_code"
        const val STATE_COMPILATION_TIMED_OUT = "cpp_prototype_compilation_timed_out"

        const val STATE_OUTPUT =
            "cpp_prototype_output"

        const val STATE_WRAP =
            "cpp_prototype_wrap"

        const val STATE_CLANG21_SELECTED =
            "cpp_prototype_clang21_selected"
        const val STATE_FULLSCREEN = "cpp_prototype_fullscreen"
        const val STATE_SELECTION_START = "cpp_prototype_selection_start"
        const val STATE_SELECTION_END = "cpp_prototype_selection_end"
        const val STATE_SCROLL_X = "cpp_prototype_scroll_x"
        const val STATE_SCROLL_Y = "cpp_prototype_scroll_y"
        const val STATE_NORMAL_SCREEN_SCROLL = "cpp_prototype_normal_screen_scroll"
        const val STATE_NORMAL_BARS = "cpp_prototype_normal_bars"
        const val STATE_NORMAL_BARS_BEHAVIOR = "cpp_prototype_normal_bars_behavior"

        const val FOUR_SPACE_LINE =
            "\n    \n"

        const val SAMPLE_SOURCE =
            """#include <iostream>

using namespace std;

int main()
{
    
}"""
    }
}

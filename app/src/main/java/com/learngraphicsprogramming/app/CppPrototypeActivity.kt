package com.learngraphicsprogramming.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.appcompat.app.AlertDialog
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.activity.result.contract.ActivityResultContracts
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

    private val clang21ConfirmationLauncher =
        registerForActivityResult(
            ActivityResultContracts.StartIntentSenderForResult()
        ) { result ->
            if (result.resultCode != RESULT_OK && !isFinishing && !isDestroyed) {
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
            if (savedInstanceState?.getBoolean(STATE_CLANG21_SELECTED) == true)
                CppCompilerVariant.OPTIONAL_CLANG_21
            else
                CppCompilerVariant.DEFAULT

        setupClang21FeaturePanel()
    }

    /**
     * Merely installing Clang 21 does not switch compilers. The user must
     * choose it explicitly, and only executable system-installed native
     * libraries may back an external clang process.
     */
    private fun setupClang21FeaturePanel() {
        if (!BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED) {
            binding.clang21FeatureSection.visibility = View.GONE
            binding.clangCompilerSelectorSection.visibility = View.GONE
            return
        }

        binding.clang21FeatureSection.visibility = View.VISIBLE
        binding.clangCompilerSelectorSection.visibility = View.VISIBLE
        val installer = Clang21FeatureDelivery(this)
        clang21FeatureDelivery = installer
        updateClang21FeaturePanel(installer.currentState())

        binding.clangCompilerRadioGroup.setOnCheckedChangeListener { _, checkedId ->
            selectedCompiler =
                if (checkedId == R.id.clang21RadioButton &&
                    binding.clang21RadioButton.isEnabled
                ) CppCompilerVariant.OPTIONAL_CLANG_21
                else CppCompilerVariant.DEFAULT
        }

        binding.clang21DownloadButton.setOnClickListener {
            AlertDialog.Builder(this)
                .setTitle(R.string.clang21_confirm_title)
                .setMessage(R.string.clang21_confirm_message)
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

    private fun updateClang21FeaturePanel(state: Clang21FeatureDelivery.State) {
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
            Phase.PENDING -> getString(R.string.clang21_pending)
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
            Phase.DOWNLOADED -> getString(R.string.clang21_downloaded)
            Phase.INSTALLING -> getString(R.string.clang21_installing)
            Phase.NEEDS_CONFIRMATION -> getString(R.string.clang21_confirming)
            Phase.INSTALLED -> getString(R.string.clang21_installed)
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
        updateCompilerAvailability(phase == Phase.INSTALLED)

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

    private fun updateCompilerAvailability(moduleInstalled: Boolean) {
        val available = moduleInstalled &&
            CppCompiler.isOptionalClang21Available(this)
        binding.clang21RadioButton.isEnabled = available && binding.runButton.isEnabled
        binding.clang23RadioButton.isEnabled = binding.runButton.isEnabled
        binding.clang21ExecutionUnavailable.visibility =
            if (moduleInstalled && !available) View.VISIBLE else View.GONE

        if (!available) selectedCompiler = CppCompilerVariant.DEFAULT
        binding.clangCompilerRadioGroup.check(
            if (selectedCompiler == CppCompilerVariant.OPTIONAL_CLANG_21)
                R.id.clang21RadioButton
            else
                R.id.clang23RadioButton
        )
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

        val lineCount =
            binding.sourceCode.text
                .count {
                    it == '\n'
                } + 1

        binding.lineNumbers.text =
            (1..lineCount)
                .joinToString(
                    "\n"
                )
    }

    private fun runPrototype() {

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

                binding.runButton.isEnabled =
                    true
                if (BuildConfig.LGP_PLAY_CLANG21_FEATURE_ENABLED) {
                    updateCompilerAvailability(
                        clang21FeatureDelivery?.currentState()?.phase == Phase.INSTALLED
                    )
                }

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

        super.onSaveInstanceState(
            outState
        )
    }

    override fun onDestroy() {

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

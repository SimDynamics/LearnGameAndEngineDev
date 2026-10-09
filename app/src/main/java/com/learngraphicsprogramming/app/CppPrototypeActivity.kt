package com.learngraphicsprogramming.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.ViewGroup
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.learngraphicsprogramming.app.databinding.ActivityCppPrototypeBinding
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import java.util.concurrent.Executors

class CppPrototypeActivity : AppCompatActivity() {

    private lateinit var binding:
            ActivityCppPrototypeBinding

    private val worker =
        Executors.newSingleThreadExecutor()

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

        binding.runButton.isEnabled =
            false

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
                        source
                    )
                }

            runOnUiThread {

                binding.runButton.isEnabled =
                    true

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
            STATE_WRAP,
            lineWrapEnabled
        )

        super.onSaveInstanceState(
            outState
        )
    }

    override fun onDestroy() {

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

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

            binding.statusText.text =
                savedInstanceState.getString(
                    STATE_STATUS
                )
                    ?: getString(
                        R.string.cpp_runtime_ready
                    )

            binding.outputText.text =
                savedInstanceState.getString(
                    STATE_OUTPUT
                )
                    .orEmpty()

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

        binding.statusText.setText(
            R.string.cpp_runtime_ready
        )

        binding.outputText.text =
            ""

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

        binding.statusText.setText(
            R.string.cpp_runtime_running
        )

        binding.outputText.text =
            ""

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

                    binding.statusText.text =
                        if (
                            execution == null
                        ) {

                            getString(
                                R.string.cpp_compilation_failed,
                                compilation.exitCode
                            )

                        } else {

                            getString(
                                R.string.cpp_runtime_finished,
                                execution.exitCode
                            )
                        }

                    binding.outputText.text =
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
                }

                result.onFailure { error ->

                    binding.statusText.setText(
                        R.string.cpp_runtime_failed
                    )

                    binding.outputText.text =
                        error.stackTraceToString()
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

        outState.putString(
            STATE_STATUS,
            binding.statusText.text.toString()
        )

        outState.putString(
            STATE_OUTPUT,
            binding.outputText.text.toString()
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

    private companion object {

        const val STATE_SOURCE =
            "cpp_prototype_source"

        const val STATE_STATUS =
            "cpp_prototype_status"

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
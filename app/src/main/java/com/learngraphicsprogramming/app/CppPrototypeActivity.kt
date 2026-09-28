package com.learngraphicsprogramming.app

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.learngraphicsprogramming.app.databinding.ActivityCppPrototypeBinding
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import java.util.concurrent.Executors

class CppPrototypeActivity : AppCompatActivity() {

    private lateinit var binding: ActivityCppPrototypeBinding

    private val worker =
        Executors.newSingleThreadExecutor()

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

        if (
            savedInstanceState == null
        ) {

            binding.sourceCode.setText(
                SAMPLE_SOURCE
            )

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
        }

        binding.runButton.setOnClickListener {

            runPrototype()
        }
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

        super.onSaveInstanceState(
            outState
        )
    }

    override fun onDestroy() {

        worker.shutdownNow()

        super.onDestroy()
    }

    private companion object {

        const val STATE_SOURCE =
            "cpp_prototype_source"

        const val STATE_STATUS =
            "cpp_prototype_status"

        const val STATE_OUTPUT =
            "cpp_prototype_output"

        const val SAMPLE_SOURCE =
            """#include <iostream>

int main()
{
    std::cout << "Hola desde C++" << std::endl;
}
"""
    }
}
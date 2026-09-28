package com.learngraphicsprogramming.app

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.learngraphicsprogramming.app.databinding.ActivityCppPrototypeBinding
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import java.util.concurrent.Executors

class CppPrototypeActivity : AppCompatActivity() {
    private lateinit var binding: ActivityCppPrototypeBinding
    private val worker = Executors.newSingleThreadExecutor()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityCppPrototypeBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.sourceCode.text = SAMPLE_SOURCE
        binding.runButton.setOnClickListener { runPrototype() }
    }

    private fun runPrototype() {
        binding.runButton.isEnabled = false
        binding.statusText.setText(R.string.cpp_runtime_running)
        binding.outputText.text = ""
        worker.execute {
            val result = runCatching {
                CppExecutionEngine(this).compileAndRun(SAMPLE_SOURCE)
            }
            runOnUiThread {
                binding.runButton.isEnabled = true
                result.onSuccess { engineResult ->
                    val compilation = engineResult.compilation
                    val execution = engineResult.execution
                    binding.statusText.text = if (execution == null) {
                        getString(R.string.cpp_compilation_failed, compilation.exitCode)
                    } else {
                        getString(R.string.cpp_runtime_finished, execution.exitCode)
                    }
                    binding.outputText.text = buildString {
                        append(compilation.stdout)
                        append(compilation.stderr)
                        if (execution != null) {
                            append(execution.stdout)
                            append(execution.stderr)
                        }
                    }.trimEnd()
                }.onFailure { error ->
                    binding.statusText.setText(R.string.cpp_runtime_failed)
                    binding.outputText.text = error.stackTraceToString()
                }
            }
        }
    }

    override fun onDestroy() {
        worker.shutdownNow()
        super.onDestroy()
    }

    private companion object {
        const val SAMPLE_SOURCE = """#include <iostream>

int main()
{
    std::cout << "Hola desde C++" << std::endl;
}
"""
    }
}

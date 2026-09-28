package com.learngraphicsprogramming.app

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.runtime.CppExecutionEngine
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CppExecutionEngineInstrumentedTest {

    @Test
    fun compilesAndExecutesCppLocally() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val result = CppExecutionEngine(context).compileAndRun(SOURCE)

        assertEquals(result.compilation.stderr, 0, result.compilation.exitCode)
        assertNotNull(result.compilation.wasmFile)
        assertTrue(result.compilation.wasmFile!!.length() > 0)
        assertEquals(result.execution?.stderr, 0, result.execution?.exitCode)
        assertEquals("Hola desde C++\n", result.execution?.stdout)
    }

    private companion object {
        const val SOURCE = """#include <iostream>

int main()
{
    std::cout << "Hola desde C++" << std::endl;
}
"""
    }
}

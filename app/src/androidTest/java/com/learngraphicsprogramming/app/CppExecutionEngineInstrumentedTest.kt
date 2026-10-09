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

    @Test
    fun readsTwoIntegersFromStandardInput() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val source = """#include <iostream>

int main()
{
    int a = 0;
    int b = 0;
    if (!(std::cin >> a >> b)) return 2;
    std::cout << (a + b) << '\n';
}
"""
        val result = CppExecutionEngine(context).compileAndRun(source, "7 8\n")
        assertEquals(result.compilation.stderr, 0, result.compilation.exitCode)
        assertEquals(result.execution?.stderr, 0, result.execution?.exitCode)
        assertEquals("15\n", result.execution?.stdout)
    }

    @Test
    fun readsLineWithSpacesFromStandardInput() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val source = """#include <iostream>
#include <string>

int main()
{
    std::string line;
    if (!std::getline(std::cin, line)) return 2;
    std::cout << "[" << line << "]" << '\n';
}
"""
        val result = CppExecutionEngine(context).compileAndRun(source, "hello C++ world\n")
        assertEquals(result.compilation.stderr, 0, result.compilation.exitCode)
        assertEquals(result.execution?.stderr, 0, result.execution?.exitCode)
        assertEquals("[hello C++ world]\n", result.execution?.stdout)
    }

    @Test
    fun compilesAndRunsCpp23LanguageMode() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val source = """
consteval int square(int x) { return x * x; }
static_assert(square(7) == 49);

#include <iostream>
int main() {
    std::cout << square(7) << '\\n';
}
""".trimIndent()
        val result = CppExecutionEngine(context).compileAndRun(source)
        assertEquals(result.compilation.stderr, 0, result.compilation.exitCode)
        assertEquals(result.execution?.stderr, 0, result.execution?.exitCode)
        assertEquals("49\\n", result.execution?.stdout)
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

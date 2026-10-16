package com.learngraphicsprogramming.app

import org.json.JSONObject

/** Presentation metadata only. Never inspect the expected ordering to choose a language. */
object CodeExerciseLanguage {
    enum class Language(val contentName: String) { CPP("cpp"), CMAKE("cmake") }

    fun order(block: JSONObject): Language {
        if (block.has("language")) {
            val language = block.get("language") as? String
                ?: throw IllegalArgumentException("Ordering language must be text")
            return when (language.trim().lowercase()) {
                "cpp", "c++" -> Language.CPP
                "cmake" -> Language.CMAKE
                else -> throw IllegalArgumentException("Unsupported ordering language")
            }
        }
        val fragments = block.optJSONArray("fragments") ?: return Language.CPP
        if (fragments.length() == 0) return Language.CPP
        val commands = (0 until fragments.length()).map { index ->
            val fragment = fragments.opt(index) as? String ?: return Language.CPP
            command.matchEntire(fragment.trim()) ?: return Language.CPP
        }
        // A distinctive built-in command or variable expansion supplies positive CMake
        // evidence. Ambiguous calls such as set(x) retain the historical C++ label.
        return if (commands.all { it.groupValues[1].lowercase() in cmakeCommands } &&
            commands.any { it.groupValues[1].lowercase() in distinctiveCommands ||
                Regex("\\$\\{[A-Za-z_][A-Za-z0-9_]*}").containsMatchIn(it.groupValues[2]) }) {
            Language.CMAKE
        } else Language.CPP
    }

    private val command = Regex("([A-Za-z_][A-Za-z0-9_]*)\\s*\\((.*)\\)\\s*(?:#[^\\n]*)?", RegexOption.DOT_MATCHES_ALL)
    private val distinctiveCommands = setOf("cmake_minimum_required", "project", "add_executable", "add_library",
        "set_target_properties", "set_source_files_properties", "target_compile_options", "target_compile_features",
        "target_compile_definitions", "target_include_directories", "target_link_libraries", "target_sources",
        "add_subdirectory", "add_test", "enable_testing", "find_package", "configure_file", "include_guard")
    private val cmakeCommands = distinctiveCommands + setOf("set", "unset", "list", "option", "if", "elseif", "else",
        "endif", "foreach", "endforeach", "while", "endwhile", "function", "endfunction", "macro", "endmacro",
        "include", "message", "return", "string", "file", "math", "get_target_property", "get_property", "set_property")
}

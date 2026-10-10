package com.learngraphicsprogramming.app.runtime

/**
 * Selects a compiler for one execution, without changing the APK's default.
 *
 * DEFAULT keeps Clang 23 in modern builds and the Clang 21/WAMR legacy
 * fallback in builds created with -Plgp.clang23=false.
 */
internal enum class CppCompilerVariant {
    DEFAULT,
    OPTIONAL_CLANG_21
}

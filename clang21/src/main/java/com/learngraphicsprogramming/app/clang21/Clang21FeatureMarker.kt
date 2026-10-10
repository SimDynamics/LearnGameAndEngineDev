package com.learngraphicsprogramming.app.clang21

/**
 * Lightweight marker for the on-demand dynamic feature.
 *
 * The base APK must continue compiling with Clang 23 when this module
 * is absent. The native Clang 21 payload is packaged in the on-demand split.
 * Its Play installation flow and runtime loader will be integrated later.
 */
object Clang21FeatureMarker {
    const val MODULE_NAME = "clang21"
}

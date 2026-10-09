package com.learngraphicsprogramming.app.graphics

import android.view.Surface

/**
 * Puente JNI del motor grafico nativo.
 *
 * El programa C++ del usuario se ejecuta por separado en WASM.
 * Esta API controla solamente superficies y recursos de renderizado confiables.
 */
internal object GraphicsNative {
    const val BACKEND_AUTO = 0
    const val BACKEND_VULKAN = 1
    const val BACKEND_OPENGL_ES = 2

    init {
        System.loadLibrary("app")
    }

    external fun create(surface: Surface, backend: Int): Long
    external fun draw(handle: Long, elapsedSeconds: Float): Boolean
    external fun describe(handle: Long): String
    external fun lastError(): String
    external fun destroy(handle: Long)
}

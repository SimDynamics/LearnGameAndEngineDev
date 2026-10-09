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
    const val TOUCH_DOWN = 0
    const val TOUCH_MOVE = 1
    const val TOUCH_UP = 2
    const val TOUCH_CANCEL = 3
    const val TOUCH_RESET = 4

    external fun draw(handle: Long, elapsedSeconds: Float): Boolean
    external fun touch(
        handle: Long,
        action: Int,
        normalizedX: Float,
        normalizedY: Float,
        velocityX: Float,
        velocityY: Float
    )
    external fun stats(handle: Long): String
    external fun describe(handle: Long): String
    external fun lastError(): String
    external fun destroy(handle: Long)
}

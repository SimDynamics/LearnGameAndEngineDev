package com.learngraphicsprogramming.app.graphics

import android.graphics.Color
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * Prueba visible de los dos renderizadores nativos.
 *
 * Nunca ejecuta codigo arbitrario del editor dentro del proceso Android.
 * El motor C++/WASM conserva su ejecucion aislada e independiente.
 */
class GraphicsDemoActivity : AppCompatActivity(), SurfaceHolder.Callback {
    private lateinit var preview: SurfaceView
    private lateinit var status: TextView

    private var preferredBackend = GraphicsNative.BACKEND_AUTO
    private var surfaceReady = false
    private var inForeground = false
    private var rendererThread: Thread? = null

    @Volatile
    private var running = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(16, 17, 20))
            setPadding(dp(12), dp(12), dp(12), dp(12))
        }
        val heading = TextView(this).apply {
            text = "Renderizador nativo C++"
            textSize = 21f
            setTextColor(Color.WHITE)
            setPadding(0, dp(4), 0, dp(12))
        }
        root.addView(heading)

        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
        }
        addBackendButton(row, "Auto", GraphicsNative.BACKEND_AUTO)
        addBackendButton(row, "Vulkan", GraphicsNative.BACKEND_VULKAN)
        addBackendButton(row, "OpenGL ES", GraphicsNative.BACKEND_OPENGL_ES)
        root.addView(row)

        status = TextView(this).apply {
            text = "Iniciando motor gráfico..."
            textSize = 14f
            setTextColor(Color.rgb(205, 212, 226))
            setPadding(0, dp(12), 0, dp(12))
        }
        root.addView(status)

        preview = SurfaceView(this).apply {
            holder.addCallback(this@GraphicsDemoActivity)
        }
        root.addView(
            preview,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1f
            )
        )
        setContentView(root)
    }

    private fun addBackendButton(
        row: LinearLayout,
        label: String,
        backend: Int
    ) {
        row.addView(
            Button(this).apply {
                text = label
                textSize = 12f
                isAllCaps = false
                minWidth = 0
                setOnClickListener {
                    preferredBackend = backend
                    stopRenderer()
                    startRenderer()
                }
            },
            LinearLayout.LayoutParams(0, dp(54), 1f)
        )
    }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density + 0.5f).toInt()

    override fun onResume() {
        super.onResume()
        inForeground = true
        startRenderer()
    }

    override fun onPause() {
        inForeground = false
        stopRenderer()
        super.onPause()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceReady = true
        startRenderer()
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int
    ) {
        // No renderizamos desde el hilo principal.
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        stopRenderer()
    }

    private fun startRenderer() {
        if (!surfaceReady || !inForeground || rendererThread != null) return

        running = true
        val androidSurface = preview.holder.surface
        val requested = preferredBackend

        rendererThread = Thread({
            val handle = GraphicsNative.create(androidSurface, requested)
            if (handle == 0L) {
                showStatus("No se pudo inicializar: " + GraphicsNative.lastError())
                return@Thread
            }
            try {
                showStatus(GraphicsNative.describe(handle))
                val started = System.nanoTime()
                while (running) {
                    val elapsed = (System.nanoTime() - started) / 1_000_000_000f
                    if (!GraphicsNative.draw(handle, elapsed)) {
                        showStatus("Renderizador detenido: " + GraphicsNative.lastError())
                        break
                    }
                    try {
                        Thread.sleep(16)
                    } catch (_: InterruptedException) {
                        break
                    }
                }
            } finally {
                GraphicsNative.destroy(handle)
            }
        }, "lgp-graphics-renderer").also { it.start() }
    }

    private fun stopRenderer() {
        running = false
        val thread = rendererThread ?: return
        thread.interrupt()
        try {
            thread.join(3500)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
        if (thread.isAlive) {
            status.text = "El renderizador aún está finalizando."
        } else {
            rendererThread = null
        }
    }

    private fun showStatus(message: String) {
        runOnUiThread {
            if (!isFinishing && !isDestroyed) {
                status.text = message
            }
        }
    }
}

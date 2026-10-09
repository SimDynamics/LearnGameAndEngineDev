package com.learngraphicsprogramming.app.graphics

import android.graphics.Color
import android.os.Bundle
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.VelocityTracker
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.util.concurrent.ConcurrentLinkedQueue

/**
 * Touch-controlled native C++ physics shared by Vulkan and OpenGL ES.
 * All JNI physics calls run on the renderer thread, never on the UI thread.
 */
class GraphicsDemoActivity : AppCompatActivity(), SurfaceHolder.Callback {
    private data class TouchCommand(
        val action: Int,
        val x: Float = 0f,
        val y: Float = 0f,
        val velocityX: Float = 0f,
        val velocityY: Float = 0f
    )

    private lateinit var preview: SurfaceView
    private lateinit var status: TextView

    private var preferredBackend = GraphicsNative.BACKEND_AUTO
    private var surfaceReady = false
    private var inForeground = false
    private var rendererThread: Thread? = null
    private var velocityTracker: VelocityTracker? = null
    private val pendingTouches = ConcurrentLinkedQueue<TouchCommand>()

    @Volatile
    private var running = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(16, 17, 20))
            setPadding(dp(12), dp(12), dp(12), dp(12))
        }
        root.addView(TextView(this).apply {
            text = "Simulación de física 3D"
            textSize = 21f
            setTextColor(Color.WHITE)
            setPadding(0, dp(4), 0, dp(12))
        })

        val backends = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
        }
        addBackendButton(backends, "Auto", GraphicsNative.BACKEND_AUTO)
        addBackendButton(backends, "Vulkan", GraphicsNative.BACKEND_VULKAN)
        addBackendButton(backends, "OpenGL ES", GraphicsNative.BACKEND_OPENGL_ES)
        root.addView(backends)

        root.addView(Button(this).apply {
            text = "Reiniciar objetos"
            isAllCaps = false
            setOnClickListener {
                pendingTouches.add(TouchCommand(GraphicsNative.TOUCH_RESET))
            }
        })
        root.addView(TextView(this).apply {
            text = "Toca una esfera, arrástrala y suéltala. La velocidad del dedo determina la fuerza."
            textSize = 13f
            setTextColor(Color.rgb(182, 197, 215))
            setPadding(0, dp(5), 0, dp(7))
        })

        status = TextView(this).apply {
            text = "Preparando simulación..."
            textSize = 13f
            setTextColor(Color.rgb(205, 212, 226))
            setPadding(0, dp(8), 0, dp(12))
        }
        root.addView(status)

        preview = SurfaceView(this).apply {
            holder.addCallback(this@GraphicsDemoActivity)
            setOnTouchListener { _, event -> onPreviewTouch(event) }
        }
        root.addView(
            preview,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        )
        setContentView(root)
    }

    private fun onPreviewTouch(event: MotionEvent): Boolean {
        val width = preview.width.toFloat().coerceAtLeast(1f)
        val height = preview.height.toFloat().coerceAtLeast(1f)
        val x = (event.x / width).coerceIn(0f, 1f)
        val y = (event.y / height).coerceIn(0f, 1f)

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                velocityTracker?.recycle()
                velocityTracker = VelocityTracker.obtain().apply { addMovement(event) }
                pendingTouches.add(TouchCommand(GraphicsNative.TOUCH_DOWN, x, y))
            }

            MotionEvent.ACTION_MOVE -> {
                val tracker = velocityTracker
                if (tracker != null) {
                    tracker.addMovement(event)
                    tracker.computeCurrentVelocity(1000, 8000f)
                    pendingTouches.add(
                        TouchCommand(
                            GraphicsNative.TOUCH_MOVE, x, y,
                            tracker.xVelocity / width,
                            tracker.yVelocity / height
                        )
                    )
                } else {
                    pendingTouches.add(TouchCommand(GraphicsNative.TOUCH_MOVE, x, y))
                }
            }

            MotionEvent.ACTION_UP -> {
                val tracker = velocityTracker
                if (tracker != null) {
                    tracker.addMovement(event)
                    tracker.computeCurrentVelocity(1000, 8000f)
                    pendingTouches.add(
                        TouchCommand(
                            GraphicsNative.TOUCH_UP, x, y,
                            tracker.xVelocity / width,
                            tracker.yVelocity / height
                        )
                    )
                    tracker.recycle()
                } else {
                    pendingTouches.add(TouchCommand(GraphicsNative.TOUCH_UP, x, y))
                }
                velocityTracker = null
            }

            MotionEvent.ACTION_CANCEL, MotionEvent.ACTION_POINTER_DOWN -> {
                velocityTracker?.recycle()
                velocityTracker = null
                pendingTouches.add(TouchCommand(GraphicsNative.TOUCH_CANCEL))
            }
        }
        return true
    }

    private fun addBackendButton(row: LinearLayout, label: String, backend: Int) {
        row.addView(
            Button(this).apply {
                text = label
                textSize = 12f
                isAllCaps = false
                minWidth = 0
                setOnClickListener {
                    if (preferredBackend != backend) {
                        preferredBackend = backend
                        stopRenderer()
                        startRenderer()
                    }
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
        holder: SurfaceHolder, format: Int, width: Int, height: Int
    ) {
        // Rendering remains exclusively on the native rendering thread.
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        stopRenderer()
    }

    private fun startRenderer() {
        if (!surfaceReady || !inForeground || rendererThread != null) return
        pendingTouches.clear()
        running = true
        val surface = preview.holder.surface
        val requested = preferredBackend

        rendererThread = Thread({
            val handle = GraphicsNative.create(surface, requested)
            if (handle == 0L) {
                showStatus("Motor gráfico: " + GraphicsNative.lastError())
                return@Thread
            }
            try {
                val rendererName = GraphicsNative.describe(handle)
                val started = System.nanoTime()
                var previousStats = started
                var frameCount = 0

                while (running) {
                    var touch = pendingTouches.poll()
                    while (touch != null) {
                        GraphicsNative.touch(
                            handle, touch.action, touch.x, touch.y,
                            touch.velocityX, touch.velocityY
                        )
                        touch = pendingTouches.poll()
                    }

                    val now = System.nanoTime()
                    val elapsed = (now - started) / 1_000_000_000f
                    if (!GraphicsNative.draw(handle, elapsed)) {
                        showStatus("Motor gráfico detenido: " + GraphicsNative.lastError())
                        break
                    }
                    frameCount++

                    val dt = (now - previousStats) / 1_000_000_000f
                    if (dt >= 0.7f) {
                        val fps = frameCount / dt
                        showStatus(
                            rendererName + "\n" +
                            GraphicsNative.stats(handle) +
                            "\n" + String.format(java.util.Locale.US, "%.1f FPS", fps)
                        )
                        previousStats = now
                        frameCount = 0
                    }
                    try {
                        Thread.sleep(8)
                    } catch (_: InterruptedException) {
                        break
                    }
                }
            } finally {
                GraphicsNative.destroy(handle)
            }
        }, "lgp-physics-renderer").also { it.start() }
    }

    private fun stopRenderer() {
        running = false
        velocityTracker?.recycle()
        velocityTracker = null
        pendingTouches.clear()

        val thread = rendererThread ?: return
        thread.interrupt()
        try {
            thread.join(3500)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
        if (thread.isAlive) {
            status.text = "El motor aún está cerrando recursos de la GPU."
        } else {
            rendererThread = null
        }
    }

    private fun showStatus(message: String) {
        runOnUiThread {
            if (!isFinishing && !isDestroyed) status.text = message
        }
    }
}

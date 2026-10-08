package com.learngraphicsprogramming.app

import ai.onnxruntime.OrtEnvironment
import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

@RunWith(AndroidJUnit4::class)
class AndroidSpeechSynthesisDeviceTest {
    @Test fun onnxRuntimeNativeLibraryLoadsOnConnectedArm64Device() {
        // A native dependency check, not a claim that Kokoro synthesis passed.
        val environment = OrtEnvironment.getEnvironment()
        assertTrue(environment.version.startsWith("1.30."))
        assertTrue(OrtEnvironment.getAvailableProviders().isNotEmpty())
    }

    @Test fun installedOfflineSpanishVoiceProducesNonSilentAudio() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val initialized = CountDownLatch(1)
        val completed = CountDownLatch(1)
        val status = AtomicInteger(TextToSpeech.ERROR)
        val synthesisError = AtomicInteger(0)
        var engine: TextToSpeech? = null
        val output = File(context.cacheDir, "narration-spanish-device-test.wav")
        try {
            instrumentation.runOnMainSync {
                engine = TextToSpeech(context) { result -> status.set(result); initialized.countDown() }
            }
            assertTrue("El motor TTS debe responder", initialized.await(20, TimeUnit.SECONDS))
            assertEquals("Motor TTS inicializado", TextToSpeech.SUCCESS, status.get())
            val tts = engine!!
            val voice = tts.voices.orEmpty().filter {
                it.locale.language == "es" && !it.isNetworkConnectionRequired &&
                    TextToSpeech.Engine.KEY_FEATURE_NOT_INSTALLED !in it.features.orEmpty()
            }.maxByOrNull { it.quality }
            assertNotNull("Instala una voz española sin conexión antes de esta comprobación", voice)
            assertEquals(TextToSpeech.SUCCESS, tts.setVoice(voice!!))
            tts.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                override fun onStart(utteranceId: String?) = Unit
                override fun onDone(utteranceId: String?) { completed.countDown() }
                @Deprecated("Legacy engine callback")
                override fun onError(utteranceId: String?) { synthesisError.set(-1); completed.countDown() }
                override fun onError(utteranceId: String?, errorCode: Int) {
                    synthesisError.set(errorCode); completed.countDown()
                }
            })
            assertEquals(TextToSpeech.SUCCESS, tts.synthesizeToFile(
                "Un medio más tres cuartos. Equis al cuadrado. Raíz cuadrada de nueve.",
                Bundle(), output, "spanish-device-test"
            ))
            assertTrue("Debe generarse audio en español", completed.await(30, TimeUnit.SECONDS))
            assertEquals("Síntesis sin errores", 0, synthesisError.get())
            assertTrue("El archivo contiene audio y no sólo cabecera", output.length() > 4_096L)
            val bytes = output.readBytes()
            assertEquals("RIFF", bytes.copyOfRange(0, 4).toString(Charsets.US_ASCII))
            assertEquals("WAVE", bytes.copyOfRange(8, 12).toString(Charsets.US_ASCII))
            val audio = wavAudioData(bytes)
            assertTrue("PCM debe contener muestras audibles", audio.size > 4_096 && audio.any { it != 0.toByte() })
        } finally {
            engine?.stop()
            engine?.shutdown()
            output.delete()
        }
    }

    private fun wavAudioData(bytes: ByteArray): ByteArray {
        var position = 12
        while (position + 8 <= bytes.size) {
            val name = bytes.copyOfRange(position, position + 4).toString(Charsets.US_ASCII)
            val length = java.nio.ByteBuffer.wrap(bytes, position + 4, 4)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN).int
            require(length >= 0 && length <= bytes.size - position - 8) { "WAV incompleto" }
            if (name == "data") return bytes.copyOfRange(position + 8, position + 8 + length)
            position += 8 + length + (length % 2)
        }
        error("El WAV no contiene muestras de audio")
    }
}

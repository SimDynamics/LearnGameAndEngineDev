package com.learngraphicsprogramming.app.narration

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.learngraphicsprogramming.app.narration.phonemes.SpanishFluidG2P
import java.io.File
import kotlin.math.abs
import kotlin.math.sqrt
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Read-only recovery check against the installed resources. No fixture import,
 * settings changes, WAV writing, cache lookup, TTS or Activity lifecycle hooks.
 * The three native waveforms exist only in memory.
 */
@RunWith(AndroidJUnit4::class)
class KokoroRecoveryDeviceTest {
    @Test
    fun restoredResourcesVerifyAndSpanishNativeSynthesisDoesNotWriteAppData() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val external = requireNotNull(context.getExternalFilesDir(null))
        val modelRoot = File(external, "kokoro/${KokoroModelManifest.revision}").canonicalFile
        assertTrue("The restored model revision must already exist", modelRoot.isDirectory)
        val artifacts = KokoroModelManifest.artifacts
        assertEquals("The recovery checks every current manifest entry", artifacts.size,
            artifacts.map { it.fileName }.toSet().size)
        assertEquals("The approved recovery contains 50 independently verified resources", 50, artifacts.size)
        for (artifact in artifacts) {
            val resource = File(modelRoot, artifact.fileName)
            assertEquals("A resource must remain inside the installed revision", modelRoot, resource.canonicalFile.parentFile)
            assertTrue("Restored ${artifact.fileName} must match its pinned size and SHA-256",
                KokoroModelManifest.verify(resource, artifact))
        }
        Log.i("KokoroRecovery", "Verified ${artifacts.size} resources, ${artifacts.sumOf { it.bytes }} bytes")

        val spanish = KokoroModelManifest.resources(NarrationLanguage.SPANISH).single()
        val lexicon = SpanishFluidG2P.loadLexicon(File(modelRoot, spanish.fileName))
        val phrases = listOf(
            "Una temperatura de -3 °C describe un estado.",
            "Contrasta menos cuatro menos tres y −4 − (−3).",
            "Las fracciones equivalentes representan la misma cantidad."
        )
        val owner = Any()
        try {
            KokoroEngineCoordinator.withEngine(owner, modelRoot, { false }, foreground = true) { engine ->
                phrases.forEachIndexed { index, text ->
                    val phonemes = SpanishFluidG2P.phonemize(text, lexicon)
                    assertTrue("Recovery phrase $index must contain actual Spanish phonemes", phonemes.isNotBlank())
                    for (part in KokoroPhonemeChunks.split(phonemes)) {
                        val pcm = engine.synthesizePhonemes(part, "ef_dora", 1f)
                        assertTrue("Recovery phrase $index must contain speech-duration PCM", pcm.size > 2_400)
                        assertTrue("Recovery phrase $index PCM must be finite", pcm.all(Float::isFinite))
                        val peak = pcm.maxOf { abs(it) }
                        val rms = sqrt(pcm.sumOf { it.toDouble() * it } / pcm.size)
                        assertTrue("Recovery phrase $index must contain non-silent native speech",
                            peak > 0.005f && rms > 0.0005)
                        Log.i("KokoroRecovery", "Native phrase $index: ${pcm.size} samples, peak=$peak, rms=$rms")
                    }
                }
            }
        } finally {
            // Never close the borrowed engine or process-wide OrtEnvironment directly.
            // The coordinator releases an idle session under its exclusive lock, and
            // skips release if another narration owner has already acquired it.
            KokoroEngineCoordinator.cancel(owner)
            KokoroEngineCoordinator.releaseIfIdle()
        }
    }
}

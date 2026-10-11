package com.learngraphicsprogramming.app.narration

import android.content.Context
import android.os.Build
import android.util.Log
import java.io.File
import java.io.FileNotFoundException
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.UUID

/**
 * Persistent, bounded audio cache for deterministic Kokoro sentences.
 * Never caches Android TTS fallback audio under a Kokoro voice key.
 *
 * Matching pre-recorded audio can also ship inside the APK at:
 * assets/narration/prerendered/<key>.ogg (Opus preferred on Android 10+)
 * or assets/narration/prerendered/<key>.wav (lossless fallback).
 * The same validation applies to bundled files and local synthesized files.
 */
internal object KokoroNarrationCache {
    private const val VERSION = "kokoro-wav-v1"
    private const val SPANISH_TECHNICAL_REVISION = "spanish-technical-speech-v1"
    private const val DIRECTORY = "kokoro-narration-cache"
    // Audio persists until the user deletes app data. Background work stops
    // on low storage instead of evicting successfully generated lessons.

    fun key(language: String, voice: String, text: String): String {
        val parts = mutableListOf(VERSION, KokoroModelManifest.revision, language, voice, text)
        if (language == NarrationLanguage.SPANISH.tag) {
            val prepared = SpanishTechnicalSpeech.prepare(text)
            if (prepared.changed || text.any { it in "ᵃᵇᶜ" }) {
                // Preserve every legacy key whose pronunciation is unchanged. New
                // technical readings and previously lost exponents need fresh audio.
                parts += SPANISH_TECHNICAL_REVISION
                parts += prepared.text
            }
        }
        val payload = parts.joinToString("\u0000")
        val digest = MessageDigest.getInstance("SHA-256")
            .digest(payload.toByteArray(Charsets.UTF_8))
        return digest.joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }

    private fun root(context: Context): File {
        val directory = File(context.filesDir, DIRECTORY)
        check(directory.isDirectory || directory.mkdirs()) { "No se pudo crear la caché Kokoro" }
        return directory
    }

    private fun validatedWav(file: File): Boolean = runCatching {
        if (!file.isFile || file.length() !in 46L..(96L * 1024 * 1024)) return@runCatching false
        val header = ByteArray(44)
        RandomAccessFile(file, "r").use { it.readFully(header) }
        val pcm = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
        String(header, 0, 4, Charsets.US_ASCII) == "RIFF" &&
            String(header, 8, 4, Charsets.US_ASCII) == "WAVE" &&
            String(header, 12, 4, Charsets.US_ASCII) == "fmt " &&
            pcm.getShort(20).toInt() == 1 &&
            pcm.getShort(22).toInt() == 1 &&
            pcm.getInt(24) == 24_000 &&
            pcm.getShort(34).toInt() == 16 &&
            String(header, 36, 4, Charsets.US_ASCII) == "data" &&
            pcm.getInt(40).toLong() + 44L == file.length()
    }.getOrDefault(false)

    private fun validatedOpus(file: File): Boolean = runCatching {
        if (!file.isFile || file.length() !in 64L..(96L * 1024 * 1024)) return@runCatching false
        val header = ByteArray(128)
        val count = file.inputStream().use { it.read(header) }
        count >= 48 &&
            String(header, 0, 4, Charsets.US_ASCII) == "OggS" &&
            String(header, 0, count, Charsets.ISO_8859_1).contains("OpusHead")
    }.getOrDefault(false)

    private fun validatedAudio(file: File): Boolean = when (file.extension) {
        "wav" -> validatedWav(file)
        "ogg" -> validatedOpus(file)
        else -> false
    }

    /** Keep rejected bytes for diagnosis; never erase an existing recording. */
    private fun preserveInvalid(context: Context, file: File): Boolean {
        if (!file.exists()) return true
        if (!file.isFile) return false
        val preserved = File(file.parentFile,
            "${file.nameWithoutExtension}.unverified-${UUID.randomUUID()}.${file.extension}")
        if (!file.renameTo(preserved)) {
            Log.w("KokoroInference", "Could not preserve invalid narration; leaving it intact")
            return false
        }
        KokoroNarrationDiagnostics.event(context, "KokoroInference", "invalid-audio-preserved",
            fields = mapOf("file" to preserved.name, "bytes" to preserved.length()))
        return true
    }

    @Synchronized
    fun lookup(context: Context, language: String, voice: String, text: String): File? {
        val hash = key(language, voice, text)
        val directory = root(context)
        // Ogg/Opus playback is Android 10+. Earlier versions use WAV.
        val formats = if (Build.VERSION.SDK_INT >= 29) listOf("ogg", "wav") else listOf("wav")
        for (extension in formats) {
            val cached = File(directory, "$hash.$extension")
            if (validatedAudio(cached)) {
                cached.setLastModified(System.currentTimeMillis())
                return cached
            }
            if (cached.isFile) preserveInvalid(context, cached)
        }

        // Optional build-time assets. Prefer compressed Opus to PCM WAV.
        // The absence of pre-rendered files is normal on development builds.
        for (extension in formats) {
            val output = File(directory, "$hash.$extension")
            val staged = File(directory, ".$hash-${UUID.randomUUID()}.$extension")
            try {
                context.assets.open("narration/prerendered/$hash.$extension").use { source ->
                    staged.outputStream().use { destination -> source.copyTo(destination) }
                }
                if (validatedAudio(staged) && preserveInvalid(context, output) && staged.renameTo(output)) {
                    KokoroNarrationDiagnostics.event(context, "KokoroInference", "packaged-audio-loaded",
                        fields = mapOf("cacheKey" to hash, "format" to extension))
                    return output
                }
            } catch (_: FileNotFoundException) {
                // Try the next format or fall back to the installed model.
            } catch (error: Exception) {
                Log.w("KokoroInference", "Pre-rendered audio unavailable", error)
            } finally {
                staged.delete()
            }
        }
        return null
    }

    @Synchronized
    fun store(context: Context, language: String, voice: String, text: String, wav: File): File? {
        if (!validatedWav(wav)) return null
        val hash = key(language, voice, text)
        val directory = root(context)
        val target = File(directory, "$hash.wav")
        if (validatedWav(target)) return target
        val staging = File(directory, ".$hash-${UUID.randomUUID()}.part")
        return try {
            wav.inputStream().use { input ->
                staging.outputStream().use { output -> input.copyTo(output) }
            }
            if (validatedWav(staging) && preserveInvalid(context, target) && staging.renameTo(target)) target else null
        } catch (error: Exception) {
            Log.w("KokoroInference", "Could not preserve generated narration", error)
            null
        } finally {
            staging.delete()
        }
    }

    /** Remove only abandoned partial writes. Complete WAV/Opus files are permanent. */
    @Synchronized
    fun prune(context: Context) {
        root(context).listFiles().orEmpty()
            .filter { it.isFile && (it.name.endsWith(".part") ||
                it.name.matches(Regex("^\\.[a-f0-9]{64}-[a-zA-Z0-9-]+\\.(wav|ogg)$"))) }
            .forEach { it.delete() }
    }

}

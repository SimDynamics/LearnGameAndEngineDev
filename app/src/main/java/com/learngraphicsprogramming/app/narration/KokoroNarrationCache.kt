package com.learngraphicsprogramming.app.narration

import android.content.Context
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
 * A matching pre-recorded WAV can also ship inside the APK at:
 * assets/narration/prerendered/<key>.wav
 * The same validation applies to bundled files and local synthesized files.
 */
internal object KokoroNarrationCache {
    private const val VERSION = "kokoro-wav-v1"
    private const val DIRECTORY = "kokoro-narration-cache"
    private const val MAX_BYTES = 384L * 1024 * 1024
    private val validName = Regex("^[a-f0-9]{64}\\.wav$")

    fun key(language: String, voice: String, text: String): String {
        val payload = listOf(VERSION, KokoroModelManifest.revision, language, voice, text)
            .joinToString("\u0000")
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
        if (!file.isFile || file.length() !in 46..(96L * 1024 * 1024)) return@runCatching false
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

    @Synchronized
    fun lookup(context: Context, language: String, voice: String, text: String): File? {
        val hash = key(language, voice, text)
        val output = File(root(context), "$hash.wav")
        if (validatedWav(output)) {
            output.setLastModified(System.currentTimeMillis())
            return output
        }
        if (output.isFile) output.delete()

        // Optional build-time pre-rendered assets. Missing assets are normal:
        // a device can still synthesize and cache its own speech.
        val staged = File(root(context), ".$hash-${UUID.randomUUID()}.part")
        return try {
            context.assets.open("narration/prerendered/$hash.wav").use { source ->
                staged.outputStream().use { destination -> source.copyTo(destination) }
            }
            if (!validatedWav(staged) || !staged.renameTo(output)) {
                staged.delete()
                null
            } else {
                Log.i("KokoroInference", "Loaded pre-rendered narration from APK")
                output
            }
        } catch (_: FileNotFoundException) {
            null
        } catch (error: Exception) {
            Log.w("KokoroInference", "Pre-rendered audio unavailable", error)
            null
        } finally {
            staged.delete()
        }
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
            if (validatedWav(staging) && staging.renameTo(target)) target else null
        } catch (error: Exception) {
            Log.w("KokoroInference", "Could not preserve generated narration", error)
            null
        } finally {
            staging.delete()
        }
    }

    /** Evict only at controller startup, before this controller can play a WAV. */
    @Synchronized
    fun prune(context: Context) {
        val files = root(context).listFiles().orEmpty()
        files.filter { it.isFile && it.name.endsWith(".part") }.forEach { it.delete() }
        val wavs = files.filter { it.isFile && validName.matches(it.name) }
            .sortedBy { it.lastModified() }
        var used = wavs.sumOf { it.length() }
        for (file in wavs) {
            if (used <= MAX_BYTES) break
            val bytes = file.length()
            if (file.delete()) used -= bytes
        }
    }
}

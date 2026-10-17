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
        val annotated = (language == NarrationLanguage.SPANISH.tag || language == NarrationLanguage.FRENCH.tag) &&
            ManualEnglishSpeech.isPrepared(text)
        val spoken = if (annotated) ManualEnglishSpeech.strip(text) else text
        val parts = mutableListOf(VERSION, KokoroModelManifest.revision, language, voice, spoken)
        if (language == NarrationLanguage.SPANISH.tag) {
            val prepared = SpanishTechnicalSpeech.prepare(spoken)
            if (prepared.changed || spoken.any { it in "ᵃᵇᶜ" }) {
                // Preserve every legacy key whose pronunciation is unchanged. New
                // technical readings and previously lost exponents need fresh audio.
                parts += SPANISH_TECHNICAL_REVISION
                parts += prepared.text
            }
        }
        if (language == NarrationLanguage.AMERICAN_ENGLISH.tag || language == NarrationLanguage.BRITISH_ENGLISH.tag) {
            if (EnglishTechnicalSpeech.hasChangedSymbolPronunciation(text)) {
                // Only recordings with corrected technical readings get another identity.
                parts += EnglishTechnicalSpeech.CACHE_REVISION
                EnglishTechnicalSpeech.replacements(text).forEach { replacement ->
                    parts += "${replacement.start}:${replacement.endExclusive}"
                    parts += replacement.spoken
                    parts += replacement.phonemes.orEmpty()
                }
            }
        }
        if (language == NarrationLanguage.FRENCH.tag) {
            if (FrenchTechnicalSpeech.hasLegacySymbolRevision(text)) {
                parts += FrenchTechnicalSpeech.SYMBOL_CACHE_REVISION
            }
            val operators = FrenchTechnicalSpeech.changedOperatorReadings(text)
            if (operators.isNotEmpty()) {
                // Keep valid dièse-only and ordinary recordings. Names and source
                // ownership change the identity only where speech actually changes.
                parts += "french-technical-operator-names-v1"
                operators.forEach { (start, end, words) ->
                    parts += "$start:$end"
                    parts += words
                }
            }
        }
        if (annotated) {
            val terms = ManualEnglishSpeech.spans(text)
            if (terms.isNotEmpty()) {
                parts += "manual-english-same-voice-v1"
                // Only the selected occurrences and their actual phones affect this
                // key. Adding an unrelated term preserves every compatible recording.
                terms.forEach { term ->
                    parts += ManualEnglishSpeech.strip(text.substring(0, term.start)).length.toString()
                    parts += term.entry.id
                    parts += if (language == NarrationLanguage.FRENCH.tag) MixedFrenchG2P.englishPhones(term.entry)
                        else ManualEnglishPronunciations.phonemes(term.entry)
                    if (language == NarrationLanguage.FRENCH.tag) {
                        MixedFrenchG2P.namespacePhonesBefore(text, term)?.let { phones ->
                            parts += "french-us-namespace-v1"
                            parts += phones
                        }
                        if (MixedFrenchG2P.needsTermBoundary(text, term)) parts += "french-cout-boundary-v1"
                    }
                }
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

    /**
     * A read-only inventory for course statistics. It never calls lookup(),
     * creates the cache directory, changes file timestamps or removes files.
     * Count only fully verified, final audio files; exclude stages and rejects.
     */
    fun verifiedInventory(context: Context, cancelled: () -> Boolean = {
        Thread.currentThread().isInterrupted
    }): Map<String, KokoroCachedAudio> {
        val directory = File(context.applicationContext.filesDir, DIRECTORY)
        if (!directory.isDirectory) return emptyMap()
        val items = mutableMapOf<String, KokoroCachedAudio>()
        for (file in directory.listFiles().orEmpty()) {
            if (cancelled()) throw InterruptedException("Audio inventory interrupted")
            if (!file.isFile || file.extension !in setOf("wav", "ogg")) continue
            val hash = file.nameWithoutExtension
            if (hash.length != 64 || hash.any { it !in '0'..'9' && it !in 'a'..'f' }) continue
            if (!validatedAudio(file)) continue
            val previous = items[hash] ?: KokoroCachedAudio(0, 0L)
            items[hash] = KokoroCachedAudio(previous.files + 1, previous.bytes + file.length())
        }
        return items
    }

    data class ExplicitRemoval(
        val deletedFiles: Int, val deletedBytes: Long,
        val failedFiles: Int, val interrupted: Boolean
    )

    /**
     * ONLY after the user's confirmation. Never called by pruning, budget
     * management, playback or background generation.
     *
     * Hashes have already been checked against every course's source ownership.
     * Revalidate each exact final WAV/Opus file before deleting it; stage,
     * unverified and unknown cache files are never eligible.
     */
    @Synchronized
    fun deleteExclusiveVerified(
        context: Context, exclusiveKeys: Set<String>,
        permit: () -> Boolean
    ): ExplicitRemoval {
        require(exclusiveKeys.all { it.matches(Regex("[a-f0-9]{64}")) })
        val directory = File(context.applicationContext.filesDir, DIRECTORY)
        if (!directory.isDirectory) return ExplicitRemoval(0, 0L, 0, false)
        var deleted = 0
        var bytes = 0L
        var failed = 0
        for (hash in exclusiveKeys) for (extension in listOf("ogg", "wav")) {
            if (!permit()) return ExplicitRemoval(deleted, bytes, failed, true)
            val file = File(directory, "$hash.$extension")
            var interruptedWithinStep = false
            val exclusive = NarrationWorkPriority.runMaintenanceStep {
                if (!permit()) {
                    interruptedWithinStep = true
                } else if (validatedAudio(file)) {
                    val length = file.length()
                    if (file.delete()) {
                        deleted++
                        bytes += length
                    } else failed++
                }
            }
            if (!exclusive || interruptedWithinStep)
                return ExplicitRemoval(deleted, bytes, failed, true)
        }
        return ExplicitRemoval(deleted, bytes, failed, false)
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

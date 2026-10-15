package com.learngraphicsprogramming.app.narration

import java.io.File
import java.security.MessageDigest

/** Metadata only: constructing this manifest never downloads weights or voices. */
object KokoroModelManifest {
    const val revision = "1939ad2a8e416c0acfeecc08a694d14ef25f2231"
    const val source = "https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX"
    const val license = "Apache-2.0"
    const val sampleRate = 24_000
    const val maximumPhonemeTokens = 510

    data class Artifact(
        val fileName: String,
        val remotePath: String,
        val bytes: Long,
        val sha256: String,
        val sourceUrl: String? = null
    ) {
        val url: String get() = sourceUrl ?: "$source/resolve/$revision/$remotePath"
    }

    // Published LFS SHA-256 values for the complete fp32 graph and voice data.
    // Small JSON hashes were calculated from the pinned revision's exact bytes.
    val sharedArtifacts = listOf(
        Artifact("model.onnx", "onnx/model.onnx", 325_532_232L,
            "8fbea51ea711f2af382e88c833d9e288c6dc82ce5e98421ea61c058ce21a34cb"),
        Artifact("tokenizer.json", "tokenizer.json", 3_497L,
            "77a02c8e164413299b4b4c403b14f8e0e1c1b727db4d46a09d6327b861060a34"),
        Artifact("tokenizer_config.json", "tokenizer_config.json", 113L,
            "be1cb066d6ef6b074b3f15e6a6dd21ac88ff3cdaedf325f0aaed686c70f75d20"),
        Artifact("config.json", "config.json", 44L,
            "df34b4f930b23447cd4dc410fabfb42eb3f24e803e6c3f97d618fb359380a36f")
    )

    // Every resource is independent. The FP32 graph and tokenizer exist only once.
    val voiceArtifacts get() = KokoroVoiceCatalog.voices.map { it.artifact }
    val artifacts get() = sharedArtifacts + voiceArtifacts + languageArtifacts + EnglishAuxResources.artifacts
    val languageArtifacts: List<Artifact> get() = listOf(
        Artifact("es_lexicon_cache.json", "es_lexicon_cache.json", 3_894_233L,
            "0c35e45f0384dde2fcb0e38b9a041246172f6e89491b1f26d1d8c1c9bb1d4317",
            "https://huggingface.co/FluidInference/kokoro-82m-coreml/resolve/d93c5f3eed11847fb81bd6d6a76665e133d7ca77/es_lexicon_cache.json"),
        Artifact("fr_lexicon_cache.json", "fr_lexicon_cache.json", 13_625_891L,
            "9c3073d1dbed06e1eec6bcfecbdbb00df4e826a7b5f8df693a9720f7238c79cd",
            "https://huggingface.co/FluidInference/kokoro-82m-coreml/resolve/d93c5f3eed11847fb81bd6d6a76665e133d7ca77/fr_lexicon_cache.json")
    ) + englishArtifacts

    val englishArtifacts: List<Artifact> get() =
        com.learngraphicsprogramming.app.narration.phonemes.EnglishMisakiResources.dictionaries.map { (name, info) ->
            Artifact(name, "misaki/data/$name", info.bytes, info.sha256,
                info.url(name))
        }

    fun resources(language: NarrationLanguage): List<Artifact> = when (language.resourceGroup) {
        "es" -> languageArtifacts.filter { it.fileName.startsWith("es_") }
        "fr" -> languageArtifacts.filter { it.fileName.startsWith("fr_") }
        else -> englishArtifacts
    }

    val totalBytes: Long = artifacts.sumOf { it.bytes }

    val licensingSources = mapOf(
        "Kokoro weights and official voices (Apache-2.0)" to
            "https://huggingface.co/hexgrad/Kokoro-82M/blob/9871713c6a29032f914cee65b617e2b6b3323770/README.md",
        "ONNX export and token vocabulary (Apache-2.0 repository metadata)" to
            "$source/blob/$revision/README.md",
        "ONNX Runtime (MIT; not bundled by this manifest)" to
            "https://github.com/microsoft/onnxruntime/blob/v1.30.0/LICENSE",
        "FluidAudio Spanish/French G2P (Apache-2.0; ipa-dict MIT)" to
            "https://github.com/FluidInference/FluidAudio/blob/0c0f113e8db4b862b19a99ce9fd7c9da73324f5f/ThirdPartyLicenses/KokoroAneSpanishFrenchG2P-LICENSE.md",
        "Misaki English (Apache-2.0)" to "https://github.com/hexgrad/misaki/blob/fba1236595f2d2bf21d414ba6e57d25256afada3/LICENSE",
        "Misaki BART US model (Apache-2.0)" to "https://huggingface.co/PeterReid/graphemes_to_phonemes_en_us/blob/a5631b285d18d59483c32c0c3379cb9fac924f4b/README.md",
        "Misaki BART GB model (Apache-2.0)" to "https://huggingface.co/PeterReid/graphemes_to_phonemes_en_gb/blob/d8357d5067fa26a5c34134d6bbcf4bbf000c0ac8/README.md",
        "spaCy POS model (MIT)" to "https://github.com/explosion/spacy-models/releases/tag/en_core_web_sm-3.8.0",
        "SIWIS voice dataset attribution (CC-BY-4.0)" to "https://datashare.ed.ac.uk/handle/10283/2353"
    )

    /** Must run off the UI thread. A file is usable only after size and digest match. */
    fun verify(file: File, artifact: Artifact): Boolean {
        if (!file.isFile || file.length() != artifact.bytes) return false
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                if (Thread.currentThread().isInterrupted) throw InterruptedException()
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        val actual = digest.digest().joinToString("") { "%02x".format(it.toInt() and 0xff) }
        return actual == artifact.sha256
    }
}

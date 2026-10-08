package com.learngraphicsprogramming.app.narration.phonemes

import java.io.File
import java.security.MessageDigest

/** Published metadata only: no network request or model installation occurs here. */
object FrenchFluidResources {
    const val codeRevision = "0c0f113e8db4b862b19a99ce9fd7c9da73324f5f"
    const val lexiconRevision = "d93c5f3eed11847fb81bd6d6a76665e133d7ca77"
    const val lexiconFileName = "fr_lexicon_cache.json"
    const val lexiconBytes = 13_625_891L
    const val lexiconSha256 = "9c3073d1dbed06e1eec6bcfecbdbb00df4e826a7b5f8df693a9720f7238c79cd"
    const val lexiconUrl = "https://huggingface.co/FluidInference/kokoro-82m-coreml/resolve/$lexiconRevision/$lexiconFileName"
    const val licenseNoticeUrl = "https://raw.githubusercontent.com/FluidInference/FluidAudio/$codeRevision/ThirdPartyLicenses/KokoroAneSpanishFrenchG2P-LICENSE.md"
    const val fluidAudioLicenseUrl = "https://raw.githubusercontent.com/FluidInference/FluidAudio/$codeRevision/LICENSE"

    // Exact upstream checkpoint used by FluidAudio's own conversion script. Licensing
    // is not sufficiently established for this checkpoint; do not install or distribute it.
    const val auxiliarySourceRevision = "0586e9cb4632aa3909ec9e00539064af70c1a6b2"
    const val auxiliarySourceBytes = 82_672_813L
    const val auxiliarySourceSha256 = "1fdf8971dd0386d1d4a0da95d538a7cce1477cf2754587f818e5ba039a7cf130"
    const val auxiliarySourceUrl = "https://huggingface.co/charsiu/g2p_multilingual_byT5_tiny_16_layers_100/resolve/$auxiliarySourceRevision/pytorch_model.bin"
    const val auxiliaryCommercialDistributionVerified = false

    internal fun requireVerifiedLexicon(file: File) {
        if (!file.isFile || file.length() != lexiconBytes) throw FrenchG2PException("El diccionario francés no está instalado íntegramente")
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().buffered().use { stream ->
            val bytes = ByteArray(64 * 1024)
            while (true) {
                if (Thread.currentThread().isInterrupted) throw InterruptedException()
                val count = stream.read(bytes)
                if (count < 0) break
                digest.update(bytes, 0, count)
            }
        }
        val hash = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
        if (hash != lexiconSha256) throw FrenchG2PException("La integridad SHA-256 del diccionario francés no coincide")
    }
}

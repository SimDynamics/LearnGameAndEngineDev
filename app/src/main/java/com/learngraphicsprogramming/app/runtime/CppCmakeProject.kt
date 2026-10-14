package com.learngraphicsprogramming.app.runtime

import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import java.util.Collections
import java.util.Locale
import java.security.MessageDigest

/** CMake, rather than a manually selected list of .cpp files, owns this build description. */
internal class CppCmakeProject(
    files: Map<String, String>,
    val target: String,
    options: Map<String, String> = emptyMap(),
    val buildType: String = "Debug",
    val compilerVariant: CppCompilerVariant = CppCompilerVariant.DEFAULT
) {
    val files: Map<String, String> = Collections.unmodifiableMap(LinkedHashMap(files))
    val options: Map<String, String> = Collections.unmodifiableMap(LinkedHashMap(options))

    init {
        require(this.files.isNotEmpty() && this.files.size <= 128) { "El proyecto necesita entre 1 y 128 archivos." }
        CppProject.validateFiles(this.files)
        require(this.files.values.sumOf { it.toByteArray(Charsets.UTF_8).size.toLong() } <= 8L * 1024 * 1024) {
            "El proyecto supera el límite de 8 MiB de texto." }
        require(target.matches(Regex("[A-Za-z_][A-Za-z0-9_.+-]{0,127}"))) { "Escribe un nombre de objetivo CMake válido." }
        require(buildType in BUILD_TYPES) { "Tipo de construcción CMake no admitido." }
        require(this.options.size <= 64) { "El proyecto supera el límite de 64 opciones." }
        this.options.forEach { (key, value) -> validateOption(key, value) }
    }

    /** File order is irrelevant; lengths delimit authored text without removing whitespace. */
    val fingerprint: String = fingerprint(false)
    /** Source/header contents may rebuild using existing rules. Changed paths invalidate them. */
    val configurationFingerprint: String = fingerprint(true)

    private fun fingerprint(configurationOnly: Boolean): String {
        val bytes = ByteArrayOutputStream()
        DataOutputStream(bytes).use { out ->
            fun write(value: String) {
                val encoded = value.toByteArray(Charsets.UTF_8)
                out.writeInt(encoded.size)
                out.write(encoded)
            }
            write("lgp-cmake-project-v1")
            write(compilerVariant.name)
            write(buildType)
            if (!configurationOnly) write(target)
            out.writeInt(options.size)
            options.toSortedMap().forEach { (key, value) -> write(key); write(value) }
            out.writeInt(files.size)
            files.toSortedMap().forEach { (path, code) ->
                write(path)
                write(if (configurationOnly && isTranslationInput(path)) "" else code)
            }
        }
        return MessageDigest.getInstance("SHA-256").digest(bytes.toByteArray()).joinToString("") { "%02x".format(it) }
    }

    companion object {
        val BUILD_TYPES = listOf("Debug", "Release", "RelWithDebInfo", "MinSizeRel")
        private fun isTranslationInput(path: String) = CppProject.isSourceFile(path) ||
            path.substringAfterLast('.', "").lowercase(Locale.ROOT) in setOf("h", "hh", "hpp", "hxx", "inc", "inl")

        fun validateOption(key: String, value: String) {
            require(key.matches(Regex("[A-Za-z_][A-Za-z0-9_]{0,127}")) &&
                !key.uppercase(Locale.ROOT).startsWith("CMAKE_") && !key.uppercase(Locale.ROOT).startsWith("LGP_")) {
                "Usa una opción del proyecto; CMAKE_ y LGP_ están reservados al entorno." }
            require(value.length <= 4096 && value.none { it.isISOControl() }) { "El valor de la opción contiene caracteres no admitidos." }
        }

        fun parseOptions(text: String): Map<String, String> = linkedMapOf<String, String>().apply {
            text.lines().forEach { line ->
                if (line.isBlank()) return@forEach
                val separator = line.indexOf('=')
                require(separator > 0) { "Escribe una opción CLAVE=valor en cada línea." }
                val key = line.substring(0, separator).trim()
                val value = line.substring(separator + 1)
                validateOption(key, value)
                require(!containsKey(key)) { "Una opción sólo puede aparecer una vez: $key" }
                put(key, value)
            }
        }
    }
}

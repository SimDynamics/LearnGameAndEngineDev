package com.learngraphicsprogramming.app.narration

import java.io.File
import java.io.IOException

/** Removes declared model files only; unknown files and nonempty directories are preserved. */
internal object KokoroManagedFiles {
    private val revisionPattern = Regex("[0-9a-f]{40}")

    fun removeObsolete(root: File, currentRevision: String, revisions: Set<String>, artifacts: Set<String>) {
        require(currentRevision.matches(revisionPattern)) { "Revisión de Kokoro no válida" }
        require(artifacts.all { it.isNotEmpty() && it != "." && it != ".." && '/' !in it && '\\' !in it }) {
            "Nombre de archivo de Kokoro no válido"
        }
        val canonicalRoot = root.canonicalFile
        // Validate the complete plan before the first deletion, including symlink targets.
        val plan = (revisions - currentRevision).map { revision ->
            require(revision.matches(revisionPattern)) { "Revisión de Kokoro no válida" }
            val directory = File(root, revision)
            if (directory.canonicalFile != File(canonicalRoot, revision)) {
                throw IOException("Directorio de Kokoro no válido")
            }
            if (directory.exists() && !directory.isDirectory) throw IOException("Directorio de Kokoro no válido")
            val files = artifacts.flatMap { listOf(it, "$it.part", "$it.import-part") }.map { name ->
                val file = File(directory, name)
                if (file.canonicalFile.parentFile != directory.canonicalFile || (file.exists() && !file.isFile)) {
                    throw IOException("Archivo de Kokoro no válido")
                }
                file
            }
            directory to files
        }
        plan.forEach { (directory, files) ->
            files.forEach { file ->
                if (file.exists() && !file.delete()) throw IOException("No se pudo eliminar ${file.name}")
            }
            if (directory.isDirectory) {
                val remaining = directory.list() ?: throw IOException("No se pudo comprobar el directorio de Kokoro")
                if (remaining.isEmpty() && !directory.delete()) throw IOException("No se pudo eliminar el directorio de Kokoro")
            }
        }
    }
}

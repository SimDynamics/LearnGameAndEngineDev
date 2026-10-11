package com.learngraphicsprogramming.app.runtime

import java.io.File
import java.util.Collections
import java.util.Locale

/** A snapshot of the editable files and the translation units selected for one build. */
internal class CppProject(
    files: Map<String, String>,
    sourceFiles: List<String> = files.keys.filter(::isSourceFile)
) {
    val files: Map<String, String> = Collections.unmodifiableMap(LinkedHashMap(files))
    val sourceFiles: List<String> = Collections.unmodifiableList(ArrayList(sourceFiles))

    init {
        require(this.files.isNotEmpty()) { "El proyecto debe contener archivos." }
        validateFiles(this.files)
        require(this.sourceFiles.isNotEmpty()) { "Selecciona al menos un archivo fuente C++." }
        require(this.sourceFiles.distinct().size == this.sourceFiles.size) {
            "Un archivo fuente sólo puede seleccionarse una vez."
        }
        this.sourceFiles.forEach { path ->
            validatePath(path)
            require(path in this.files) { "El archivo fuente no pertenece al proyecto: $path" }
            require(isSourceFile(path)) { "Selecciona una fuente C++, no una cabecera: $path" }
        }
    }

    /** Materialize only this snapshot, checking the actual filesystem before every write. */
    internal fun materialize(directory: File): Map<String, File> {
        check(directory.isDirectory || directory.mkdirs()) {
            "No se pudo crear el directorio del proyecto."
        }
        val root = directory.canonicalFile
        val rootPrefix = root.path + File.separator
        val destinations = files.keys.associateWith { path ->
            File(root, path).canonicalFile.also { destination ->
                require(destination.path.startsWith(rootPrefix)) {
                    "El archivo sale del directorio del proyecto: $path"
                }
            }
        }
        destinations.forEach { (path, destination) ->
            val parent = requireNotNull(destination.parentFile)
            check(parent.isDirectory || parent.mkdirs()) {
                "No se pudo crear la carpeta del archivo: $path"
            }
            // Recheck after creating parents, including any filesystem symlinks.
            require(destination.canonicalPath.startsWith(rootPrefix)) {
                "El archivo sale del directorio del proyecto: $path"
            }
            destination.writeText(files.getValue(path), Charsets.UTF_8)
        }
        return destinations
    }

    companion object {
        fun isSourceFile(path: String): Boolean =
            path.substringAfterLast('.', "").lowercase(Locale.ROOT) in
                setOf("cpp", "cc", "cxx", "c++")

        /** Drafts may have only headers or no selected source while being edited. */
        fun validateFiles(files: Map<String, String>) {
            files.keys.forEach(::validatePath)
            files.keys.forEach { path ->
                val segments = path.split('/')
                for (end in 1 until segments.size) {
                    require(segments.take(end).joinToString("/") !in files) {
                        "Un archivo no puede ser también una carpeta del proyecto: $path"
                    }
                }
            }
        }

        /** Portable relative paths keep lesson projects inside their own job directory. */
        fun validatePath(path: String) {
            require(path.isNotBlank() && path == path.trim()) {
                "El archivo necesita una ruta relativa sin espacios al principio o al final."
            }
            require(path.none { it.code < 32 || it in "\\:<>\"|?*" }) {
                "La ruta contiene caracteres no admitidos: $path"
            }
            require(path.split('/').all { it.isNotEmpty() && it != "." && it != ".." }) {
                "Usa una ruta relativa del proyecto, sin segmentos vacíos, . o ..: $path"
            }
        }
    }
}

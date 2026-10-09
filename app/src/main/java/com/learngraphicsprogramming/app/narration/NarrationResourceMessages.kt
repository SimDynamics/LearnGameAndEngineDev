package com.learngraphicsprogramming.app.narration

import android.content.Context
import com.learngraphicsprogramming.app.R
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException

/** Stable persisted problems are rendered in the current UI language at the display boundary. */
internal enum class NarrationResourceProblem(val messageRes: Int) {
    CHECK_FAILED(R.string.narration_resource_check_failed),
    CREATE_DIRECTORY(R.string.narration_resource_create_directory),
    SAVE_DIRECTORY(R.string.narration_resource_save_directory),
    IMPORT_CLEANUP(R.string.narration_resource_import_cleanup),
    DOWNLOAD_CLEANUP(R.string.narration_resource_download_cleanup),
    SAVE_DOWNLOAD(R.string.narration_resource_save_download),
    START_FAILED(R.string.narration_resource_start_failed),
    CANCEL_FAILED(R.string.narration_resource_cancel_failed),
    REMOVE_FILE(R.string.narration_resource_remove_file),
    REMOVE_FAILED(R.string.narration_resource_remove_failed),
    IMPORT_INTERRUPTED(R.string.narration_resource_import_interrupted),
    REPLACE_FILE(R.string.narration_resource_replace_file),
    SAVE_FILE(R.string.narration_resource_save_file),
    DOWNLOAD_DIGEST(R.string.narration_resource_download_digest),
    DOWNLOAD_FAILED(R.string.narration_resource_download_failed),
    DOWNLOAD_INTERRUPTED(R.string.narration_resource_download_interrupted),
    INVALID_FILE(R.string.narration_resource_invalid_file),
    SAVE_REVISION(R.string.narration_resource_save_revision),
    UNKNOWN_ENTRY(R.string.narration_resource_unknown_entry),
    INVALID_ENTRY(R.string.narration_resource_invalid_entry),
    INVALID_SIZE(R.string.narration_resource_invalid_size),
    IMPORT_DIGEST(R.string.narration_resource_import_digest),
    OPEN_PACKAGE(R.string.narration_resource_open_package),
    EMPTY_PACKAGE(R.string.narration_resource_empty_package),
    IMPORT_FAILED(R.string.narration_resource_import_failed),
    INVALID_DIRECTORY(R.string.narration_resource_invalid_directory),
    STORAGE_UNAVAILABLE(R.string.narration_resource_storage_unavailable),
    INVALID_PRIVATE_DIRECTORY(R.string.narration_resource_invalid_private_directory),
    INVALID_PATH(R.string.narration_resource_invalid_path),
    UNKNOWN(R.string.narration_resource_unknown),
}

internal class NarrationResourceException(
    val problem: NarrationResourceProblem,
    vararg val arguments: Any
) : IOException()

internal object NarrationResourceMessages {
    fun encode(problem: NarrationResourceProblem, vararg arguments: Any): String =
        JSONObject().put("code", problem.name).put("arguments", JSONArray(arguments.toList())).toString()

    fun encode(error: Exception, fallback: NarrationResourceProblem): String =
        if (error is NarrationResourceException) encode(error.problem, *error.arguments) else encode(fallback)

    fun normalize(encoded: String): String = runCatching {
        val saved = JSONObject(encoded)
        NarrationResourceProblem.valueOf(saved.getString("code"))
        saved.getJSONArray("arguments")
        encoded
    }.getOrElse { encode(NarrationResourceProblem.UNKNOWN) }

    fun render(context: Context, encoded: String?): String? {
        if (encoded == null) return null
        return runCatching {
            val saved = JSONObject(encoded)
            val problem = NarrationResourceProblem.valueOf(saved.getString("code"))
            val args = saved.getJSONArray("arguments")
            context.getString(problem.messageRes, *Array(args.length()) { args.get(it) })
        }.getOrElse {
            // Legacy releases stored Spanish prose or exception diagnostics. Never expose
            // that stale language after the user changes the interface language.
            context.getString(NarrationResourceProblem.UNKNOWN.messageRes)
        }
    }
}

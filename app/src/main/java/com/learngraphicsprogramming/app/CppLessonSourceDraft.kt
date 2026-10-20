package com.learngraphicsprogramming.app

import android.content.SharedPreferences
import org.json.JSONObject
import java.security.MessageDigest

/** Source is escaped inside the existing preference, not copied into another draft.
 * Some Android/OEM FastXmlSerializer versions indent a closing string tag after a
 * final LF, adding four characters to the value when the next process reads it.
 * JSON keeps that LF escaped and preserves every original source character.
 */
internal object CppLessonSourceDraft {
    internal const val ENCODING = "json-v1"

    fun read(preferences: SharedPreferences, draftKey: String, initialCode: String): String {
        val stored = preferences.getString("$draftKey/source", null) ?: return initialCode
        return decode(stored, preferences.getString("$draftKey/sourceEncoding", null),
            preferences.getString("$draftKey/sourceIndentation_v2", null))
    }

    fun write(editor: SharedPreferences.Editor, draftKey: String, source: CharSequence): SharedPreferences.Editor =
        editor.putString("$draftKey/source", encode(source.toString()))
            .putString("$draftKey/sourceEncoding", ENCODING)
            .putString("$draftKey/sourceIndentation_v2", CppIndentationUnits.snapshot(source))

    internal fun encode(source: String): String = JSONObject().put("version", 1).put("code", source).toString()

    internal fun decode(stored: String, encoding: String?, indentation: String?): String {
        if (encoding == ENCODING) return runCatching {
            val value = JSONObject(stored)
            require(value.opt("version") == 1)
            value.get("code") as String
        }.getOrElse { stored } // Preserve unrecognized data rather than replace it with initialCode.
        if (encoding != null || !stored.endsWith("\n    ")) return stored

        // Recover only the exact legacy corruption proved by the already-recorded
        // source SHA. Manual whitespace, missing metadata and every other change stay.
        val expected = runCatching {
            val value = JSONObject(indentation ?: return stored)
            require(value.opt("version") == 1)
            (value.get("sha256") as String).also { require(it.matches(Regex("[0-9a-f]{64}"))) }
        }.getOrElse { return stored }
        if (hash(stored) == expected) return stored
        val candidate = stored.dropLast(4)
        return if (hash(candidate) == expected) candidate else stored
    }

    private fun hash(source: String): String = MessageDigest.getInstance("SHA-256")
        .digest(source.toByteArray(Charsets.UTF_8)).joinToString("") { "%02x".format(it.toInt() and 0xff) }
}

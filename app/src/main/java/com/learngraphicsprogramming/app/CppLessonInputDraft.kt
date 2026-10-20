package com.learngraphicsprogramming.app

import android.content.SharedPreferences

/** Prepared stdin is data: all spaces, tabs and final line breaks are significant.
 * Use the same escaped-text format as source drafts, in the existing input key.
 * Old stdin has no recorded digest, so never guess which trailing spaces are user data.
 */
internal object CppLessonInputDraft {
    fun read(preferences: SharedPreferences, draftKey: String, initialInput: String): String {
        val stored = preferences.getString("$draftKey/input", null) ?: return initialInput
        return decode(stored, preferences.getString("$draftKey/inputEncoding", null))
    }

    fun write(editor: SharedPreferences.Editor, draftKey: String, input: CharSequence): SharedPreferences.Editor =
        editor.putString("$draftKey/input", CppDraftTextCodec.encode(input.toString(), "input"))
            .putString("$draftKey/inputEncoding", CppDraftTextCodec.ENCODING)

    internal fun decode(stored: String, encoding: String?): String =
        if (encoding == CppDraftTextCodec.ENCODING) CppDraftTextCodec.decode(stored, "input") ?: stored
        else stored
}

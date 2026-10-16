package com.learngraphicsprogramming.app

import android.content.Context
import org.json.JSONArray

object KeyboardShortcutSettingsRepository {

    private const val PREFERENCES_NAME =
        "keyboard_shortcut_settings"

    private const val KEY_SHORTCUTS =
        "cpp_shortcuts"

    private val DEFAULT_SHORTCUTS =
        listOf(
            "(",
            "{",
            "[",
            ";",
            "\"",
            "<",
            ">",
            "=",
            "+",
            "-",
            "*",
            "/",
            "&",
            "|",
            "!",
            ",",
            ":",
            "::",
            "<<",
            ">>"
        )

    fun getShortcuts(
        context: Context
    ): List<String> {

        val preferences =
            context.getSharedPreferences(
                PREFERENCES_NAME,
                Context.MODE_PRIVATE
            )

        val stored =
            preferences.getString(
                KEY_SHORTCUTS,
                null
            )

        if (
            stored.isNullOrEmpty()
        ) {
            return DEFAULT_SHORTCUTS
        }

        return try {

            val array =
                JSONArray(
                    stored
                )

            val result =
                mutableListOf<String>()

            for (
            index in 0 until
                    array.length()
            ) {

                result.add(
                    array.optString(
                        index,
                        ""
                    )
                )
            }

            result

        } catch (
            exception: Exception
        ) {

            DEFAULT_SHORTCUTS
        }
    }

    fun saveShortcuts(
        context: Context,
        shortcuts: List<String>
    ) {

        val array =
            JSONArray()

        for (
        shortcut in shortcuts
        ) {

            array.put(
                shortcut
            )
        }

        context.getSharedPreferences(
            PREFERENCES_NAME,
            Context.MODE_PRIVATE
        )
            .edit()
            .putString(
                KEY_SHORTCUTS,
                array.toString()
            )
            .apply()
    }

    fun resetShortcuts(
        context: Context
    ) {

        context.getSharedPreferences(
            PREFERENCES_NAME,
            Context.MODE_PRIVATE
        )
            .edit()
            .remove(
                KEY_SHORTCUTS
            )
            .apply()
    }

    fun getDefaultShortcuts():
            List<String> {

        return DEFAULT_SHORTCUTS
    }

    fun getTabText(): String {

        return "\t"
    }
}
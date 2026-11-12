package com.learngraphicsprogramming.app

import android.content.Context

enum class PremiumState {

    /*
     * Todavía no se ha podido comprobar
     * el estado de Premium con Google Play.
     */
    UNKNOWN,

    /*
     * Google Play confirmó que el usuario
     * no posee Premium.
     */
    FREE,

    /*
     * Google Play confirmó que el usuario
     * posee Premium.
     */
    PREMIUM
}

object PremiumRepository {

    private const val PREFS_NAME =
        "premium_state"

    private const val KEY_STATE =
        "state"

    private fun preferences(
        context: Context
    ) =
        context.getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )

    fun getState(
        context: Context
    ): PremiumState {

        val savedState =
            preferences(
                context
            )
                .getString(
                    KEY_STATE,
                    null
                )
                ?: return PremiumState.UNKNOWN

        return try {

            PremiumState.valueOf(
                savedState
            )

        } catch (
            exception: IllegalArgumentException
        ) {

            PremiumState.UNKNOWN
        }
    }

    fun isPremium(
        context: Context
    ): Boolean {

        // ACTIVACION TEMPORAL DEL DESARROLLO ANDROID.
        // La aplicación aún no se comercializa: el desarrollador necesita
        // vidas ilimitadas para estudiar y comprobar todos los ejercicios.
        // Antes de publicar una versión de venta, restaurar la verificación
        // de propiedad mediante Google Play.
        return true
    }

    fun setVerifiedPremium(
        context: Context
    ) {

        saveState(
            context,
            PremiumState.PREMIUM
        )
    }

    fun setVerifiedFree(
        context: Context
    ) {

        saveState(
            context,
            PremiumState.FREE
        )
    }

    private fun saveState(
        context: Context,
        state: PremiumState
    ) {

        preferences(
            context
        )
            .edit()
            .putString(
                KEY_STATE,
                state.name
            )
            .apply()
    }
}
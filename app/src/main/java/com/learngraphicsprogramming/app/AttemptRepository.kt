package com.learngraphicsprogramming.app

import android.content.Context

data class AttemptState(
    val available: Int,
    val maximum: Int,
    val reserveUnits: Float,
    val totalReserveProgress: Float,
    val unlimited: Boolean
) {

    val canEvaluate: Boolean
        get() =
            unlimited ||
                    available > 0
}

object AttemptRepository {

    const val RECOVERY_MILLIS =
        60L * 1000L

    private const val PREFS_NAME =
        "attempt_reserve_seconds_test"

    private const val KEY_UNLIMITED =
        "unlimited"

    private const val PREFIX_RESERVE =
        "reserve_"

    private const val PREFIX_UPDATED_AT =
        "updated_at_"

    private fun preferences(
        context: Context
    ) =
        context.getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )

    fun maximumForRoute(
        routeId: String
    ): Int {

        return if (
            routeId ==
            AppearanceSettingsRepository.ROUTE_MATH_PHYSICS
        ) {
            12
        } else {
            8
        }
    }

    fun getState(
        context: Context,
        routeId: String,
        now: Long = System.currentTimeMillis()
    ): AttemptState {

        val maximum =
            maximumForRoute(
                routeId
            )

        val prefs =
            preferences(
                context
            )

        val reserveKey =
            PREFIX_RESERVE +
                    routeId

        val updatedAtKey =
            PREFIX_UPDATED_AT +
                    routeId

        val savedReserve =
            prefs.getFloat(
                reserveKey,
                maximum.toFloat()
            )
                .coerceIn(
                    0f,
                    maximum.toFloat()
                )

        val updatedAt =
            prefs.getLong(
                updatedAtKey,
                now
            )

        val elapsed =
            (
                    now -
                            updatedAt
                    )
                .coerceAtLeast(
                    0L
                )

        val recoveredUnits =
            elapsed.toFloat() /
                    RECOVERY_MILLIS.toFloat()

        val reserveUnits =
            (
                    savedReserve +
                            recoveredUnits
                    )
                .coerceIn(
                    0f,
                    maximum.toFloat()
                )

        val available =
            reserveUnits
                .toInt()
                .coerceIn(
                    0,
                    maximum
                )

        val totalReserveProgress =
            (
                    reserveUnits /
                            maximum.toFloat()
                    )
                .coerceIn(
                    0f,
                    1f
                )

        return AttemptState(
            available =
                available,
            maximum =
                maximum,
            reserveUnits =
                reserveUnits,
            totalReserveProgress =
                totalReserveProgress,
            unlimited =
                isUnlimited(
                    context
                )
        )
    }

    fun consumeIncorrect(
        context: Context,
        routeId: String,
        now: Long = System.currentTimeMillis()
    ): AttemptState {

        val state =
            getState(
                context,
                routeId,
                now
            )

        if (
            state.unlimited ||
            state.available <= 0
        ) {
            return state
        }

        preferences(
            context
        )
            .edit()
            .putFloat(
                PREFIX_RESERVE +
                        routeId,
                (
                        state.reserveUnits -
                                1f
                        )
                    .coerceAtLeast(
                        0f
                    )
            )
            .putLong(
                PREFIX_UPDATED_AT +
                        routeId,
                now
            )
            .apply()

        return getState(
            context,
            routeId,
            now
        )
    }

    fun setUnlimited(
        context: Context,
        unlimited: Boolean
    ) {

        preferences(
            context
        )
            .edit()
            .putBoolean(
                KEY_UNLIMITED,
                unlimited
            )
            .apply()
    }

    fun isUnlimited(
        context: Context
    ): Boolean {

        return preferences(
            context
        )
            .getBoolean(
                KEY_UNLIMITED,
                false
            )
    }
}
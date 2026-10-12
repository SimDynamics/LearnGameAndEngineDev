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
        60L * 60L * 1000L

    private const val PREFS_NAME =
        "attempt_reserve_seconds_test"

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

    /** Keep the earned balance once when upgrading the old one-minute test build. */
    @Synchronized
    private fun ensureHourlyRecovery(prefs: android.content.SharedPreferences,
                                     routeId: String, maximum: Int, now: Long) {
        val intervalKey = "recovery_interval_" + routeId
        if (prefs.getLong(intervalKey, 0L) == RECOVERY_MILLIS) return
        val reserveKey = PREFIX_RESERVE + routeId
        val timeKey = PREFIX_UPDATED_AT + routeId
        val editor = prefs.edit().putLong(intervalKey, RECOVERY_MILLIS)
        if (prefs.contains(reserveKey) && prefs.contains(timeKey)) {
            // Legacy conversion only; all subsequent regeneration uses one full hour.
            val previousInterval = prefs.getLong(intervalKey, 60_000L).coerceAtLeast(1L)
            val elapsed = (now - prefs.getLong(timeKey, now)).coerceAtLeast(0L)
            val saved = prefs.getFloat(reserveKey, maximum.toFloat()).toDouble()
            val balance = (saved + elapsed.toDouble() / previousInterval)
                .coerceIn(0.0, maximum.toDouble()).toFloat()
            editor.putFloat(reserveKey, balance).putLong(timeKey, now)
        }
        editor.apply()
    }

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

        ensureHourlyRecovery(prefs, routeId, maximum, now)

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
                PremiumRepository.isPremium(
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
}
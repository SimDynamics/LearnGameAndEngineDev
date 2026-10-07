package com.learngraphicsprogramming.app

import android.content.Context

data class AttemptState(
    val available: Int,
    val maximum: Int,
    val nextRecoveryAt: Long?,
    val nextRecoveryProgress: Float,
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
        "attempt_state"

    private const val KEY_UNLIMITED =
        "unlimited"

    private const val PREFIX_PENDING =
        "pending_"

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

        val savedPending =
            loadPending(
                context,
                routeId
            )

        val pending =
            savedPending
                .filter {
                    it > now
                }
                .sorted()
                .take(
                    maximum
                )

        if (
            pending != savedPending
        ) {
            savePending(
                context,
                routeId,
                pending
            )
        }

        val available =
            (
                    maximum -
                            pending.size
                    )
                .coerceIn(
                    0,
                    maximum
                )

        val nextRecoveryAt =
            pending.firstOrNull()

        val nextRecoveryProgress =
            if (
                nextRecoveryAt == null
            ) {
                1f
            } else {

                val recoveryStartedAt =
                    nextRecoveryAt -
                            RECOVERY_MILLIS

                (
                        (
                                now -
                                        recoveryStartedAt
                                ).toFloat() /
                                RECOVERY_MILLIS.toFloat()
                        )
                    .coerceIn(
                        0f,
                        1f
                    )
            }

        return AttemptState(
            available =
                available,
            maximum =
                maximum,
            nextRecoveryAt =
                nextRecoveryAt,
            nextRecoveryProgress =
                nextRecoveryProgress,
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

        val pending =
            loadPending(
                context,
                routeId
            )
                .filter {
                    it > now
                }
                .toMutableList()

        pending.add(
            now +
                    RECOVERY_MILLIS
        )

        savePending(
            context,
            routeId,
            pending.sorted()
        )

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

    private fun loadPending(
        context: Context,
        routeId: String
    ): List<Long> {

        val raw =
            preferences(
                context
            )
                .getString(
                    PREFIX_PENDING +
                            routeId,
                    ""
                )
                .orEmpty()

        if (
            raw.isBlank()
        ) {
            return emptyList()
        }

        return raw
            .split(",")
            .mapNotNull {
                it.toLongOrNull()
            }
            .sorted()
    }

    private fun savePending(
        context: Context,
        routeId: String,
        pending: List<Long>
    ) {

        preferences(
            context
        )
            .edit()
            .putString(
                PREFIX_PENDING +
                        routeId,
                pending.joinToString(
                    ","
                )
            )
            .apply()
    }
}
package com.learngraphicsprogramming.app

/** Selects existing result channels without interpreting lesson content or changing their text. */
internal object CppLabPresentationPolicy {
    enum class Channel { DIAGNOSTICS, STDOUT, STDERR }

    data class ConsoleState(val channels: List<Channel>, val selected: Channel)

    fun console(
        diagnostics: String,
        stdout: String,
        stderr: String,
        preferred: Channel? = null
    ): ConsoleState {
        // Whitespace can be the actual output of the program; only an empty string lacks data.
        val channels = buildList {
            if (diagnostics.isNotEmpty()) add(Channel.DIAGNOSTICS)
            if (stdout.isNotEmpty()) add(Channel.STDOUT)
            if (stderr.isNotEmpty()) add(Channel.STDERR)
        }.ifEmpty { listOf(Channel.STDOUT) }
        val selected = preferred?.takeIf { it in channels }
            ?: listOf(Channel.STDOUT, Channel.STDERR, Channel.DIAGNOSTICS).first { it in channels }
        return ConsoleState(channels, selected)
    }
}

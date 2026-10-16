package com.learngraphicsprogramming.app

import com.learngraphicsprogramming.app.CppLabPresentationPolicy.Channel
import org.junit.Assert.assertEquals
import org.junit.Test

class CppLabPresentationPolicyTest {
    @Test fun anUnusedConsoleHasOneEmptyOutputChannel() {
        assertState(listOf(Channel.STDOUT), Channel.STDOUT, "", "", "")
    }

    @Test fun aSimpleProgramNeedsOnlyItsStandardOutput() {
        assertState(listOf(Channel.STDOUT), Channel.STDOUT, "", "Caja preparada\n", "")
    }

    @Test fun aCompilationFailureShowsOnlyItsActualDiagnostics() {
        assertState(listOf(Channel.DIAGNOSTICS), Channel.DIAGNOSTICS,
            "main.cpp:2: error: expected ';'", "", "")
    }

    @Test fun aRejectedRuntimeRequestShowsItsErrorWithoutAnEmptyOutputOption() {
        assertState(listOf(Channel.STDERR), Channel.STDERR, "", "", "Cantidad fuera del intervalo\n")
    }

    @Test fun independentProgramChannelsRemainSelectableAndOutputIsTheDefault() {
        assertState(listOf(Channel.STDOUT, Channel.STDERR), Channel.STDOUT,
            "", "Aceptadas: 2\n", "Pedido 3 rechazado\n")
    }

    @Test fun successfulExecutionDoesNotHideNonemptyBuildWarnings() {
        assertState(listOf(Channel.DIAGNOSTICS, Channel.STDOUT), Channel.STDOUT,
            "cantidad.cpp:4: warning: comparison is always true", "Bateria: ERROR\n", "")
    }

    @Test fun runtimeErrorsTakePriorityOverDiagnosticsWhenThereIsNoStandardOutput() {
        assertState(listOf(Channel.DIAGNOSTICS, Channel.STDERR), Channel.STDERR,
            "Construccion completada\n", "", "Entrada incompleta\n")
    }

    @Test fun eachAvailableChannelCanBeExplicitlySelectedWithoutChangingOptions() {
        for (preferred in listOf(Channel.DIAGNOSTICS, Channel.STDOUT, Channel.STDERR)) {
            assertState(listOf(Channel.DIAGNOSTICS, Channel.STDOUT, Channel.STDERR), preferred,
                "warning\n", "resultado\n", "aviso del programa\n", preferred)
        }
    }

    @Test fun aChannelSelectedForAnEarlierRunCannotSurviveWhenItHasNoData() {
        assertState(listOf(Channel.STDOUT), Channel.STDOUT,
            "", "Ejecucion corregida\n", "", Channel.DIAGNOSTICS)
        assertState(listOf(Channel.STDERR), Channel.STDERR,
            "", "", "Entrada invalida\n", Channel.STDOUT)
        assertState(listOf(Channel.STDOUT), Channel.STDOUT,
            "", "", "", Channel.STDERR)
    }

    @Test fun spacesTabsAndLineBreaksAreRealChannelData() {
        assertState(listOf(Channel.DIAGNOSTICS, Channel.STDOUT, Channel.STDERR), Channel.STDOUT,
            "\n", "  \t\n", "\t")
        assertState(listOf(Channel.STDOUT), Channel.STDOUT, "", " ", "")
        assertState(listOf(Channel.STDERR), Channel.STDERR, "", "", "\n")
    }

    private fun assertState(
        channels: List<Channel>,
        selected: Channel,
        diagnostics: String,
        stdout: String,
        stderr: String,
        preferred: Channel? = null
    ) {
        val actual = CppLabPresentationPolicy.console(diagnostics, stdout, stderr, preferred)
        assertEquals(channels, actual.channels)
        assertEquals(selected, actual.selected)
    }
}

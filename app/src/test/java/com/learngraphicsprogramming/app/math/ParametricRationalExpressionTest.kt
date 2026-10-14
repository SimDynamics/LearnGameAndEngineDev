package com.learngraphicsprogramming.app.math

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.fail
import org.junit.Test

class ParametricRationalExpressionTest {
    private val parameters = listOf("a", "b", "c", "d", "u", "v")
    private val definitions = mapOf("D" to "a*d-b*c")
    private val correct = ParametricRationalExpression.Comparison.CORRECT
    private val valueMismatch = ParametricRationalExpression.Comparison.VALUE_MISMATCH
    private val domainMismatch = ParametricRationalExpression.Comparison.DOMAIN_MISMATCH
    private fun contract(expected: String) = ParametricRationalExpression.compile(parameters, definitions, listOf("D"), expected)

    @Test fun criterionAndBothNumeratorsUseExactMultivariablePolynomialIdentity() {
        for ((expected, variants) in listOf(
            "a*d-b*c" to listOf("d*a-c*b", "ad-bc", "D", "-(bc-ad)", "ad-(b/2)*(2*c)"),
            "d*u-b*v" to listOf("ud-vb", "u*d-v*b", "-(bv-du)", "(2du-2bv)/2"),
            "a*v-c*u" to listOf("v*a-u*c", "av-cu", "-(cu-av)", "av+(-c)*u"))) {
            val compiled = contract(expected)
            variants.forEach { assertEquals(it, correct, compiled.compare(it)) }
        }
        assertEquals(valueMismatch, contract("D").compare("ad+bc"))
        assertEquals(valueMismatch, contract("du-bv").compare("av-cu"))
    }

    @Test fun allPublishedXFormsAndReorderedSignsAndProductsAreAccepted() {
        val compiled = contract("(d*u-b*v)/D")
        for (answer in listOf("(d*u-b*v)/(a*d-b*c)", "(b*v-d*u)/(b*c-a*d)", "d*u/D-b*v/D",
            "((d*u-b*v)*D)/(D*D)", "(ud-vb)/(da-cb)", "-(bv-du)/D",
            "(2du-2bv)/(2D)", "(du-bv)/(-bc+ad)", "(du-bv)*(-D)/(-D*D)",
            "((du-bv)*D^2)/D^3", "((du-bv)*D²)/(D*D*D)", "((du-bv)*D^7)/D^8",
            "((du-bv)/D)/(D/D)", "(du-bv)/(1/(1/D))", "(du-bv)*(1/D)",
            "(du-bv)/(D/2)/2", "(du-bv)/D*(D/D)^0")) {
            assertEquals(answer, correct, compiled.compare(answer))
        }
    }

    @Test fun yFormsAndBothTransferFormulaePreserveTheEntireRightHandSides() {
        val y = contract("(a*v-c*u)/D")
        for (answer in listOf("(v*a-u*c)/(d*a-c*b)", "(cu-av)/(bc-ad)", "av/D-cu/D",
            "((av-cu)*D)/(D*D)", "(av-cu)/(1/(1/D))")) {
            assertEquals(answer, correct, y.compare(answer))
        }
        val transferX = contract("(d*(u+v)-b*(u-v))/D")
        for (answer in listOf("(du+dv-bu+bv)/D", "u*(d-b)/D+v*(d+b)/D",
            "((u+v)*d-(u-v)*b)/(ad-bc)", "(b*(u-v)-d*(u+v))/(bc-ad)")) {
            assertEquals(answer, correct, transferX.compare(answer))
        }
        val transferY = contract("(a*(u-v)-c*(u+v))/D")
        for (answer in listOf("(au-av-cu-cv)/D", "u*(a-c)/D-v*(a+c)/D",
            "(c*(u+v)-a*(u-v))/(bc-ad)")) {
            assertEquals(answer, correct, transferY.compare(answer))
        }
        assertEquals(valueMismatch, transferX.compare("(d*u+v-b*u-v)/D"))
        assertEquals(valueMismatch, transferY.compare("(a*u-v-c*u+v)/D"))
    }

    @Test fun completeValueErrorsAreEvaluableEvenIfTheirChangedDivisorIsNotGuaranteed() {
        val compiled = contract("(du-bv)/D")
        for (answer in listOf("(du+bv)/D", "(av-cu)/D", "(du-bv)/(ad+bc)",
            "(du-bv)/(-D)", "(du-bv)*D", "(du-bv)/(D*D)", "(du-bv)/d")) {
            assertEquals(answer, valueMismatch, compiled.compare(answer))
        }
    }

    @Test fun formallyCancelledParametersLoseAnAllowedPartOfTheDeclaredDomain() {
        val compiled = contract("(du-bv)/D")
        for (parameter in parameters) {
            val answer = "$parameter*(du-bv)/($parameter*D)"
            assertEquals(answer, domainMismatch, compiled.compare(answer))
        }
        assertEquals(domainMismatch, compiled.compare("d*(du-bv)/(d*D)"))
        assertEquals(domainMismatch, compiled.compare("b*(du-bv)/(b*D)"))
    }

    @Test fun divisionObligationsSurviveZeroTermsAndPowersOfZero() {
        val compiled = contract("(du-bv)/D")
        for (answer in listOf("(du-bv)/D+0/d", "(du-bv)/D+0/b", "(du-bv)/D+(d/d)^0-1",
            "(du-bv)/D*(d/d)^0", "(du-bv)/D+0*(1/d)", "(du-bv)/D/(d/d)",
            "(du-bv)/D+0/(d*D)", "(du-bv)/D+(0/d)^0-1")) {
            assertEquals(answer, domainMismatch, compiled.compare(answer))
        }
        for (answer in listOf("(du-bv)/D+0/D", "(du-bv)/D*(D/D)^0", "(du-bv)/D+(0/D)^0-1")) {
            assertEquals(answer, correct, compiled.compare(answer))
        }
        for (answer in listOf("(du-bv)/D+0/0", "(du-bv)/D*(1/0)^0", "(du-bv)/D+0/(d-d)")) {
            assertEquals(answer, domainMismatch, compiled.compare(answer))
        }
    }

    @Test fun undecidableNonzeroObligationsStayTechnicalEvenWhenTheFormulaIsValid() {
        val compiled = contract("(du-bv)/D")
        for (answer in listOf("(du-bv)/D*((D^2+1)/(D^2+1))",
            "(du-bv)/D+0/(d^2+1)", "(du-bv)/D*(a+1)/(a+1)")) {
            unsupported("unproved_domain") { compiled.compare(answer) }
        }
        // A later proved invalid obligation must not be hidden by an earlier undecided one.
        assertEquals(domainMismatch, compiled.compare("(du-bv)/D+0/(D^2+1)+0/d"))
    }

    @Test fun definitionsAreExplicitRevisableAndMayDependOnOtherDeclaredAliases() {
        val compiled = ParametricRationalExpression.compile(parameters,
            linkedMapOf("Nx" to "du-bv", "E" to "2*D", "D" to "ad-bc", "Ny" to "av-cu"),
            listOf("D"), "Nx/D")
        assertEquals(correct, compiled.compare("2*Nx/E"))
        assertEquals(valueMismatch, compiled.compare("Ny/D"))
        val revised = ParametricRationalExpression.compile(parameters, mapOf("D" to "ad+bc"), listOf("D"), "D")
        assertEquals(correct, revised.compare("ad+bc"))
        assertEquals(valueMismatch, revised.compare("ad-bc"))
        unsupported("unknown_name") { ParametricRationalExpression.compile(parameters, emptyMap(), emptyList(), "D") }
        unsupported("unknown_name") { compiled.compare("x") }
        unsupported("unknown_name") { compiled.compare("y") }
    }

    @Test fun nonzeroHypothesesAuthorizeOnlyTheirExplicitPolynomialFactors() {
        val dAllowed = ParametricRationalExpression.compile(parameters, definitions, listOf("D", "d"), "(du-bv)/D")
        assertEquals(correct, dAllowed.compare("d*(du-bv)/(d*D)"))
        assertEquals(domainMismatch, dAllowed.compare("b*(du-bv)/(b*D)"))
        val squared = ParametricRationalExpression.compile(parameters, definitions, listOf("D^2"), "(du-bv)/D")
        assertEquals(correct, squared.compare("(du-bv)/D"))
        val product = ParametricRationalExpression.compile(parameters, definitions, listOf("d*D"), "(du-bv)/D")
        assertEquals(correct, product.compare("d*(du-bv)/(d*D)"))
        assertEquals(correct, contract("D/(-2)").compare("-D/2"))
    }

    @Test fun malformedOrUnsupportedSpecificationsFailBeforeAnAnswerIsRequested() {
        unsupported("definition_cycle") {
            ParametricRationalExpression.compile(parameters, mapOf("D" to "E", "E" to "D"), listOf("D"), "D")
        }
        unsupported("definition_collision") {
            ParametricRationalExpression.compile(parameters, mapOf("a" to "b"), emptyList(), "a")
        }
        unsupported("parameters") {
            ParametricRationalExpression.compile(listOf("a", "a"), emptyMap(), emptyList(), "a")
        }
        unsupported("definition_polynomial") {
            ParametricRationalExpression.compile(parameters, mapOf("D" to "1/d"), listOf("D"), "D")
        }
        unsupported("zero_hypothesis") { ParametricRationalExpression.compile(parameters, definitions, listOf("a-a"), "D") }
        unsupported("expected_domain") { contract("1/d") }
        unsupported("expected_domain") { contract("0/0") }
        unsupported("nonzero_polynomial") { ParametricRationalExpression.compile(parameters, definitions, listOf("1/d"), "D") }
        val minusDefinition = ParametricRationalExpression.compile(parameters, mapOf("D" to "(ad-bc)/(-1)"), listOf("D"), "D")
        assertEquals(correct, minusDefinition.compare("bc-ad"))
    }

    @Test fun notationFailuresAreTechnicalInsteadOfIncorrectOrCorrectVerdicts() {
        val compiled = contract("(du-bv)/D")
        for (answer in listOf("", "  ", "du-bv)/D", "(du-bv/D", "(du-bv)/", "sin(a)",
            "sqrt(D)", "D^a", "D^-1", "D^9", "D^2^2", "a=0", "1.2.3", "2 3", "D,0", "x+y")) {
            try { compiled.compare(answer); fail("Technical retry required: $answer") }
            catch (_: ParametricRationalExpression.UnsupportedException) { }
        }
    }

    @Test fun exactSubstitutionConfirmsZeroIndividualDataAndRejectsAddedDivisions() {
        val x = contract("(du-bv)/D")
        val y = contract("(av-cu)/D")
        val dZero = mapOf("a" to "0", "b" to "2", "c" to "-3", "d" to "0", "u" to "5", "v" to "-4")
        assertEquals("6", x.evaluate("D", dZero).toString())
        assertEquals("4/3", x.evaluate("(du-bv)/D", dZero).toString())
        assertEquals("5/2", y.evaluate("(av-cu)/D", dZero).toString())
        assertNull(x.evaluate("d*(du-bv)/(d*D)", dZero))
        val bZero = mapOf("a" to "2", "b" to "0", "c" to "-1", "d" to "3", "u" to "5", "v" to "4")
        assertEquals("6", x.evaluate("D", bZero).toString())
        assertEquals("5/2", x.evaluate("(du-bv)/D", bZero).toString())
        assertEquals("13/6", y.evaluate("(av-cu)/D", bZero).toString())
        assertNull(x.evaluate("b*(du-bv)/(b*D)", bZero))
    }

    @Test fun exactWitnessesIncludeNegativeDeterminantFractionsHomogeneousAndTransfer() {
        val x = contract("(du-bv)/D")
        val negative = mapOf("a" to "1", "b" to "2", "c" to "3", "d" to "4", "u" to "5", "v" to "6")
        assertEquals("-2", x.evaluate("D", negative).toString())
        assertEquals("-4", x.evaluate("(du-bv)/D", negative).toString())
        assertEquals("9/2", x.evaluate("(av-cu)/D", negative).toString())
        val fractions = mapOf("a" to "1/2", "b" to "1/3", "c" to "-2/3", "d" to "3/4", "u" to "5/6", "v" to "-7/8")
        assertEquals("43/72", x.evaluate("D", fractions).toString())
        assertEquals("66/43", x.evaluate("(du-bv)/D", fractions).toString())
        assertEquals("17/86", x.evaluate("(av-cu)/D", fractions).toString())
        val homogeneous = negative + mapOf("u" to "0", "v" to "0")
        assertEquals("0", x.evaluate("(du-bv)/D", homogeneous).toString())
        assertEquals("0", x.evaluate("(av-cu)/D", homogeneous).toString())
        val tx = "(d*(u+v)-b*(u-v))/D"
        val ty = "(a*(u-v)-c*(u+v))/D"
        assertEquals("-23", x.evaluate(tx, negative).toString())
        assertEquals("17", x.evaluate(ty, negative).toString())
        assertEquals("11", x.evaluate("a*($tx)+b*($ty)", negative).toString())
        assertEquals("-1", x.evaluate("c*($tx)+d*($ty)", negative).toString())
        unsupported("outside_domain") { x.evaluate("(du-bv)/D", negative + mapOf("c" to "2", "d" to "4")) }
    }

    @Test fun originalSystemSufficiencyUsesIdentitiesRatherThanFiniteWitnesses() {
        val definitions = this.definitions + mapOf("X" to "du-bv", "Y" to "av-cu")
        val first = ParametricRationalExpression.compile(parameters, definitions, listOf("D"), "u")
        val second = ParametricRationalExpression.compile(parameters, definitions, listOf("D"), "v")
        assertEquals(correct, first.compare("a*X/D+b*Y/D"))
        assertEquals(correct, second.compare("c*X/D+d*Y/D"))
        assertEquals(valueMismatch, first.compare("a*X/D-b*Y/D"))
    }

    @Test fun exactCoefficientsDoNotRoundDecimalNeighboursAndRationalPowersAreBounded() {
        val compiled = contract("D/3")
        assertEquals(correct, compiled.compare("(2*D)/6"))
        assertEquals(valueMismatch, compiled.compare("0.3333333333333333*D"))
        assertEquals(correct, contract("D^2").compare("a^2*d^2-2*a*b*c*d+b^2*c^2"))
        assertEquals(correct, contract("D").compare("D^(1)"))
        assertEquals(valueMismatch, contract("D").compare("D^0"))
    }

    @Test fun inclusiveSourceLengthAndOtherResourceBoundsStayTechnical() {
        val compiled = contract("D")
        assertEquals(correct, compiled.compare(" ".repeat(ParametricRationalExpression.MAX_SOURCE_LENGTH - 1) + "D"))
        resource("source_length") { compiled.compare(" ".repeat(ParametricRationalExpression.MAX_SOURCE_LENGTH) + "D") }
        resource("literal_digits") { compiled.compare("9".repeat(ParametricRationalExpression.MAX_LITERAL_DIGITS + 1)) }
        resource("depth") { compiled.compare("(".repeat(65) + "D" + ")".repeat(65)) }
        resource("terms") { compiled.compare("(a+b+c+d+u+v+1)^8") }
        resource("degree") { compiled.compare("(D^8)^8") }
        resource("coefficient_bits") { compiled.compare("(" + "9".repeat(1024) + ")^2") }
        resource("contract_size") { ParametricRationalExpression.compile((1..9).map { "p$it" }, emptyMap(), emptyList(), "p1") }
        val nestedAliases = (0..15).associate { index ->
            "Alias$index" to "(".repeat(18) + (if (index == 15) "a" else "Alias${index + 1}") + ")".repeat(18)
        }
        resource("depth") { ParametricRationalExpression.compile(parameters, nestedAliases, emptyList(), "Alias0") }
    }

    private fun unsupported(reason: String, action: () -> Any?) {
        try { action(); fail("Expected technical unsupported state: $reason") }
        catch (exception: ParametricRationalExpression.UnsupportedException) { assertEquals(reason, exception.reason) }
    }
    private fun resource(reason: String, action: () -> Any?) {
        try { action(); fail("Expected technical resource limit: $reason") }
        catch (exception: ParametricRationalExpression.ResourceLimitException) { assertEquals(reason, exception.reason) }
    }
}

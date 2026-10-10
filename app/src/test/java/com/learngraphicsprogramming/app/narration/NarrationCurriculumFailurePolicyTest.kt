package com.learngraphicsprogramming.app.narration

import java.io.FileNotFoundException
import java.io.IOException
import org.json.JSONException
import org.junit.Assert.assertEquals
import org.junit.Test

class NarrationCurriculumFailurePolicyTest {
    @Test fun optionalMissingAssetsProduceOnlyOneCompactEventPerPath() {
        val policy = NarrationCurriculumFailurePolicy()
        val first = "content/es/programming/course_20/units.json"
        val second = "content/es/programming/course_21/units.json"
        assertEquals(NarrationCurriculumFailurePolicy.Decision.FIRST_MISSING,
            policy.classify(first, FileNotFoundException(first)))
        repeat(5) {
            assertEquals(NarrationCurriculumFailurePolicy.Decision.REPEATED_MISSING,
                policy.classify(first, FileNotFoundException(first)))
        }
        assertEquals(NarrationCurriculumFailurePolicy.Decision.FIRST_MISSING,
            policy.classify(second, FileNotFoundException(second)))
        assertEquals(2, policy.missingCount)
    }

    @Test fun installedJsonAndUnexpectedIoFailuresAreNeverSuppressed() {
        val policy = NarrationCurriculumFailurePolicy()
        val path = "content/es/programming/course_01/units.json"
        policy.classify(path, FileNotFoundException(path))
        repeat(3) {
            assertEquals(NarrationCurriculumFailurePolicy.Decision.UNEXPECTED,
                policy.classify(path, JSONException("Unexpected malformed JSON")))
            assertEquals(NarrationCurriculumFailurePolicy.Decision.UNEXPECTED,
                policy.classify(path, IOException("Cannot read installed resource")))
        }
        assertEquals(1, policy.missingCount)
    }
}

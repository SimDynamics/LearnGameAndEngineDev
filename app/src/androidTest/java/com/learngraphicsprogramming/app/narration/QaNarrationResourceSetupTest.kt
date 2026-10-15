package com.learngraphicsprogramming.app.narration

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Test fixture setup only: copies already installed, verified models into the isolated QA APK. */
@RunWith(AndroidJUnit4::class)
class QaNarrationResourceSetupTest {
    @Test fun copyVerifiedPrivateTestFixturesToTheQaOwnedModelDirectory() {
        val app = InstrumentationRegistry.getInstrumentation().targetContext
        assertEquals("Never write models in the ordinary app", "com.learngraphicsprogramming.app.realdownloadsqa", app.packageName)
        val source = File(app.filesDir, "qa-narration-resource-stage").canonicalFile
        assertEquals(app.filesDir.canonicalFile, source.parentFile)
        assertTrue("Only explicitly staged test resources are accepted", source.isDirectory)
        val destination = File(requireNotNull(app.getExternalFilesDir(null)), "kokoro/${KokoroModelManifest.revision}")
        assertTrue(destination.isDirectory || destination.mkdirs())
        var count = 0
        for (artifact in KokoroModelManifest.artifacts.distinctBy { it.fileName }) {
            val staged = File(source, artifact.fileName)
            if (!staged.isFile) continue
            assertTrue("The original pinned model must be intact: ${artifact.fileName}", KokoroModelManifest.verify(staged, artifact))
            val installed = File(destination, artifact.fileName)
            if (!KokoroModelManifest.verify(installed, artifact)) staged.copyTo(installed, overwrite = true)
            assertTrue(KokoroModelManifest.verify(installed, artifact))
            count++
        }
        assertTrue("Real model and voice fixtures must be copied", count >= 7)
        assertTrue(source.deleteRecursively())
    }
}

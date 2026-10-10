pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "Learn Graphics Programming"
include(":app")

// The Play-delivered Clang 21 feature is still being integrated. Keep it
// outside normal builds until its App Bundle and native payload are verified.
val enableClang21PlayFeature =
    providers.gradleProperty("lgp.playClang21Feature").orNull.equals("true", ignoreCase = true)
if (enableClang21PlayFeature) {
    include(":clang21")
}

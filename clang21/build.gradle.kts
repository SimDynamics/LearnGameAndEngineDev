plugins {
    alias(libs.plugins.android.dynamic.feature)
}

android {
    namespace = "com.learngraphicsprogramming.app.clang21"
    compileSdk {
        version = release(37)
    }
    defaultConfig {
        minSdk = 24
    }
}

dependencies {
    // A dynamic feature depends on the base app, never the other way around.
    implementation(project(":app"))
}

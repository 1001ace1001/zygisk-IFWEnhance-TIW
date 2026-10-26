// Vendored from https://github.com/Kr328/MagicLibrary (library module),
// substituted for the unpublished com.github.kr328.magic:library:1.9 AAR.
plugins {
    id("com.android.library")
}

android {
    compileSdk = 34

    namespace = "com.github.kr328.magic"

    defaultConfig {
        minSdk = 26

        consumerProguardFiles("consumer-rules.pro")
    }
}

dependencies {
    compileOnly(project(":magic-hideapi"))

    compileOnly(libs.androidx.annotation)
}

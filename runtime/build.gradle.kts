// Vendored from https://github.com/Kr328/ZygoteLoader (fix branch)
// "Never abort() on recoverable failures (fix companion + fork crashes)"
//
// The published com.github.kr328.zygote-loader:runtime:3.1 artifact aborts
// (SIGABRT) whenever a companion socket operation, open() or mmap() fails.
// This kills the module companion daemon (zygiskd64) and, as a cascade,
// makes freshly forked app processes die inside nativeForkAndSpecialize.
// Vendoring the patched runtime lets this module be built standalone.
//
// This fork is ZYGISK-ONLY: the Riru flavor was removed.
plugins {
    id("com.android.library")
}

android {
    compileSdk = 34

    ndkVersion = "26.1.10909125"

    namespace = "com.github.kr328.zloader"

    defaultConfig {
        minSdk = 26

        consumerProguardFiles("consumer-rules.pro")

        externalNativeBuild {
            cmake {
                abiFilters("arm64-v8a", "armeabi-v7a", "x86", "x86_64")
                arguments("-DANDROID_STL=none")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1+"
        }
    }

    buildFeatures {
        buildConfig = true
        prefab = true
    }
}

dependencies {
    compileOnly(libs.androidx.annotation)
}

rootProject.name = "IFW Enhance"

// Self-contained build: the com.github.kr328.gradle.zygote plugin is
// vendored as a composite build because maven.kr328.app is unreachable.
includeBuild("gradle-plugin")

include(":module", ":hideapi", ":runtime", ":magic", ":magic-hideapi")

pluginManagement {
    repositories {
        mavenCentral()
        google()
        mavenLocal()
    }
}

dependencyResolutionManagement {
    repositories {
        mavenCentral()
        google()
        mavenLocal()
    }
    versionCatalogs {
        create("libs") {
            val magic = "1.9"
            val annotation = "1.7.0"

            library("magic-library", "com.github.kr328.magic", "library").version(magic)
            library("androidx-annotation", "androidx.annotation:annotation:$annotation")
        }
    }
}

rootProject.name = "zygote-gradle-plugin"

pluginManagement {
    repositories {
        mavenCentral()
        google()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositories {
        mavenCentral()
        google()
    }
    versionCatalogs {
        create("libs") {
            val agp = "8.1.2"

            library("android-gradle", "com.android.tools.build:gradle:$agp")
        }
    }
}

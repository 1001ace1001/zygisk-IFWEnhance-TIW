import com.github.kr328.gradle.zygote.ZygoteLoader

plugins {
    id("com.android.application")
    id("com.github.kr328.gradle.zygote")
}

android {
    namespace = "com.github.kr328.module"
}

dependencies {
    compileOnly(project(":hideapi"))

    implementation(libs.magic.library)
}

zygote {
    val moduleId = "ifw-enhance-tiw"
    val moduleName = "IFW Enhance TIW"
    val moduleDescription = "Allows Intent Firewall to filter results of queryIntent(Activities/Services) APIs. Special edition for TigerInTheWall."
    val moduleAuthor = "Revincx"
    val moduleEntrypoint = "com.github.kr328.ifw.Main"
    val versionName = android.defaultConfig.versionName

    packages(ZygoteLoader.PACKAGE_SYSTEM_SERVER)

    riru {
        id = "riru-$moduleId".replace('-', '_')
        name = "Riru - $moduleName"
        archiveName = "riru-$moduleId-$versionName"
        updateJson = "https://github.com/TigerBeanst/Riru-IFWEnhance-TIW/releases/latest/download/riru-$moduleId.json"
    }

    zygisk {
        id = "zygisk-$moduleId".replace('-', '_')
        name = "Zygisk - $moduleName"
        archiveName = "zygisk-$moduleId-$versionName"
        updateJson = "https://github.com/TigerBeanst/Riru-IFWEnhance-TIW/releases/latest/download/zygisk-$moduleId.json"
    }

    all {
        author = moduleAuthor
        description = moduleDescription
        entrypoint = moduleEntrypoint
        isUseBinderInterceptors = true
    }
}

// The com.github.kr328.gradle.zygote plugin injects an implementation
// dependency on the published com.github.kr328.gradle.zygote-loader:runtime
// AAR. Replace it with the vendored (crash-fixed) :runtime project above.
// MagicLibrary is vendored as :magic for the same reason: maven.kr328.app
// (the only repository publishing both) is unreachable.
configurations.all {
    resolutionStrategy.dependencySubstitution {
        substitute(module("com.github.kr328.gradle.zygote:runtime")).using(project(":runtime"))
        substitute(module("com.github.kr328.magic:library")).using(project(":magic"))
    }
}

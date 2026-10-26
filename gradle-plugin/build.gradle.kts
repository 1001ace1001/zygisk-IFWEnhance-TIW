// Vendored from https://github.com/Kr328/ZygoteLoader (gradle-plugin
// subproject), adjusted so the plugin can be consumed via includeBuild:
//  - group/version pinned explicitly (the upstream root build set them)
//  - RUNTIME_DEPENDENCY no longer reaches across projects
//  - no maven-publish
plugins {
    java
    `java-gradle-plugin`
}

group = "com.github.kr328.gradle.zygote"
version = "3.1"

java {
    withSourcesJar()
    sourceCompatibility = JavaVersion.VERSION_11
    targetCompatibility = JavaVersion.VERSION_11
}

val dynamicSources = layout.buildDirectory.dir("generated/dynamic")

dependencies {
    compileOnly(gradleApi())
    compileOnly(libs.android.gradle)
}

sourceSets {
    named("main") {
        java.srcDir(dynamicSources)
    }
}

gradlePlugin {
    plugins {
        create("zygote") {
            id = "com.github.kr328.gradle.zygote"
            implementationClass = "com.github.kr328.gradle.zygote.ZygoteLoaderPlugin"
        }
    }
}

task("generateDynamicSources") {
    inputs.property("moduleGroup", project.group)
    inputs.property("moduleVersion", project.version)
    outputs.dir(dynamicSources)
    tasks.withType(JavaCompile::class.java).forEach { it.dependsOn(this) }
    tasks["sourcesJar"].dependsOn(this)

    doFirst {
        val buildConfig = dynamicSources.get().asFile.resolve("com/github/kr328/gradle/zygote/BuildConfig.java")

        buildConfig.parentFile.mkdirs()

        buildConfig.writeText(
            """
            package com.github.kr328.gradle.zygote;

            public final class BuildConfig {
                public static final String RUNTIME_DEPENDENCY = "${project.group}:runtime:${project.version}";
            }
            """.trimIndent()
        )
    }
}

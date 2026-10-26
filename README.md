# Zygisk - IFWEnhance TIW

A module of [Zygisk](https://github.com/topjohnwu/zygisk-module-sample). Allows Intent Firewall to filter results of queryIntent(Activities/Services) APIs.

This is a special edition, ONLY for [TigerInTheWall](https://github.com/TigerBeanst/TigerInTheWall).

> This fork is **Zygisk-only** (Riru support was removed) and ships a
> **crash fix** for the bundled ZygoteLoader runtime.

## Requirements

* Zygisk >= 24000 (KernelSU with Zygisk Next, or Magisk)
* Android 8.0+

## Feature

Allows Intent Firewall to filter results of follow APIs.

- `IPackageManager.queryIntentActivities`
- `IPackageManager.queryIntentActivityOptions`
- `IPackageManager.queryIntentServices`


## Crash fix in this fork (v23TIW)

The upstream `com.github.kr328.zygote-loader:runtime:3.1` native library
`abort()`s on **any** recoverable failure. Two crash signatures observed in
the field (KernelSU bugreport tombstones):

1. `zygisk_companion_entry` (SIGABRT): a single broken socket connection or
   a failed `open()` inside the companion daemon `zygiskd64` aborted the
   whole companion process.
2. `SIGABRT inside nativeForkAndSpecialize` (child still named `zygote64`):
   after the companion died, the next app fork hit the `connectCompanion()`
   failure path and `abort()`ed, killing the app being forked (apps failed
   to start, e.g. `os.kei.keimi:resolver`).

The vendored runtime in `runtime/` fixes this:

- companion: broken connections / failed `open()` / failed `mmap()` are
  logged (`LOGW`/`LOGE`) and dropped instead of aborting
- zygote side: any IPC failure degrades to "module not injected into this
  process"; the forked process always survives
- `binder.c`: fixed reply parcel native pointer (read the `data` parcel
  twice), cleared pending JNI exceptions on lookup failures
- `LOGF` now logs before aborting, so any future tombstone has a message

The runtime is vendored from https://github.com/Kr328/ZygoteLoader with the
fix branch applied (see `upstream-patches/` for a ready-to-submit PR patch).

The build is also **fully self-contained**: maven.kr328.app (the only
repository publishing the `com.github.kr328.gradle.zygote` gradle plugin
and MagicLibrary) is unreachable, so both are vendored:

- `gradle-plugin/` — the zygote module gradle plugin (composite build)
- `magic/`, `magic-hideapi/` — MagicLibrary and its compile-time stubs

## Build

1. Install JDK, Android SDK (platform 34), NDK 26.1.10909125, CMake 3.22.1

2. Configure sdk path in local.properties 

   ```properties
   sdk.dir=/path/to/android/sdk
   ```

3. Build

    ``` bash 
    ./gradlew module:assembleZygisk
    ```
    
4. Pick `zygisk-ifw-enhance-tiw-<version>.zip` from `module/build/outputs/magisk/zygisk/release`

GitHub Actions also builds the module on every push (`.github/workflows/build-module.yaml`).


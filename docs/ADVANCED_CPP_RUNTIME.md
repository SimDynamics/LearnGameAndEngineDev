# Advanced C++ / GPU architecture

Branch: `fix/cpp-stdin-support`

## Target architecture and actual status

| Layer | Target | Current state |
| --- | --- | --- |
| Windows compiler | MSVC 14.50, C++20 (Unreal reference) | Windows-only smoke test: `tools/test-msvc-windows.ps1` |
| On-device WASI compiler | Clang/LLVM 23, C++23 | Existing FoldCode ARM64 compiler is Clang 21; C++23 language flag is enabled, but the Clang 23 binary is NOT yet shipped |
| WASI sysroot | WASI SDK 34, exception-enabled | Official archive download, SHA-256 verification and EH library inspection via `:app:prepareWasiSdk34Reference`; **not yet substituted into the APK** |
| Native Android compiler | Android NDK, ARM64 | Existing NDK/CMake build, Vulkan/OpenGL ES renderers and physics benchmark |
| WebAssembly execution | Wasmtime 49.0.2, Android ARM64 | JNI C API and verified release package, activatable with `-Plgp.wasmtime=true`; device test outstanding |
| Regression reference | WAMR 2.4.5 | Retained as default until Wasmtime tests pass |
| Graphics backends | Vulkan + OpenGL ES | Both tested successfully on Honor X8a; 1,024 instanced spheres reported at approximately 50 FPS |

## Why the SDK cannot simply replace our existing ZIP

The packaged Android Clang binary comes from FoldCode (Clang 21).
WASI SDK 34 is from the LLVM 23 generation, and its exception-capable
C++ standard library requires an LLVM >= 22 WebAssembly compiler.

The current `app/src/main/assets/runtime/wasi-runtime.zip` is a
previously generated WASI-based, no-exceptions distribution. Although
its Android extraction directory includes `wasi-sdk-34`, that name
does not guarantee that it matches the full official WASI SDK 34.

Do not activate `-fwasm-exceptions`, `-lunwind` or the official
exception sysroot until a compatible **Clang 23 host binary built for
Android ARM64** is packaged, along with matching compiler-rt and wasm-ld.

The official WASI SDK 34 Linux tarball is a SOURCE OF TARGET LIBRARIES.
Its x86_64 Linux executable Clang is NOT executable on Android.

## Test commands

Confirm a clean working tree, then run:

```powershell
git switch fix/cpp-stdin-support
git status --short
git pull --ff-only origin fix/cpp-stdin-support

# Existing graphics / WAMR baseline:
.\gradlew :app:installDebug --no-configuration-cache

# Separate official WASI SDK 34 sysroot qualification (183 MiB):
.\gradlew :app:prepareWasiSdk34Reference --no-configuration-cache

# Android Wasmtime C API build and install (official 49.0.2 / ARM64):
.\gradlew :app:installDebug "-Plgp.wasmtime=true" --no-configuration-cache

# Compare the C++ compiler/runtime instrumentation tests:
.\gradlew :app:connectedDebugAndroidTest "-Plgp.wasmtime=true" "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.CppExecutionEngineInstrumentedTest" --no-configuration-cache
```

The app's MSVC cross-check is instead run on Windows with Visual Studio
Developer PowerShell:

```powershell
.\tools\test-msvc-windows.ps1 -StrictVersion
```

## Acceptance gates

1. No regression in Vulkan/OpenGL ES or the graphics benchmark.
2. Wasmtime shared library is present in the installed APK.
3. C++23 smoke test, stdout and stdin pass against Wasmtime.
4. Clang 23 for Android ARM64 is built, signed/reproducible and verified by `--version`.
5. Official SDK 34 libraries are packaged without the Linux-host executables.
6. Compile and run `throw` / `try` / `catch` with standardized WebAssembly EH flags.
7. Test runtime limits, cancellation/termination and WASI filesystem isolation.
8. Only after these gates, make Wasmtime/EH the default and retire WAMR.

## Pinned official release hashes

See `toolchains/versions.properties`.

Sources:
- https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-34
- https://github.com/WebAssembly/wasi-sdk/blob/main/CppExceptions.md
- https://github.com/bytecodealliance/wasmtime/releases/tag/v49.0.2
- https://docs.wasmtime.dev/c-api/

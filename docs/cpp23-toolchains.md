# Compilador C++23 para Android: configuración predeterminada y alternativa

Este proyecto compila C++23 localmente en Android ARM64 y ejecuta los binarios WebAssembly mediante WASI.

## Configuración predeterminada

Al ejecutar Gradle **sin propiedades adicionales**, el APK selecciona:

- **Clang 23.1.3 y LLD 23** como herramientas ARM64 de compilación y enlace.
- **Wasmtime 49.0.2** como motor WebAssembly.
- **WASI SDK 34 `wasm32-wasip1/eh`** para excepciones C++ (incluyendo `try`, `catch`, `throw` y `std::runtime_error`).
- Excepciones WebAssembly modernas mediante `-fwasm-exceptions`, `-mllvm -wasm-use-legacy-eh=false` y `-lunwind`.

```powershell
.\gradlew :app:assembleDebug --console=plain
```

**Requisito de compilación:** los ejecutables nativos Android ARM64 `liblgp_clang23.so` y `liblgp_lld23.so` deben estar disponibles en `.gradle/clang23-android-arm64/`. No se almacenan en Git. Gradle comprueba sus cabeceras ELF antes de empaquetarlos. Los SDK oficiales WASI/Wasmtime se obtienen y verifican mediante las tareas existentes. El primer build en un entorno nuevo requiere preparar esas dependencias.

## Alternativa Clang 21 + WAMR

Una sola propiedad selecciona automáticamente la configuración anterior:

```powershell
.\gradlew :app:assembleDebug "-Plgp.clang23=false" --console=plain
```

El APK resultante incluye **FoldCode Clang 21**, **WAMR 2.4.5** y el sysroot **sin excepciones**. Las dependencias FoldCode se obtienen desde sus archivos oficiales con verificación SHA-256. No incluye el Clang 23 ni el overlay WASI EH.

Esta selección es **de compilación**, no un botón para cambiar de motor dentro de la app. Un módulo Clang 21 distribuible con Play Feature Delivery es una tarea posterior.

## Propiedades avanzadas

| Propiedad | Valor por omisión | Significado |
| --- | --- | --- |
| `lgp.clang23` | `true` | Usa Clang 23; `false` usa FoldCode Clang 21 |
| `lgp.wasmtime` | Igual que `lgp.clang23` | `true` usa Wasmtime; `false` usa WAMR |
| `lgp.cppExceptions` | `lgp.clang23 && lgp.wasmtime` | Activa WASI EH solo con Clang 23 y Wasmtime |

Cada propiedad acepta `true` o `false`. Combinar `lgp.cppExceptions=true` con Clang 21 o WAMR genera un error deliberado. Por ejemplo, `-Plgp.wasmtime=false` mantiene Clang 23, pero usa WAMR y desactiva las excepciones.

## Validaciones en el teléfono Android

Verificar el contenido del APK **predeterminado**:

```powershell
.\gradlew :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.CppToolchainSelectionInstrumentedTest" --console=plain
```

Comprobar las **seis pruebas de excepciones C++23**:

```powershell
.\gradlew :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.Cpp23ExceptionsInstrumentedTest" --console=plain
```

Comprobar las **nueve pruebas de Clang 23**, incluidas entradas por `std::cin`, biblioteca estándar y enlace:

```powershell
.\gradlew :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.Clang23CompilationInstrumentedTest" --console=plain
```

Comprobar el **APK alternativo Clang 21**:

```powershell
.\gradlew :app:connectedDebugAndroidTest "-Plgp.clang23=false" "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.CppToolchainSelectionInstrumentedTest" --console=plain
.\gradlew :app:connectedDebugAndroidTest "-Plgp.clang23=false" "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.CppExecutionEngineInstrumentedTest" --console=plain
```

El catálogo `Cpp23LibraryCompatibilityInstrumentedTest` evalúa doce características de la biblioteca estándar. En el último examen previo a cambiar el modo predeterminado, once aparecieron como `SUPPORTED`, `std::ispanstream` como `UNAVAILABLE` y ninguna como `BROKEN`. No equivale a afirmar compatibilidad absoluta con toda la biblioteca C++23.

**Precaución:** las pruebas ejecutadas mediante `connectedDebugAndroidTest` pueden desinstalar la aplicación al finalizar y eliminar informes almacenados en sus archivos privados. Para conservar informes, puede instalarse el APK de prueba y ejecutarse la instrumentación directamente con ADB.

Los resultados de las pruebas con la nueva configuración predeterminada deben verificarse **en el dispositivo** antes de considerar terminada la migración.

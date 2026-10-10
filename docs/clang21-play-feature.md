# Clang 21 como módulo descargable (Play Feature Delivery)

## Estado

El módulo Android `:clang21` usa `com.android.dynamic-feature` y declara
`<dist:on-demand />`. Se activa únicamente con `-Plgp.playClang21Feature=true`.

**Las 12 bibliotecas nativas ARM64 de FoldCode Clang 21 se preparan para
el split opcional, no para el APK base Clang 23.** En la máquina de desarrollo,
Gradle reutiliza o descarga los paquetes oficiales FoldCode, verifica SHA-256,
extrae las doce bibliotecas y comprueba sus cabeceras ELF64 AArch64.

**Validado en el Honor X8a durante el desarrollo:** el App Bundle contiene
exactamente las doce bibliotecas bajo `clang21/lib/arm64-v8a/`, y la búsqueda
de `libfoldclang.so`, `libfoldlldcore.so` y `libLLVM.so` en `base` no
encontró resultados.

El APK base dispone ahora de `Clang21FeatureDelivery`, un gestor que consulta
el estado del split y puede solicitar instalación, informar del progreso,
cancelar y gestionar confirmación de Google Play. No inicia descargas por sí
mismo. La clase preexistente `LearnGraphicsApplication` ahora inicializa
`SplitCompat` cuando el módulo dinámico está habilitado, sin perder la
inicialización del idioma.

La pantalla `CppPrototypeActivity` muestra el panel de descarga y, en
compilaciones con el módulo opcional activado, un selector **Clang 23 /
Clang 21**. Clang 23 sigue marcado por defecto. La elección se aplica a una
ejecución del compilador y se conserva al rotar la pantalla. Clang 21 solo
puede seleccionarse si el módulo está instalado y Android expone sus
bibliotecas en el directorio nativo con permiso de ejecución. En caso
contrario, el selector permanece deshabilitado y muestra una explicación.

**Validaciones completadas en Honor X8a, Android 14, 9 de octubre de 2026:**

1. `bundletool --local-testing` instaló el módulo opcional mediante
   `SplitCompat` y extrajo sus 12 bibliotecas bajo `files/splitcompat`,
   pero `libfoldclang.so` tenía permisos `-r--------`, no ejecutables.
2. `adb install-multiple -r` de los seis APK relevantes registró
   `split_clang21.apk` y `split_clang21.config.arm64_v8a.apk` en Android.
   La biblioteca `libfoldclang.so` instalada en `/data/app/.../lib/arm64`
   tenía permisos `-rwxr-xr-x`.
3. Configurando `LD_LIBRARY_PATH` se ejecutó Clang 21.1.8. Con
   `LGP_LLD_CORE` y `liblgp_wasm_ld.so`, LLD 21.1.8 también funcionó.
4. Clang 21 + LLD 21 compilaron y enlazaron un programa C++23 con
   `<iostream>` contra WASI SDK 34, generando `iostream.wasm` de 242 KiB
   aproximadamente.
5. Una prueba instrumentada ejecutó ese WebAssembly mediante Wasmtime 49
   y verificó exactamente `Hola desde Clang 21\n`: **OK (1 test)**.

**Todavía pendiente:** probar la selección integrada y una compilación
completa iniciada desde la interfaz, así como validar una descarga bajo
demanda a través de Google Play real. La instalación manual de los seis APK
no demuestra que Play coloque las bibliotecas en un directorio ejecutable.

Las compilaciones normales siguen usando Clang 23 + Wasmtime + WASI EH.
El APK alternativo completo se conserva con `-Plgp.clang23=false`.

## Compilar el App Bundle experimental

```powershell
.\gradlew :app:bundleDebug "-Plgp.playClang21Feature=true" --console=plain
```

Resultado esperado: `BUILD SUCCESSFUL` y
`app/build/outputs/bundle/debug/app-debug.aab`.

## Verificar la separación del compilador

El App Bundle es un archivo ZIP. Inspeccionar su índice en PowerShell:

```powershell
$bundleEntries = jar tf ".\app\build\outputs\bundle\debug\app-debug.aab"
$clang21Libraries = @($bundleEntries | Select-String '^clang21/lib/arm64-v8a/.*[.]so$')
$clang21Libraries
"Bibliotecas Clang 21 en el split: $($clang21Libraries.Count)"
$bundleEntries | Select-String '^base/lib/arm64-v8a/(libfoldclang|libfoldlldcore|libLLVM)[.]so$'
```

Resultado esperado: **12** rutas bajo `clang21/lib/arm64-v8a/` y **ninguna**
ruta en la última búsqueda del módulo `base`. Las bibliotecas son:

- `libc++_shared.so` y `libfoldclang.so` del paquete FoldCode base.
- `libLLVM.so`, `libclang-cpp.so`, `libffi.so`, `libfoldlldcore.so`,
  `libiconv.so`, `libicudataxxx.so`, `libicuucxxx.so`,
  `libxml2xxx.so`, `libzstdxx.so` y `libzzz.so` del paquete C++.

La biblioteca `libc++_shared.so` puede existir también en el módulo base
como dependencia de otras bibliotecas Android; la última búsqueda no la
utiliza para declarar independencia. Antes de distribuir, comprobar
compatibilidad si hay versiones distintas del mismo SONAME entre splits.

## Gestor de instalación y prueba segura

La dependencia usada es
`com.google.android.play:feature-delivery:2.1.0` (biblioteca oficial).
El gestor Kotlin vive en
`app/src/main/java/com/learngraphicsprogramming/app/runtime/Clang21FeatureDelivery.kt`.
Solo permite solicitar `clang21` si se compiló con
`-Plgp.playClang21Feature=true`; no altera `CppCompiler`.

Para comprobar que la nueva infraestructura Android compila y que no intenta
descargar el módulo automáticamente, ejecutar:

```powershell
.\gradlew :app:connectedDebugAndroidTest "-Plgp.playClang21Feature=true" "-Pandroid.testInstrumentationRunnerArguments.class=com.learngraphicsprogramming.app.Clang21FeatureDeliveryInstrumentedTest" --console=plain
```

Resultado esperado: **3/3 pruebas**, incluyendo la clase
`LearnGraphicsApplication` declarada en el manifiesto y la visibilidad
condicional del panel de descarga. Estas pruebas **no** pulsan el botón ni
llaman a `startInstall`; no ejercitan descargas reales ni confirmaciones
de Google Play.

Para volver al build convencional sin módulo dinámico, omitir
`-Plgp.playClang21Feature=true`. El gestor informará `NOT_IN_BUILD` en
vez de solicitar un módulo que no existe en ese APK.

## Selección del compilador y próximas pruebas

- `CppCompilerVariant.DEFAULT` mantiene Clang 23 y las excepciones WASM
  modernas. El antiguo APK `-Plgp.clang23=false` sigue usando Clang 21 + WAMR.
- `CppCompilerVariant.OPTIONAL_CLANG_21` emplea el compilador FoldCode,
  los encabezados/bibliotecas WASI **sin excepciones** y el wrapper LLD 21.
  Conserva Wasmtime como motor de ejecución.
- Cada compilación crea su propio enlace simbólico `bin/wasm-ld` en su carpeta
  de trabajo, evitando que ejecuciones alternas modifiquen un symlink global.
- La variante opcional requiere `-Plgp.playClang21Feature=true`, una instalación
  registrada del módulo y bibliotecas nativas ejecutables. Una descarga
  `--local-testing` por SplitCompat puede quedar instalada pero deshabilitada
  para ejecución de procesos; no se intenta cambiar permisos en `files/`.
- Probar el selector con `Clang21FeatureDeliveryInstrumentedTest`, y el
  recorrido Clang 21 → LLD 21 → Wasmtime con
  `Clang21WasmRuntimeInstrumentedTest.compilesAndExecutesThroughSelectedOptionalClang21`.
- Validar entrega real por Google Play, alternar ambos compiladores desde el
  editor y comprobar que las bibliotecas compartidas del módulo no generan
  conflictos con las del APK base.

Para pruebas **sin perder los splits opcionales**, construir un nuevo AAB,
extraer sus seis APK para el Honor X8a con bundletool y usar
`adb install-multiple -r` para reemplazarlos juntos. La instalación
individual de `app-debug.apk` puede retirar los splits. Una ejecución
`connectedDebugAndroidTest` también puede reinstalar la app bajo prueba,
por lo que se prefiere instalar el APK de pruebas por separado y usar
`adb shell am instrument`.

El módulo opcional **no incluye WAMR**: la aplicación base conserva Wasmtime.
El cambio `-Plgp.clang23=false` sigue construyendo el APK anterior completo;
no equivale a descargar el módulo sobre el APK base.

Durante la compilación Gradle obtiene archivos de FoldCode con hashes
fijados, pero la app nunca descarga ejecutables de GitHub.

## Documentación oficial

- https://developer.android.com/guide/playcore/feature-delivery
- https://developer.android.com/guide/playcore/feature-delivery/on-demand

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
mismo. También se añadió `LearnGraphicsApplication` para inicializar
`SplitCompat` cuando el módulo dinámico está habilitado.

La pantalla `CppPrototypeActivity` tiene ahora un panel visible **solo
cuando se compila con el módulo experimental habilitado**. Antes de descargar
muestra una confirmación; después informa del progreso y permite cancelar.
Clang 23 sigue siendo el compilador activo aunque Clang 21 se instale.

**Pendiente:** validar la interfaz en un dispositivo, instalar el módulo real
con Google Play/bundletool y resolver las bibliotecas nativas del split
antes de permitir seleccionar Clang 21. Las instalaciones bajo demanda no
funcionan necesariamente en un APK instalado directamente mediante ADB,
porque no equivale a una instalación desde Play.

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

## Arquitectura pendiente de completar

1. Validar la pantalla de descarga y sus textos localizados en el teléfono.
2. Probar un ciclo de instalación, progreso, errores y cancelación con
   `bundletool --local-testing` y posteriormente con Google Play.
3. Resolver las rutas nativas reales después de instalar el split;
   no asumir que viven en `applicationInfo.nativeLibraryDir` del APK base.
4. Verificar carga de dependencias y enlace con el `wasm-ld` del módulo base.
5. Añadir selección explícita de Clang 21 después de validar su ejecución.

El módulo opcional **no incluye WAMR**: la aplicación base conserva Wasmtime.
El cambio `-Plgp.clang23=false` sigue construyendo el APK anterior completo;
no equivale a descargar el módulo sobre el APK base.

Durante la compilación Gradle obtiene archivos de FoldCode con hashes
fijados, pero la app nunca descarga ejecutables de GitHub.

## Documentación oficial

- https://developer.android.com/guide/playcore/feature-delivery
- https://developer.android.com/guide/playcore/feature-delivery/on-demand

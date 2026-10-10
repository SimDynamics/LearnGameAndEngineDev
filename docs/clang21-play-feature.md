# Clang 21 como módulo descargable (Play Feature Delivery)

## Estado

El módulo Android `:clang21` usa `com.android.dynamic-feature` y declara
`<dist:on-demand />`. Se activa únicamente con `-Plgp.playClang21Feature=true`.

**Las 12 bibliotecas nativas ARM64 de FoldCode Clang 21 se preparan para
el split opcional, no para el APK base Clang 23.** En la máquina de desarrollo,
Gradle reutiliza o descarga los paquetes oficiales FoldCode, verifica SHA-256,
extrae las doce bibliotecas y comprueba sus cabeceras ELF64 AArch64.

**Pendiente:** confirmar que el App Bundle contiene físicamente las doce
bibliotecas en el split. Todavía no hay descarga desde la interfaz Android
ni selección del compilador; eso requiere Play Feature Delivery y una ruta
segura para cargar los ejecutables tras instalar el módulo.

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

## Arquitectura pendiente de completar

1. Añadir instalación bajo demanda mediante la biblioteca Play Feature
   Delivery, con progreso, errores, cancelación y confirmación.
2. Resolver las rutas nativas reales después de instalar el split;
   no asumir que viven en `applicationInfo.nativeLibraryDir` del APK base.
3. Verificar carga de dependencias y enlace con el `wasm-ld` del módulo base.
4. Validar con `bundletool --local-testing` y posteriormente con Google Play.

El módulo opcional **no incluye WAMR**: la aplicación base conserva Wasmtime.
El cambio `-Plgp.clang23=false` sigue construyendo el APK anterior completo;
no equivale a descargar el módulo sobre el APK base.

Durante la compilación Gradle obtiene archivos de FoldCode con hashes
fijados, pero la app nunca descarga ejecutables de GitHub.

## Documentación oficial

- https://developer.android.com/guide/playcore/feature-delivery
- https://developer.android.com/guide/playcore/feature-delivery/on-demand

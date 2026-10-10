# Clang 21 como módulo descargable (Play Feature Delivery)

## Estado de esta etapa

El proyecto dispone del módulo experimental Android `:clang21` con el plugin
`com.android.dynamic-feature` y entrega `<dist:on-demand />`. **Ahora
empaqueta las 12 bibliotecas ARM64 de FoldCode Clang 21 exclusivamente en este
split opcional**, obtenidas durante la compilación de archivos oficiales con
SHA-256 verificado. La aplicación **todavía no instala el módulo desde Google
Play ni ofrece selección del compilador en la interfaz**.

La compilación Android normal permanece intacta y sigue usando Clang 23,
LLD 23, Wasmtime y WASI SDK 34 EH. Para compilar la alternativa clásica,
todavía puede utilizarse `-Plgp.clang23=false`.

El módulo se activa **solamente al compilar** con:
`-Plgp.playClang21Feature=true`.

## Verificar la inclusión del módulo

Primero confirmar que Gradle detecta ambos proyectos:

```powershell
.\gradlew projects "-Plgp.playClang21Feature=true" --console=plain
```

La salida debería incluir `:app` y `:clang21`.

Después construir el Android App Bundle de depuración:

```powershell
.\gradlew :app:bundleDebug "-Plgp.playClang21Feature=true" --console=plain
```

En la carpeta `app/build/outputs/bundle/debug/` debería generarse
`app-debug.aab`. Un App Bundle es un **archivo ZIP**, pero su existencia por
sí sola no valida que Google Play entregue e instale correctamente el módulo.

Para examinar su índice con el ejecutable `jar` del JDK:

```powershell
jar tf .\app\build\outputs\bundle\debug\app-debug.aab |
    Select-String "clang21/"
```

Se esperan entradas para los dos manifiestos y **doce bibliotecas FoldCode**
dentro de `clang21/lib/arm64-v8a/`. Para comprobarlo en PowerShell:

```powershell
$bundleEntries = jar tf ".\\app\\build\\outputs\\bundle\\debug\\app-debug.aab"
$clang21Libraries = @($bundleEntries | Select-String '^clang21/lib/arm64-v8a/.*[.]so

## Arquitectura prevista

1. Mantener la aplicación base con Clang 23 y Wasmtime, ya validados.
2. **Implementado, pendiente de verificar en AAB:** las 12 bibliotecas nativas
   FoldCode Clang 21 ahora pertenecen al módulo opcional. No se incluye
   WAMR, ya que Clang 21 también se ha probado con Wasmtime.
3. Preparar el instalador `SplitInstallManager` y `SplitCompat` con el
   módulo de nombre `clang21`. Consultar disponibilidad antes de ofrecer
   su ejecución e informar al usuario del tamaño, progreso y errores.
4. Resolver la ruta de las bibliotecas nativas desde el *split* instalado,
   sin asumir que se encuentran en `applicationInfo.nativeLibraryDir` del
   APK base. Validar carga de dependencias JNI y resolución de símbolos.
5. Comprobar instalación, uso, cancelación y reinicio mediante
   `bundletool --local-testing` y pruebas reales de Google Play antes de
   publicarlo.

**Importante:** `-Plgp.clang23=false` construye el *APK alternativo completo*
para regresiones y no equivale a instalar el módulo descargable sobre la
aplicación predeterminada. Tampoco debe mezclarse en la misma compilación
con `-Plgp.playClang21Feature=true`.

La descarga del módulo dentro de Android debe usar Google Play Feature
Delivery, no descargas arbitrarias de ejecutables desde GitHub. Las descargas
oficiales FoldCode realizadas por **Gradle en la máquina de desarrollo** son
otra operación distinta. Aún falta verificar instalación y carga de las
bibliotecas mediante los APK divididos entregados por Play/bundletool.

## Fuentes oficiales

- https://developer.android.com/guide/playcore/feature-delivery
- https://developer.android.com/guide/playcore/feature-delivery/on-demand
)
$clang21Libraries
"Bibliotecas Clang 21 en el split: $($clang21Libraries.Count)"
$bundleEntries | Select-String '^base/lib/arm64-v8a/(libfoldclang|libfoldlldcore|libLLVM)[.]so

## Arquitectura prevista

1. Mantener la aplicación base con Clang 23 y Wasmtime, ya validados.
2. Mover únicamente las bibliotecas necesarias de FoldCode Clang 21 al
   módulo opcional. **No es necesario duplicar WAMR**, porque Clang 21 ya
   se probó con Wasmtime.
3. Preparar el instalador `SplitInstallManager` y `SplitCompat` con el
   módulo de nombre `clang21`. Consultar disponibilidad antes de ofrecer
   su ejecución e informar al usuario del tamaño, progreso y errores.
4. Resolver la ruta de las bibliotecas nativas desde el *split* instalado,
   sin asumir que se encuentran en `applicationInfo.nativeLibraryDir` del
   APK base. Validar carga de dependencias JNI y resolución de símbolos.
5. Comprobar instalación, uso, cancelación y reinicio mediante
   `bundletool --local-testing` y pruebas reales de Google Play antes de
   publicarlo.

**Importante:** `-Plgp.clang23=false` construye el *APK alternativo completo*
para regresiones y no equivale a instalar el módulo descargable sobre la
aplicación predeterminada. Tampoco debe mezclarse en la misma compilación
con `-Plgp.playClang21Feature=true`.

La implementación de descarga debe usar Google Play Feature Delivery,
no descargas arbitrarias de ejecutables desde GitHub dentro de la app.

## Fuentes oficiales

- https://developer.android.com/guide/playcore/feature-delivery
- https://developer.android.com/guide/playcore/feature-delivery/on-demand

```

Se espera `12` y ninguna entrada en la última búsqueda. Esta verificación
no presupone ausencia de `libc++_shared.so` en el módulo principal porque otras
dependencias Android podrían requerir una biblioteca con ese nombre.

La preparación `:clang21:prepareClang21FeatureLibraries` utiliza los mismos
dos archivos FoldCode fijados mediante SHA-256 que la alternativa Clang 21
probada anteriormente, extrae solo las bibliotecas ARM64 seleccionadas y
comprueba sus encabezados ELF64 AArch64. Es una **operación de compilación**;
no realiza descargas desde la aplicación Android.

## Arquitectura prevista

1. Mantener la aplicación base con Clang 23 y Wasmtime, ya validados.
2. Mover únicamente las bibliotecas necesarias de FoldCode Clang 21 al
   módulo opcional. **No es necesario duplicar WAMR**, porque Clang 21 ya
   se probó con Wasmtime.
3. Preparar el instalador `SplitInstallManager` y `SplitCompat` con el
   módulo de nombre `clang21`. Consultar disponibilidad antes de ofrecer
   su ejecución e informar al usuario del tamaño, progreso y errores.
4. Resolver la ruta de las bibliotecas nativas desde el *split* instalado,
   sin asumir que se encuentran en `applicationInfo.nativeLibraryDir` del
   APK base. Validar carga de dependencias JNI y resolución de símbolos.
5. Comprobar instalación, uso, cancelación y reinicio mediante
   `bundletool --local-testing` y pruebas reales de Google Play antes de
   publicarlo.

**Importante:** `-Plgp.clang23=false` construye el *APK alternativo completo*
para regresiones y no equivale a instalar el módulo descargable sobre la
aplicación predeterminada. Tampoco debe mezclarse en la misma compilación
con `-Plgp.playClang21Feature=true`.

La implementación de descarga debe usar Google Play Feature Delivery,
no descargas arbitrarias de ejecutables desde GitHub dentro de la app.

## Fuentes oficiales

- https://developer.android.com/guide/playcore/feature-delivery
- https://developer.android.com/guide/playcore/feature-delivery/on-demand

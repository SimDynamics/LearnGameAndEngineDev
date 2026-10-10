# Clang 21 como módulo descargable (Play Feature Delivery)

## Estado de esta etapa

El proyecto ahora dispone de un **esqueleto experimental** del módulo Android
`:clang21`, que utiliza el plugin `com.android.dynamic-feature` y declara
`<dist:on-demand />` en su manifiesto. **Todavía no contiene las bibliotecas
nativas FoldCode, no instala nada desde Google Play y no ofrece selección
dentro de la interfaz.**

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

Se espera una entrada similar a `clang21/manifest/AndroidManifest.xml`.
Compruebe también que los ejecutables FoldCode **no** estén en el directorio
`base/lib/arm64-v8a` ni en `clang21/lib/arm64-v8a` en esta fase; las
bibliotecas se transferirán solo después de validar el módulo vacío.

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

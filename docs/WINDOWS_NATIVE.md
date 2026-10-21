# Base nativa Windows

La raíz contiene el proyecto CMake `LearnGameAndEngineDev`. El objetivo gráfico
`LearnGameAndEngineDev.exe` utiliza C++20, Win32 Unicode y destino x64. El código
importado de Android y sus recursos permanecen en sus ubicaciones originales.

## Compilar y probar

Desde la raíz de **cualquiera de las dos copias**, ejecutar:

```powershell
& .\scripts\windows\Build-Native.ps1 -Preset windows-x64-debug
```

Los otros presets son `windows-x64-relwithdebinfo` y `windows-x64-release`.
El script utiliza el entorno oficial de Visual Studio en un proceso hijo;
no cambia las variables persistentes del equipo ni sus botones de publicación.
Configura, compila y ejecuta CTest. Para omitir únicamente las pruebas se puede
añadir `-SkipTests`. `-Fresh` solicita explícitamente a CMake renovar su caché de
configuración dentro del árbol generado del preset; no borra fuentes ni perfiles.
`-CleanFirst` limpia únicamente los resultados generados de ese preset antes de
recompilar; sirve para comprobar un enlace real, sin modificar el otro checkout.

Herramientas seleccionadas y comprobadas: paquete MSVC v145 `14.51.36231`
(compilador instalado `19.51.36260.0`), host/destino x64, Windows SDK y UCRT
`10.0.26100.0`, CMake `4.3.1-msvc1` y Ninja `1.13.2`. Se utilizan las herramientas
de Visual Studio 2026 instaladas; no se descargan otras. Una selección incompatible
falla con un diagnóstico, en lugar de cambiar silenciosamente de herramientas.

Cada preset genera sus propios archivos en `out/build/<preset>`; el ejecutable
está en `bin/LearnGameAndEngineDev.exe` dentro de ese directorio. La instalación
CMake, si se solicita posteriormente, utiliza `out/install/<preset>`.

Para comprobar ventanas y datos aislados de un ejecutable ya compilado:

```powershell
& .\scripts\windows\Test-Native.ps1 `
  -Executable .\out\build\windows-x64-debug\bin\LearnGameAndEngineDev.exe `
  -Configuration Debug -ExpectedDefaultProfile Codex
```

En la copia Work se utiliza `-ExpectedDefaultProfile Work`. La prueba abre ventanas
reales con perfiles QA, comprueba el binario gráfico AMD64, ejecuta ambos perfiles
a la vez y verifica cierre independiente y conservación de datos al reabrir.
La evidencia se guarda bajo `out/qa/`; no representa datos personales importados.

`Test-CheckoutIsolation.ps1` comprueba además los dos ejecutables procedentes de
checkouts distintos. Recibe `-WorkExecutable`, `-CodexExecutable` y un bloque
`-RebuildWork` que invoque el script Work con `-CleanFirst -SkipTests`. Abre ambos
sin elegir un perfil explícito, cierra Work, reconstruye Work mientras Codex sigue
abierto y verifica sus binarios, cachés y datos QA antes de reabrir Work.

También se puede abrir la raíz con **Archivo → Abrir → Carpeta** de Visual Studio
y elegir uno de los tres presets. Los campos externos de arquitectura/toolset
asisten al IDE; no inicializan por sí solos una consola. Si el IDE proporciona
otro SDK o paquete MSVC, la validación solicita usar el entorno fijado o el script
del repositorio. No existe un segundo proyecto `.vcxproj` mantenido manualmente.

## Copias y datos independientes

Las fuentes y presets son comunes. El perfil predeterminado se decide al configurar
por el sufijo exacto de la raíz `LearnGameAndEngineDev-Work` o
`LearnGameAndEngineDev-Codex`; no depende del directorio de ejecución.
En otras ubicaciones se debe indicar `-Profile Work` o `-Profile Codex` al script.
También existe la selección explícita de lanzamiento `--profile Work|Codex`.

Los datos mutables se resuelven con la API de carpetas de Windows bajo
`%LOCALAPPDATA%\LearnGameAndEngineDev\<perfil>`, separados en `progress`, `drafts`,
`preferences`, `logs`, `projects` y `cache`. Las pruebas añaden
`--qa-instance <identificador>` para utilizar un subperfil `qa/<identificador>`.
`--smoke-test` exige ese aislamiento, abre y pinta una ventana real y la cierra.

No se enlazan fuentes, binarios ni cachés del checkout vecino. No hay un mutex,
puerto o servicio global que impida las ejecuciones simultáneas. El manifiesto
solicita `asInvoker`; los resultados QA registran además la elevación real del
proceso, pues el manifiesto no reduce los permisos de un lanzador elevado.

La incorporación de fuentes entre copias requiere el flujo de integración
autorizado por el usuario. No copiar `out`, `.vs` ni `CMakeUserPresets.json` entre
copias. La aceptación de dos checkouts compilados requiere ejecutar las pruebas
en ambos después de integrar las mismas fuentes.

## Límites de esta base

`native/core` contiene perfiles y contratos independientes de Win32;
`native/platform/windows`, las rutas y servicios del sistema;
`native/presentation/windows`, la ventana y controles nativos con adaptación DPI.
Todavía no se ha recuperado el lector, el currículo, la evaluación, los compiladores
educativos ni el progreso Android. Tampoco se ha iniciado el renderizado GPU.
Esas capacidades conservan sus encargos separados en `PENDIENTES_WORK.md`.

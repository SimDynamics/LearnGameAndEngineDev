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
`10.0.28000.0`, CMake `4.3.1-msvc1` y Ninja `1.13.2`. Se utilizan las herramientas
de Visual Studio 2026 instaladas; no se descargan otras. Una selección incompatible
falla con un diagnóstico, en lugar de cambiar silenciosamente de herramientas.

**Actualización estable obligatoria del SDK:** `10.0.28000.2957` (septiembre de 2026).
La carpeta de cabeceras, bibliotecas y herramientas continúa llamándose `10.0.28000.0`:
el sufijo `.0` es el nombre de la familia, no identifica la actualización instalada.
CMake comprueba la versión completa registrada en las aplicaciones instaladas de Windows,
en ambas vistas del registro. Si no puede verificar `10.0.28000.2957`, se detiene con
un diagnóstico; no se supone instalada ni se sustituye por una versión anterior.
Instalador oficial: https://learn.microsoft.com/windows/apps/windows-sdk/downloads .
La comprobación no instala ni modifica el SDK o Visual Studio.

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

## Primer recorrido curricular

El inicio normal abre las seis páginas existentes de «Construir y leer un primer
programa», desde el JSON original bajo `app/src/main/assets/content/es/`.
No se genera una segunda copia del contenido. El adaptador comprueba el contrato
del JSON y las familias y direcciones históricas de `stateLayout` antes de cargar
respuestas o borradores. Las traducciones importadas se conservan sin cambios;
esta primera interfaz utiliza español.

Los controles Win32 conservan párrafos separados, énfasis y coloreado C++.
La predicción, la selección, el relleno y el ordenamiento usan los contratos del
contenido. Enter comprueba una respuesta de una línea sin avanzar de página.
Una respuesta correcta conserva el mecanismo explícito «Reiniciar respuesta».
Avanzar requiere los puzzles de la página; el compilador permanece editable.

Cada editor compila realmente con las herramientas MSVC/SDK fijadas y ejecuta
su programa en un proceso hijo limitado en tiempo y salida. No necesita Android
ni un servicio externo. Usa una consola, y «Entrada» solo aparece si el contenido
la solicita. Cuando hay `outputCheck`, el mismo botón compila, ejecuta y comprueba
la salida recién obtenida; un error de compilación o ejecución es un diagnóstico,
no una respuesta incorrecta. Editar invalida el veredicto, que nunca se restaura
como vigente tras reabrir. Las comprobaciones no descuentan corazones.

Los editores original y de modificación de la página 5 guardan borradores
independientes, incluidos espacios, saltos y procedencia de la sangría. TAB físico
y TAB del editor comparten la misma operación; deshacer/rehacer conserva la
identidad de las unidades. La entrada estándar es texto literal, sin asistencia
de código ni inserción automática de sangrías.

## Importar un estado Android exportado

En un perfil Windows vacío, «Importar estado Android…» permite seleccionar
explícitamente una carpeta que contenga `shared_prefs/*.xml` y opcionalmente
`files/`. No extrae datos del teléfono. Primero verifica versiones, familias,
tipos, proyectos, revisiones e integridad; después solicita confirmación.
El origen no se modifica. El destino se publica de forma atómica y rechaza
sobrescribir estado existente o un origen cambiado durante la revisión.

Las preferencias y los archivos originales se conservan también byte por byte
bajo el área privada `state/active/android`, con manifiesto SHA-256. El estado
Windows conserva las respuestas y las direcciones históricas, los borradores y
su entrada exacta, selecciones UTF-16 y metadatos de sangría. Una respuesta
editada pero no comprobada no se presenta como un intento incorrecto.
Los veredictos antiguos del compilador requieren siempre una ejecución nueva.

La primera validación utiliza exportaciones sintéticas y perfiles QA, no el
progreso personal. Los datos que Android conserva únicamente en memoria o en
`savedInstanceState` no pueden reconstruirse si no forman parte de la exportación.

## Arquitectura y límites actuales

`native/core` contiene perfiles, contratos de evaluación y modelos de estado;
`native/platform/windows`, los adaptadores de JSON, persistencia, importación y
ejecución; `native/presentation/windows`, los controles nativos con adaptación DPI.
La importación conserva archivos e historial de proyectos, pero su interfaz de
edición de múltiples archivos y restauración de revisiones aún no está migrada.
Tampoco se ha recuperado el catálogo completo, los porcentajes de curso, Kokoro,
la narración sincronizada, las funciones Android restantes ni el renderizado GPU.
La implementación de este primer recorrido no acredita esas capacidades ni la aceptación curricular de Work.

La ejecución educativa necesita la instalación MSVC/SDK indicada. El Job Object
gestiona cancelación y procesos descendientes; no es un sandbox de seguridad:
los programas tienen los permisos del usuario que inicia la aplicación. Los
artefactos de ejecución se conservan en `projects/cpp-runs` del perfil; todavía
no hay recolección automática. Los futuros contratos de salidas multilínea deben
definir explícitamente LF/CRLF de Windows; esta lección espera salidas sin salto
final y se comparan sus bytes exactos, sin recortar datos.

Work mantiene los pedidos en `PENDIENTES_RECURSOS_CURRICULARES.md`.
Codex comunica en el chat las capacidades comprobadas y sus límites para que
Work actualice ese único registro.

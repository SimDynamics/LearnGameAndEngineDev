# Base nativa Windows

La raíz contiene el proyecto CMake `LearnGameAndEngineDev`. El objetivo gráfico
`LearnGameAndEngineDev.exe` utiliza C++23, Win32 Unicode y destino x64. El código
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
La compilación de shaders HLSL utiliza la CPU y memoria disponibles (hasta ocho
procesos, con un presupuesto de 4 GiB por proceso). El límite puede fijarse con
`LGAED_SHADER_COMPILE_JOBS`; no cambia las opciones IEEE estrictas ni O3.

Windows es un proyecto independiente de Android. Su selección de herramientas
no está limitada por la API mínima, el bytecode ni las dependencias de Android.
El proyecto Android independiente continúa en `LearnGraphicsProgramming`.

Herramientas seleccionadas: paquete MSVC v145 `14.51.36231`
(compilador instalado `19.51.36260.0`), host/destino x64, Windows SDK y UCRT
`10.0.28000.0`, CMake `4.4.5` y Ninja `1.13.2`. MSVC, ATL y Ninja se reutilizan de
Visual Studio 2026 estable. CMake se instala bajo `C:\LGP-Tools\CMake`.
Una selección incompatible
falla con un diagnóstico, en lugar de cambiar silenciosamente de herramientas.

`toolchains/windows.versions.json` fija también Python `3.15.2`, FFmpeg `9.0.2`,
Vulkan SDK para Windows `1.4.363.0` y el JDK completo OpenJDK `27.0.1.0.3`. El SDK de
Vulkan se instala para el usuario con `copy_only=1`: contiene las herramientas
y cabeceras Windows y no instala otro controlador gráfico. No se sustituyen
por cabeceras o compiladores antiguos del NDK Android.
FFmpeg y Vulkan SDK también se alojan bajo `C:\LGP-Tools`, en carpetas versionadas.
Las herramientas extraídas desde Codex no se instalan en `AppData\Local\Programs`:
Windows puede redirigir esa escritura al caché privado de la aplicación empaquetada.
El selector y CMake comprueban las rutas físicas antes de compilar y rechazan esas
redirecciones. Python conserva su instalación física normal bajo AppData; el JDK
descargado por Android Studio conserva `.jdks`.
Las recetas de reconstrucción de herramientas Android utilizan PowerShell 7.4 o
posterior. La instalación compartida actual es PowerShell 7.6.9 mediante el MSI
oficial, bajo `C:\Program Files\PowerShell\7`; no utilizan como requisito el
runtime incluido en Codex. Los botones Windows continúan funcionando con
Windows PowerShell 5.1. Las dos ediciones cumplen funciones distintas y la
instalación MSI conserva la edición incluida en Windows.
CTest utiliza también el Git compartido 2.56.0.windows.4 bajo `Program Files`,
en lugar del Git que pueda incluir el proceso que abrió CMake. La preparación
del worker recibe FFmpeg por ruta explícita y no guarda el PATH de esa aplicación
en las reglas de compilación.

`Resolve-NativeTools.ps1` encuentra y valida las instalaciones al ejecutar cada
botón o compilación. Prepara únicamente el entorno de ese proceso y sus hijos;
no necesita conservar una consola abierta ni cambiar el Java de otros proyectos.
CMake resuelve las mismas rutas del manifiesto al abrir un preset desde Visual
Studio. Las rutas utilizan las carpetas del usuario actual, sin cuentas del
Windows anterior ni directorios de herramientas restauradas.
Las herramientas compartidas permanecen en sus instalaciones del equipo y las
dependencias descargadas del proyecto en sus directorios generados. Ninguna
compilación ni ejecución depende de `Documents\Codex`, de una conversación ni
de datos privados del agente.
`Build-NarrationWorker.ps1` utiliza también ese selector para reconstruir el
worker por separado. Ambos scripts de compilación restauran el entorno de la
consola al terminar, para no cambiar el Java de otro proyecto ejecutado después.
El alcance predeterminado `Application` conserva todas las comprobaciones,
incluido Vulkan. `Resolve-NativeTools.ps1 -Scope NarrationHost` sirve para la
preparación aislada del worker y omite sólo Vulkan; mantiene las versiones del
manifiesto, el JDK completo, FFmpeg y la comprobación de rutas físicas.

Descargar el JDK desde **Android Studio → Download JDK** y conservarlo bajo
`%USERPROFILE%\.jdks\openjdk-27*`. La selección Oracle OpenJDK del catálogo de
Studio ofrece el mantenimiento `27.0.1.0.3`; su entrada Temurin 27 inicial
todavía ofrece `27+35`. El JBR incluido en Studio tiene `javac`, pero
puede carecer de `include/jni.h`: el componente JNI de Kokoro y matemáticas
necesita además `jni_md.h` y `jvm.dll` del JDK completo. El runtime Windows usa
Java 27; el [compilador Kotlin `2.4.21`](https://raw.githubusercontent.com/JetBrains/kotlin/v2.4.21/libraries/tools/kotlin-gradle-compiler-types/src/generated/kotlin/org/jetbrains/kotlin/gradle/dsl/JvmTarget.kt)
admite como máximo bytecode/API Java 26,
por eso el worker compila con ese objetivo. Esto no depende de Android.
Las bibliotecas JVM Windows se resuelven directamente desde Maven Central
con `native/narration/worker/dependencies.lock.json` y SHA-256, sin necesitar
el caché Gradle ni una compilación del proyecto Android.

La instalación necesita también **C++ ATL** para ese toolset
(`Microsoft.VisualStudio.Component.VC.ATL`): el lector de voz de Windows utiliza
sus cabeceras mediante SAPI. El script comprueba su presencia antes de configurar.
Seleccionar sólo los componentes obligatorios de «Desarrollo para el escritorio
con C++» puede dejar ATL sin instalar.

**Última actualización estable publicada del SDK:** `10.0.28000.2957` (septiembre de 2026).
La carpeta de cabeceras, bibliotecas y herramientas continúa llamándose `10.0.28000.0`:
el sufijo `.0` identifica la familia y no demuestra la actualización instalada.
CMake exige SDK y UCRT de esa misma familia, MSVC x64 y herramientas compatibles;
no bloquea una compilación válida por la ausencia de un registro de desinstalación
que puede variar entre instalaciones de Visual Studio y del SDK independiente.
El selector compartido y CMake verifican el mantenimiento real de `rc.exe` y
`mt.exe`, incluidos los builds iniciados desde Visual Studio. No dependen del
nombre registrado del instalador. Si la actualización es anterior, instalar la versión estable desde
https://learn.microsoft.com/windows/apps/windows-sdk/downloads y recompilar.
Esta comprobación no instala software automáticamente.

Cada preset genera sus propios archivos en `out/build/<preset>`; el ejecutable
está en `bin/LearnGameAndEngineDev.exe` dentro de ese directorio. La instalación
CMake, si se solicita posteriormente, utiliza `out/install/<preset>`.

### Importación histórica en un runner Windows

El workflow `.github/workflows/windows-state-import.yml` ejecuta las cuatro
pruebas publicadas de importación de U11L16, U11L20 y los contratos de compilador
U11L19–L22/U12L20. Reutiliza los 52 escenarios existentes, las bibliotecas completas
`lgaed_core` y `lgaed_platform_windows`, el importador real y las fuentes
históricas autenticadas; recupera la historia Git completa.

La opción explícita `LGAED_STATE_IMPORT_QA_ONLY=ON` configura exclusivamente
esas cuatro pruebas en el CMake oficial. Conserva el toolchain Windows,
la verificación de dependencias y el perfil C++23 exacto. Comprueba las versiones
de MSVC, SDK/UCRT, CMake, Ninja, Python y Git del manifiesto; Python y Git pueden
proceder de las instalaciones compartidas del runner. Esta modalidad no configura
la presentación, los shaders ni el runtime JVM, que los modos de importación
seleccionados no utilizan. La configuración habitual conserva sus dependencias.

`scripts/windows/Test-StateImportCI.ps1` conserva la entrada de importación y
delega en `Test-NativeCurricularCI.ps1 -Suite StateImport`. El aprovisionador
compartido sólo admite un runner efímero alojado por GitHub. Verifica físicamente
el compilador antes de preparar el SDK; si falta
el mantenimiento requerido, obtiene su instalador oficial y comprueba la firma
Microsoft. No selecciona un toolset alternativo cuando el requerido está ausente.
Este script de aprovisionamiento no se utiliza en la PC del estudiante.

La ejecución utiliza `out/qa/state-import-ci`, registra exactamente cuatro
pruebas y las ejecuta en serie. El workflow conserva la salida de CTest y su XML
JUnit como artefactos durante catorce días. No publica cambios en la rama.
Un resultado satisfactorio acredita la importación y reapertura examinadas por
esos escenarios; no acredita la aplicación completa, los controles del lector,
la reproducción de PCM ni una ejecución educativa del compilador. El registro
de pendientes conserva esas verificaciones por separado.

### Reproductor real en un runner Windows

Este es un diagnóstico explícito del entorno. En la rama `Codex` se solicita
mediante un commit cuyo mensaje comience por `qa-audio:`; los pushes ordinarios
no ejecutan el probe. Su omisión no cuenta como una prueba superada.
Se conserva también `workflow_dispatch`, disponible
cuando el archivo exista en la rama predeterminada según el
[contrato de GitHub](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow).
El runner necesita una salida de audio utilizable: tener los dos servicios
activos no demuestra que exista un endpoint. Un probe fallido conserva su fallo
y no acredita los recorridos pendientes.

El workflow `.github/workflows/windows-narration-player.yml` utiliza
`Test-NativeCurricularCI.ps1 -Suite NarrationPlayer` y la opción oficial
`LGAED_NARRATION_PLAYER_QA_ONLY=ON`. Es mutuamente excluyente con los demás
modos focales, comparte la preparación y verificación de herramientas y compila
las bibliotecas completas `lgaed_core` y `lgaed_platform_windows`.
Conserva las aserciones de las dos pruebas positivas y añade una prueba negativa;
la configuración habitual registra los mismos tres casos:

- `native.narration_player_rates`: reloj real de `MediaPlaybackSession`,
  velocidades, liberación y reapertura con la posición conservada.
- `native.narration_player_qa_path`: ruta QA resuelta por Windows, identidad
  del archivo, reproducción, pausa, reapertura y desplazamiento de posición.
- `native.narration_player_failure_diagnostic`: archivo deliberadamente inválido
  en un QA nuevo, evento `MediaFailed` del reproductor real, conservación de sus
  propiedades entre llamadas, liberación del decoder y bytes/SHA-256 intactos.
  No exige un código de error concreto ni acredita reproducción.

Los WAV de las dos pruebas positivas son PCM de ceros creado expresamente para
comprobar el transporte. Su resultado no acredita síntesis Kokoro, voz audible, escucha
humana ni los recorridos de lecciones con PCM no silencioso. La prueba de ruta
informa además si observó una redirección real; ejecutarla fuera de MSIX no
verifica esa redirección.

El reproductor conserva `MediaFailed.Error`, `ExtendedErrorCode` y `ErrorMessage`
en una instantánea sincronizada. El mensaje público permanece igual; QA muestra
los campos disponibles y el HRESULT hexadecimal, también para fallos WinRT
síncronos. La consulta MMDevice identifica los endpoints de render activos y
las salidas predeterminadas de los tres roles; informa los errores de consulta
sin convertirlos en un recuento de cero ni omitir el intento de reproducción.
Un servicio activo o una lista CIM vacía no determina por sí solo la causa del
fallo de `MediaPlayer`.

CTest ejecuta los tres casos en serie, cada uno con un límite de 45 segundos.
El build, los logs y el primer WAV se aíslan bajo `out/qa/narration-player-ci`.
Las pruebas de ruta y diagnóstico conservan cada una su QA nuevo y único bajo
el `LocalAppData` que resuelve Windows; no crean un almacén de progreso.
En este probe sin `-VirtualAudio`, el script registra los servicios y dispositivos
presentes y puede arrancar `AudioEndpointBuilder` y `Audiosrv`
si ya existen, están detenidos y están habilitados. No cambia su tipo de inicio,
instala controladores ni crea endpoints. La disponibilidad de reproducción la
determinan las pruebas del reproductor, no el inventario de dispositivos.
El workflow conserva los logs y JUnit durante catorce días.

### Evaluación con un endpoint virtual en un runner hospedado

El workflow `.github/workflows/windows-virtual-audio.yml` es un experimento
explícito y acotado: se solicita mediante `workflow_dispatch` cuando esté
disponible en la rama predeterminada, o con un commit de `Codex` cuyo mensaje
comience por `qa-virtual-audio:`. Los pushes ordinarios no lo activan.
Primero ejecuta `Test-NativeCurricularCI.ps1 -Suite NarrationPlayer -VirtualAudio`.
Sólo si pasan sus tres pruebas originales continúa con `-Suite U12Reader`, en
el mismo runner, para conservar el endpoint entre ambas suites. No reduce las
aserciones ni convierte un fallo de preparación o reproducción en una omisión
satisfactoria.

Las entradas completas, exclusivamente desde el checkout de un runner Windows
efímero hospedado por GitHub, son:

```powershell
$ErrorActionPreference = 'Stop'
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite NarrationPlayer -VirtualAudio
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite U12Reader
```

`-VirtualAudio` sólo se admite junto con `-Suite NarrationPlayer`. La preparación
no se utiliza en la PC del usuario ni forma parte de los botones de compilación
local. El aprovisionador invoca `Prepare-VirtualAudioCI.ps1` con las rutas ya
seleccionadas mediante `-PythonPath`, `-SignToolPath` y `-LogDirectory`; los
diagnósticos de esta preparación quedan en
`out/qa/narration-player-ci/virtual-audio`.

La descarga procede del [paquete oficial VB-CABLE46](https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack46.zip).
Se exige su SHA-256 fijo
`bc44e8974191a5a2427d6c979e2dab08dbb09925f3f435dfa2f0ba3e45b6eb90`.
Antes de crear un dispositivo, se comprueba la firma Microsoft del catálogo
`vbaudio_cable64_win10.cat` con la confianza existente de Windows y se utiliza
SignTool con `verify /kp /v /c` para verificar la política de firma kernel
y la pertenencia de `vbMmeCable64_win10.inf` y
`vbaudio_cable64_win10.sys` al catálogo. La comprobación sigue la
[verificación de firmas de controladores de Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/verifying-the-release-signature)
y las [opciones de SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).
El hash o el nombre del firmante por sí solos no acreditan esa aceptación.
No combina `/kp` con `/o`: el SignTool del SDK declarado rechaza esa combinación
durante el análisis de argumentos. Se conserva el comando de verificación de
release documentado por Microsoft; Windows aplica además su política vigente
al instalar y cargar el dispositivo. Un fallo de firma o pertenencia detiene
el proceso y conserva también la ayuda del verificador para diagnosticarlo.

Tras superar esas comprobaciones, `install_ci_audio_device.py` utiliza las APIs
SetupDi y `UpdateDriverForPlugAndPlayDevicesW` con
`INSTALLFLAG_NONINTERACTIVE`, sin `INSTALLFLAG_FORCE`. Rechaza un dispositivo
previo con el mismo identificador de hardware. No ejecuta el instalador EXE del
proveedor, importa certificados, modifica almacenes de confianza ni cambia la
integridad de código, Secure Boot o el modo de prueba de firmas. Si Windows
requiere una interacción, la llamada falla; no se automatiza su aceptación.

El README del paquete y el [manual de VB-CABLE](https://vb-audio.com/Cable/VBCABLE_ReferenceManual.pdf)
indican reiniciar para finalizar la instalación. Este experimento con las APIs
de Microsoft no afirma seguir la instalación recomendada del proveedor sin
ese reinicio: falla si observa `DI_NEEDRESTART`, `DI_NEEDREBOOT` o
`bRebootRequired`, y nunca reinicia automáticamente. Un retorno satisfactorio
de la API tampoco acredita reproducción. La consulta MMDevice y las pruebas
originales determinan si hay un endpoint utilizable y si el transporte real
de MediaPlayer funciona con él.

VB-CABLE es donationware. La [licencia del proveedor](https://vb-audio.com/Services/licensing.htm)
permite instalar y evaluar primero; este workflow opt-in documenta esa
evaluación acotada y no concede aprobación tácita para un despliegue profesional
repetido. El endpoint virtual transporta audio dentro del sistema: un resultado
satisfactorio no acreditaría salida acústica ni escucha humana. La revisión
visual y auditiva durante el estudio sigue correspondiendo al usuario.

La preparación y la secuencia de suites deben ejecutarse para obtener un
resultado. Su incorporación al repositorio no acredita que la firma sea
aceptada en el runner, que el endpoint llegue a estar operativo ni que las
pruebas hayan pasado.

### Síntesis y diagnóstico del worker en un runner Windows

El workflow `.github/workflows/windows-narration-worker.yml` utiliza
`Test-NativeCurricularCI.ps1 -Suite NarrationWorker` y la opción oficial
`LGAED_NARRATION_WORKER_QA_ONLY=ON`, excluyente con los demás modos focales.
Su propósito es comprobar la síntesis Kokoro/Dora real y el puente nativo en
Windows, además de la conservación del diagnóstico de un fallo de reproducción.
No configura el lector, la presentación, los shaders ni las pruebas positivas
del reproductor. El modo habitual `Application` conserva sus dependencias.
El workflow se activa en los pushes de `Codex` que coinciden con sus filtros de
rutas y conserva los logs y el XML JUnit durante catorce días.

En la raíz del checkout del runner efímero de GitHub, la entrada completa es:

```powershell
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite NarrationWorker
```

El aprovisionador conserva las versiones y rutas verificadas del toolchain,
el perfil C++23 exacto y las bibliotecas completas `lgaed_core` y
`lgaed_platform_windows`. Prepara el runtime con el alcance `NarrationHost` del
selector compartido: necesita el JDK completo y FFmpeg del manifiesto, además
de las dependencias JVM autenticadas; no necesita Vulkan para esta suite.

Los recursos se preparan en directorios de staging aislados. La entrada
`scripts/windows/prepare_narration_ci_resources.py` recibe `--resources` y
`--english-resources`, ambos con rutas absolutas separadas. Descarga únicamente
los seis archivos españoles/Dora definidos por los manifiestos Kotlin existentes,
con sus revisiones inmutables, tamaños y SHA-256. El paquete auxiliar inglés FP32,
su procedencia y sus avisos de licencia se verifican y copian desde los recursos
originales del checkout. Un destino existente incompatible se rechaza; no se
sobrescriben modelos, recursos fuente ni grabaciones. El workflow no restaura
cachés de narración: los primeros intentos se realizan en el checkout efímero.
La prueba de PCM español exige además que su respuesta declare síntesis nueva,
sin acierto de caché ni reutilización de una grabación empaquetada o histórica;
el caso nativo conserva su comprobación posterior de reutilización.

La configuración registra exactamente estas tres pruebas y las ejecuta en serie:

| Prueba | Criterios de la prueba |
| --- | --- |
| `native.narration_worker` | Puente C++ al worker, síntesis Kokoro con Dora, identidad, tiempos de palabras, reutilización de la misma grabación y matemáticas exactas. Conserva también sus escenarios controlados de protocolo IPC y cancelación, que no son pruebas de audio. |
| `native.narration_spanish_pcm` | Caso Python original `test_03_spanish_math_is_progressive_through_numbers_and_operators`: PCM mono de 24 kHz y 16 bits no silencioso, correspondencia con el texto y avance sobre números y operadores españoles. |
| `native.narration_player_failure_diagnostic` | El evento real `MediaFailed` del intento con un medio inválido, sus tres propiedades, su estabilidad y la conservación del archivo al liberar el decoder. No acredita que exista una salida de audio utilizable. |

Los dos casos del worker tienen un límite de 180 segundos; el diagnóstico
negativo tiene 45 segundos. El build y sus logs quedan bajo
`out/qa/narration-worker-ci`; el runtime mantiene `out/narration/runtime.json` y
las pruebas sus directorios QA originales dentro de `out/qa`. Estas rutas
pertenecen al checkout efímero y no a los perfiles personales del alumno.

La suite debe compilarse y ejecutarse para obtener evidencia de su resultado;
su registro en CMake no la declara superada. Un resultado satisfactorio acredita
sólo esos escenarios de síntesis, puente nativo y diagnóstico. No acredita
reproducción audible, escucha humana, pausas del reproductor ni recorridos
integrados del lector. Las dos pruebas positivas de reproducción se conservan
en los probes explícitos del reproductor, con sus aserciones completas.

### Compilación de los recorridos curriculares en Windows

El workflow `.github/workflows/windows-reader-build.yml` utiliza
`Test-NativeCurricularCI.ps1 -Suite ReaderBuild` y la opción oficial
`LGAED_READER_BUILD_QA_ONLY=ON`, excluyente con los otros modos focales.
Compila y enlaza los tres ejecutables originales:

- `lgaed_lesson_reader_tests`.
- `lgaed_u11_declarations_reader_tests`.
- `lgaed_u6_diagnostic_flow_tests`.

La biblioteca curricular contiene las mismas fuentes de presentación que consume
la aplicación y mantiene el manifiesto de controles comunes. Se conservan las
bibliotecas completas de núcleo y plataforma y las comprobaciones del toolchain
Windows y del perfil C++23. Los shaders de las vistas gráficas y el runtime JVM
no forman parte del enlace de estos ejecutables; la configuración normal sigue
preparando sus dependencias y registrando sus pruebas.

Este modo sólo comprueba compilación y enlace. No registra ni ejecuta CTest,
prepara los fixtures históricos, genera voz ni inicia el lector. Los recorridos
integrados continúan requiriendo sus fuentes auténticas, runtime y modelos
verificados, reproducción real y ejecución de cada caso. Los binarios y logs
quedan bajo `out/qa/reader-build-ci`; el workflow conserva durante catorce días
la salida de compilación y un JSON con tamaño y SHA-256 de cada ejecutable.

### Lector histórico de U12 con síntesis y reproducción reales

`Test-NativeCurricularCI.ps1 -Suite U12Reader` y
`LGAED_U12_READER_QA_ONLY=ON` preparan y ejecutan las tres fases originales de
la recuperación histórica de narración de U12, lección 4. El modo es excluyente
con los demás modos focales y conserva el toolchain Windows, el perfil C++23
exacto, las bibliotecas completas de núcleo y plataforma, los controles
curriculares de producción y el ejecutable completo `lgaed_lesson_reader_tests`.
No configura la aplicación gráfica completa, sus shaders ni los backends
numéricos que estas pruebas no utilizan.

El runtime real se prepara con `NarrationHost`, las herramientas exactas del
manifiesto, Kokoro/Dora y los recursos auxiliares ingleses autenticados. Emplea
el mismo preparador y staging aislado descritos para `NarrationWorker`, con
destinos nuevos y sin restaurar cachés de narración. Requiere además un endpoint
Windows de renderizado operativo; la síntesis por sí sola no satisface las
aserciones de reproducción.

`cmake/U12PublishedReaderTests.cmake` comparte con el modo habitual el exportador
histórico, los fixtures auténticos `before.json`, `after.json` y `manifest.json`,
y el registro de las pruebas. Conserva los argumentos y las aserciones de la
adopción directa de la publicación posicional de U12L4 a su revisión publicada
con diecisiete entradas editoriales. La exportación necesita la historia Git
completa para autenticar ambas publicaciones. Se registran exactamente estos
tres casos:

- `native.u12_composed_output_published17_reader_0`.
- `native.u12_composed_output_published17_reader_1`.
- `native.u12_composed_output_published17_reader_2`.

CTest los ejecuta en serie, con un límite de 300 segundos por caso. Las pruebas
exigen PCM y tiempos reales, una pausa obtenida mediante reproducción, la
recuperación del párrafo correspondiente al adoptar la revisión y la
conservación de respuestas, borradores independientes y WAV compatibles.
La respuesta ordinaria previa que aísla la entrada de la tercera fase sigue
identificada como dato QA sintético: no concede una comprobación del compilador
ni un marcador de reproducción. No se fabrican avances del reloj, eventos del
reproductor ni evidencia de reproducción para suplir un endpoint ausente.

El build, los fixtures, los logs y el XML JUnit quedan bajo
`out/qa/u12-reader-ci`; el runtime conserva `out/narration/runtime.json` y las
grabaciones de cada escenario utilizan sus directorios QA originales. La
secuencia opt-in `windows-virtual-audio.yml` sólo inicia esta suite después de
superar `NarrationPlayer` en el mismo runner. Los logs y el XML JUnit de cada
ejecución permiten comprobar qué casos finalizaron correctamente. Una ejecución
que supere las tres fases acredita ese recorrido con el dispositivo observado,
sin atribuir escucha humana, validación acústica, ejecución del compilador
educativo ni aceptación de los demás recorridos curriculares.

### Recorridos editoriales, compiladores históricos y diagnósticos de U6

El workflow `.github/workflows/windows-curricular-readers.yml` ejecuta los
recorridos originales en tres runners Windows independientes. Se solicita
mediante un commit de `Codex` cuyo mensaje comience por
`qa-curricular-readers:` o mediante `workflow_dispatch` cuando esté disponible
en la rama predeterminada. Los pushes ordinarios no lo activan.

Cada job comprueba primero el dispositivo firmado con
`-Suite NarrationPlayer -VirtualAudio`. Sólo después de superar sus tres
pruebas prepara un runtime nuevo y ejecuta la suite asignada. Los jobs no
comparten perfiles, grabaciones ni runtime; un fallo de un grupo no cancela
los otros. Cada grupo ejecuta sus casos en serie y conserva los límites de
tiempo originales de CTest.

| Suite | Modo CMake | Recorridos originales | Directorio bajo `out/qa/` |
|---|---|---|---|
| `U11EditorialReader` | `LGAED_U11_EDITORIAL_READER_QA_ONLY` | Diez: asignaciones publicadas, fases 0–5; resto, publicaciones 26 y 30, fases 0–1 | `u11-editorial-reader-ci` |
| `CompilerHistoricalReader` | `LGAED_COMPILER_HISTORICAL_READER_QA_ONLY` | Catorce: declaraciones, asignaciones y constantes, fases 0–1; sesión y caracteres, fases 0–3 | `compiler-historical-reader-ci` |
| `U6DiagnosticReader` | `LGAED_U6_DIAGNOSTIC_READER_QA_ONLY` | Tres: declaraciones, interfaces y proyecto; doce diagnósticos curriculares en total | `u6-diagnostic-reader-ci` |

Desde el checkout de un runner hospedado nuevo, este ejemplo ejecuta el grupo
editorial completo:

```powershell
$ErrorActionPreference = 'Stop'
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite NarrationPlayer -VirtualAudio
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite U11EditorialReader
```

Para los compiladores históricos, en otro runner nuevo:

```powershell
$ErrorActionPreference = 'Stop'
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite NarrationPlayer -VirtualAudio
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite CompilerHistoricalReader
```

Para los diagnósticos de U6, también en su propio runner nuevo:

```powershell
$ErrorActionPreference = 'Stop'
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite NarrationPlayer -VirtualAudio
& .\scripts\windows\Test-NativeCurricularCI.ps1 -Suite U6DiagnosticReader
```

La preparación de síntesis rechaza destinos ya existentes para conservar los
datos; no se encadenan estas tres suites dentro del mismo checkout.

Los modos focales utilizan las bibliotecas y controles de producción, los
ejecutables originales, Kokoro/Dora y los recursos autenticados. Los fixtures
se exportan de la historia Git completa mediante los mismos generadores que
el build habitual. Se conserva la procedencia de las respuestas ordinarias
de preparación QA; los objetivos del compilador y la escucha se acreditan
mediante las ejecuciones y reproducciones reales que exige cada prueba.
La configuración debe registrar exactamente los casos de su grupo: una prueba
ausente, un fallo de preparación o un error de CTest detienen ese job.

Los artefactos de cada grupo conservan durante catorce días los logs de
preparación, compilación, audio y pruebas, sus informes JUnit y los manifiestos
de fuentes y herramientas. Los resultados sólo acreditan los casos ejecutados
correctamente y su versión; la compilación por sí sola no acredita esos
recorridos. La revisión visual y la escucha humana durante el estudio mantienen
su alcance propio.

### Comprobación local de ventanas y datos

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

El progreso personal se guarda en **`%APPDATA%\LearnGameAndEngineDev\<perfil>`**
(AppData\Roaming), con los perfiles `Work` y `Codex` separados. El archivo
autoritativo es `state\active\state.bin`: incluye respuestas, progreso, borradores,
ajustes y posición de lectura. La carpeta `state\active` conserva también archivos
importados e historial de proyectos C++. No basta con copiar la subcarpeta vacía
`progress`. Los audios, modelos y recursos regenerables continúan bajo
`%LOCALAPPDATA%\LearnGameAndEngineDev\<perfil>` (`cache`, `projects` y `logs`).

**Copia manual:** cerrar las aplicaciones de ambos perfiles y copiar la carpeta
`%APPDATA%\LearnGameAndEngineDev` completa a la ubicación externa elegida por el
usuario. Después de reinstalar Windows, restaurarla en AppData\Roaming del usuario
actual **antes de abrir la aplicación**. Las opciones globales muestran la ruta
exacta y permiten copiarla. No se programa ninguna copia externa automática;
Roaming por sí solo no sobrevive a un formateo.

Al abrir por primera vez esta versión, con las copias anteriores cerradas, se
valida y copia el estado de LocalAppData a Roaming sin borrar ni recodificar el
original. Se comprueba cada archivo y se publica el directorio completo de forma
atómica. Un estado corrupto o dos estados incompatibles se conservan y causan un
error; no se reemplazan por progreso vacío. Un marcador detecta modificaciones
posteriores en el origen antiguo. La sesión bloquea las escrituras de binarios
antiguos y otra apertura personal del mismo perfil mientras está en uso.

Se solicita Roaming con `KF_FLAG_NO_PACKAGE_REDIRECTION` y se comprueba además la
ruta física del directorio abierto: una ejecución normal redirigida al espacio
privado de un paquete se rechaza antes de trasladar o guardar progreso. Los
[flags de carpetas conocidas](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ne-shlobj_core-known_folder_flag)
controlan la ruta devuelta; la
[virtualización MSIX de archivos](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes)
también puede afectar a las escrituras posteriores.

Las pruebas añaden `--qa-instance <identificador>` y conservan su subperfil
aislado `%LOCALAPPDATA%\LearnGameAndEngineDev\<perfil>\qa\<identificador>`; nunca
leen ni migran progreso personal. `--smoke-test` exige ese aislamiento, abre y pinta
una ventana real y la cierra.

No se enlazan fuentes, binarios ni cachés del checkout vecino. No hay un mutex,
puerto o servicio global que impida ejecutar perfiles distintos o instancias QA
simultáneamente. El bloqueo de sesión se limita al perfil personal concreto. El manifiesto
solicita `asInvoker`; los resultados QA registran además la elevación real del
proceso, pues el manifiesto no reduce los permisos de un lanzador elevado.

La incorporación de fuentes entre copias requiere el flujo de integración
autorizado por el usuario. No copiar `out`, `.vs` ni `CMakeUserPresets.json` entre
copias. La aceptación de dos checkouts compilados requiere ejecutar las pruebas
en ambos después de integrar las mismas fuentes.

## Primer recorrido curricular

La preparación española de Windows limita las referencias C++ implícitas a sus
operandos y operadores. La prosa posterior a una asignación o declaración,
como «es una definición, no una actualización», conserva sus palabras y tildes.
Los fragmentos explícitos mantienen la lectura técnica habitual de sus
identificadores. El mismo ajuste se utiliza en lectura inmediata y anticipada,
con posiciones UTF-16 sobre la fuente original. Las claves de grabaciones siguen
dependiendo de su texto preparado: una lectura corregida obtiene su propia
clave, y las grabaciones compatibles se conservan y reutilizan. Las pruebas
`native.windows_symbol_speech` y `native.spanish_prose_boundaries` comprueban
la preparación, la correspondencia de palabras y audio Kokoro real; no sustituyen
la escucha humana de la pronunciación.

Los campos de puzzles C++ de la ruta Programación transmiten un contexto
interno de lectura, tanto al lector visible como a la generación anticipada y
al informe de audios. En español, las expresiones numéricas de esos campos se
leen como operadores: `11/2` conserva la barra y una igualdad de resultados usa
«igual a». Las asignaciones de código mantienen «asigna». La prosa general y
los componentes matemáticos conservan su contexto automático; las fórmulas
explícitas y el `mathMode` curricular prevalecen. Sus valores siguen siendo
`auto`, `text`, `inline` y `block`; el contexto técnico no añade otro modo de
autoría ni modifica los JSON.

El lector reconoce el guion automático anterior por su clave completa y
traslada su marcador dentro del mismo historial de tres páginas. Una frase
cuya preparación cambia vuelve a su inicio sin heredar tiempos ni evidencia
de escucha de la grabación anterior. Las posiciones compatibles y los otros
dos marcadores se conservan; una página terminada tampoco acredita una
explicación nueva. No se borran ni se sustituyen los audios anteriores.
`native.cpp_numeric_context` contrasta los planes inmediatos y anticipados y
las tres voces españolas; `native.cpp_numeric_reader` comprueba audio real,
correspondencia UTF-16 y migración de pausas y páginas terminadas. Son pruebas
técnicas y no sustituyen la escucha ni la aceptación curricular de Work.

En ese mismo contexto C++, una mención aislada de `(` o `)` después de un
verbo de lectura o de un nombre de token se prepara como «abre paréntesis» o
«cierra paréntesis». Los incisos balanceados, las citas y las fórmulas conservan
su tratamiento. La política anterior puede reconstruirse exclusivamente para
autenticar un marcador guardado, manteniendo la lectura numérica C++ que ya
tenía. La frase cuya grabación cambia vuelve a cero sin heredar escucha;
las posiciones compatibles y los otros marcadores se conservan. El modo
curricular y los archivos de contenido permanecen iguales.
Si una pausa está después de una explicación acertada cuya lectura cambió,
también vuelve a esa explicación nueva, aunque la página no estuviera terminada.
`native.cpp_parenthesis_context` verifica planes y PCM en tres voces;
`native.cpp_parenthesis_reader` contrasta pausas reales antes, dentro y después
del cambio, la migración en el mismo historial y la reapertura sin autoplay.

`native.equality_count_reader` comprueba el recorrido actual de los cuatro
conteos de U9L12. Parte de un perfil vacío y ejecuta mediante los controles el
programa inicial y la referencia completa con MSVC. Comprueba el bloqueo del
puzzle dependiente, los cuatro campos identificados por sus nombres accesibles,
borrador parcial, rechazo, corrección, guía y explicación. Las pausas se obtienen
reproduciendo PCM real y se restauran al reabrir la vista; los otros dos
marcadores, los WAV y el objetivo del compilador se conservan. La prueba se
detiene ante el siguiente puzzle pendiente y no acredita el cierre de la lección.

Para comprobar ese recorrido en la aplicación completa, el mismo ejecutable de
prueba admite `--prepare-visual visual-equality-<token-nuevo>`. Crea una QA Codex
nueva en el editor previo, sin respuestas, recibos ni código resuelto; rechaza
un perfil que ya exista. Se abre el ejecutable instalado con `--profile Codex`
y `--qa-instance visual-equality-<token-nuevo>`. La opción `--inspect-visual` con
ese mismo token sólo lee respuestas, fuente, recibo, página, desplazamiento e
historial, y comprueba que los hashes del estado y del asset no cambien durante
la inspección. Conviene cerrar la QA antes de inspeccionarla para evitar una
escritura concurrente. Los contadores de archivos y marcadores no sustituyen
la comprobación de la ejecución ni demuestran reproducción o escucha.

El inicio normal abre las seis páginas existentes de «Construir y leer un primer
programa», desde el JSON original bajo `app/src/main/assets/content/es/`.
No se genera una segunda copia del contenido. El adaptador comprueba el contrato
del JSON y las familias y direcciones históricas de `stateLayout` antes de cargar
respuestas o borradores. Las traducciones importadas se conservan sin cambios;
esta primera interfaz utiliza español.

Los controles Win32 conservan párrafos separados, énfasis y coloreado C++.
Las referencias técnicas dentro de prosa, preguntas, opciones y explicaciones
visibles usan la misma paleta y el mismo lexer que el editor para el color de
las letras. El resaltado de fondo es neutro e independiente de esas categorías,
sin añadir líneas de subrayado. Los números de salida, resultados y cantidades
en prosa usan la tinta neutra activa de Matemáticas y el mismo fondo; los literales que pertenecen al
código conservan su categoría C++. Los años y localizadores de citas
parentéticas o narrativas mantienen su tratamiento bibliográfico ordinario.
El fondo numérico corresponde al texto curricular, incluidas las cifras de
versiones como C++20 y de las fórmulas integradas en ese texto. No se extiende
a las consolas, los editores de código, los menús de opciones ni al contador
de lección y página. El fondo no sustituye la tinta correspondiente a cada cifra.
La clasificación sólo utiliza el contexto visible, sin consultar respuestas
ocultas ni añadir propiedades al JSON. Conserva negritas, cursivas, texto,
offsets de narración y contratos de estado.

Las muestras técnicas de opciones incorrectas tienen el mismo fondo que las
demás muestras: se clasifican por el enunciado y las alternativas visibles,
sin consultar la respuesta correcta. El fondo usa la composición original de
Android sobre la superficie local (alfa 8 en Programación y 16 en Matemáticas,
con su respaldo oscuro original para programación sobre superficies claras).
Las referencias de una misma línea comparten una altura que incluye las
métricas de sus fuentes y un pequeño margen escalado arriba y abajo.

Durante la lectura todo el texto pendiente pasa a gris, incluidas las
referencias coloreadas. El gris común sigue la política de contraste de
Android; al avanzar o cerrar el lector se recupera la tinta original.
Retroceder vuelve a atenuar las letras. El fondo permanece idéntico antes,
durante y después de la lectura: nunca depende del estado leído/pendiente,
del acento ni del progreso. La identificación de referencias se almacena
como metadatos del renderizador, sin depender de un estilo tipográfico.
El alto contraste utiliza
los colores del sistema. `lgaed_curricular_color_checks` compila la aplicación
y las pruebas de formato, montaje curricular y reproducción real afectadas.

La barra de título principal mantiene los botones y el comportamiento nativos
de Windows. Al activarse utiliza el acento de la pantalla actual: el global en
el menú principal y el de la ruta dentro de sus cursos y opciones;
al desactivarse utiliza gris neutro, con texto de contraste en ambos estados.
Navegar o guardar otro acento actualiza también la barra. Los atributos de
[DWM](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)
sólo afectan a esta ventana: el alto contraste conserva los colores del sistema
y una plataforma que no los admita mantiene su marco predeterminado.

F12 alterna pantalla completa sin marco y restaura el tamaño, posición y
estado maximizado anteriores al pulsarlo de nuevo. Funciona también desde
editores, opciones y diálogos internos, conservando los mismos documentos y
la reproducción. Mantener F12 pulsado no repite la alternancia.

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
El catálogo, los proyectos de varios archivos, la narración y los laboratorios
gráficos tienen componentes y pruebas específicos. Las comprobaciones del primer
recorrido descritas arriba no acreditan por sí solas esas capacidades ni la
aceptación curricular de Work.

Al cerrar la aplicación, `NarrationWorker::shutdown()` detiene únicamente su
proceso y despierta las solicitudes pendientes antes de esperar a los productores
y lectores. El cierre no depende de que ONNX termine de cargar un modelo o
confirme una cancelación. No envía otra solicitud por una tubería bloqueada ni
elimina grabaciones o cachés; otro perfil conserva su propio servicio. Durante el
uso normal, la inferencia sigue confirmando su cancelación antes de ceder su turno.
El cierre automático de smoke utiliza la misma ruta `WM_CLOSE`. La prueba
`native.narration_shutdown` comprueba ese ciclo con procesos de transporte
controlados, incluyendo una preparación sin respuesta y una escritura bloqueada;
no sustituye las pruebas de síntesis real.

La ejecución educativa necesita la instalación MSVC/SDK indicada. El Job Object
gestiona cancelación y procesos descendientes; no es un sandbox de seguridad:
los programas tienen los permisos del usuario que inicia la aplicación. Los
artefactos de ejecución se conservan en `projects/cpp-runs` del perfil; todavía
no hay recolección automática. Los futuros contratos de salidas multilínea deben
definir explícitamente LF/CRLF de Windows; esta lección espera salidas sin salto
final y se comparan sus bytes exactos, sin recortar datos.

Work mantiene los pedidos en `PENDIENTES_RECURSOS_CURRICULARES.md`.
Codex comunica en el chat las capacidades comprobadas y sus límites para que
se conserve ese único registro y retira de su copia los pedidos resueltos según
las instrucciones del usuario. La integración o aceptación que falte permanece
registrada.

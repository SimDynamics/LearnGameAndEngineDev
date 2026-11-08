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

El workflow `.github/workflows/windows-narration-player.yml` utiliza
`Test-NativeCurricularCI.ps1 -Suite NarrationPlayer` y la opción oficial
`LGAED_NARRATION_PLAYER_QA_ONLY=ON`. Es mutuamente excluyente con la modalidad
de importación, comparte la preparación y verificación de herramientas y compila
las bibliotecas completas `lgaed_core` y `lgaed_platform_windows`.
Reutiliza, sin modificar su código ni sus aserciones, las dos pruebas que también
registra la configuración habitual:

- `native.narration_player_rates`: reloj real de `MediaPlaybackSession`,
  velocidades, liberación y reapertura con la posición conservada.
- `native.narration_player_qa_path`: ruta QA resuelta por Windows, identidad
  del archivo, reproducción, pausa, reapertura y desplazamiento de posición.

Los WAV de ambas pruebas son PCM de ceros creado expresamente para comprobar
el transporte. Su resultado no acredita síntesis Kokoro, voz audible, escucha
humana ni los recorridos de lecciones con PCM no silencioso. La prueba de ruta
informa además si observó una redirección real; ejecutarla fuera de MSIX no
verifica esa redirección.

CTest ejecuta ambos casos en serie con sus límites originales de 45 segundos.
El build, los logs y el primer WAV se aíslan bajo `out/qa/narration-player-ci`.
La prueba de ruta conserva su QA nuevo y único bajo el `LocalAppData` que
resuelve Windows; no crea un almacén de progreso. El script registra los servicios
y dispositivos presentes y puede arrancar `AudioEndpointBuilder` y `Audiosrv`
si ya existen, están detenidos y están habilitados. No cambia su tipo de inicio,
instala controladores ni crea endpoints. La disponibilidad de reproducción la
determinan las pruebas del reproductor, no el inventario de dispositivos.
El workflow conserva los logs y JUnit durante catorce días.

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

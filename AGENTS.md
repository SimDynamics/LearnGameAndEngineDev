# LearnGameAndEngineDev: acuerdo de trabajo

Lee íntegramente `docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt` al empezar, al reanudar y después de una compactación. La copia conserva el documento proporcionado por el usuario. Las instrucciones posteriores del usuario sobre Windows, directorios y registros prevalecen sobre las ubicaciones Android y el registro único mencionados en ese documento.

## Directorios y responsabilidades vigentes

- Work: `E:\PersonalFiles\Projects\LearnGameAndEngineDev-Work`, exclusivamente en `main`. Diseña y verifica currículo, estudia referencias, redacta contenido y especifica las capacidades necesarias.
- Codex: `E:\PersonalFiles\Projects\LearnGameAndEngineDev-Codex`, exclusivamente en la rama `Codex`. Implementa y verifica la aplicación Windows y sus componentes técnicos.
- Cada agente trabaja en su directorio. No cambiar de rama el checkout del otro, sobrescribir sus cambios, crear worktrees adicionales ni ejecutar sus botones de publicación.
- Guardar progresivamente en los archivos locales. Work y Codex no crean commits ni publican sin una instrucción expresa. El usuario utiliza sus botones para esas operaciones. La importación conserva los commits anteriores; no constituye una implementación Windows terminada.

## Registro único de pendientes

- `PENDIENTES_RECURSOS_CURRICULARES.md` es el único registro de pedidos y capacidades pendientes. Work crea y mantiene los pedidos, incluidos los encargos de Windows.
- Codex consulta ese archivo, implementa y verifica los componentes técnicos y retira de su copia local los encargos realizados, según la instrucción del usuario. Conserva los apartados cuya integración, aceptación visual, escucha o verificación sigan pendientes y comunica en el chat el alcance comprobado.
- Codex no modifica el checkout de Work ni crea registros paralelos, informes o reportes de avance. Los cambios de la aplicación y sus pruebas se guardan directamente en los archivos técnicos.
- `PENDIENTES_WORK.md`, si se conserva, es una nota interna de Work; no contiene pedidos activos ni es un canal de encargos o actualizaciones de Codex. No recrear `ACTUALIZACIONES_CODEX.md`.
- Conservar las fichas, identificadores y evidencias existentes. Distinguir implementación técnica, integración curricular y aceptación comprobada en Windows: las pruebas Android mantienen su alcance original y no acreditan Windows.

## Conservación y prioridad de migración

- El proyecto Android independiente permanece en `E:\PersonalFiles\Projects\LearnGraphicsProgramming`. No modificarlo durante un encargo exclusivamente Windows. Un encargo expreso del usuario para Android autoriza sus cambios técnicos en esa carpeta; no convierte el Android importado de la copia Codex en la aplicación Android activa. Conservar siempre sus recursos y datos.
- Conservar el contenido importado, sus idiomas, IDs, respuestas, índices, mapas `stateLayout`, familias y direcciones históricas. No renombrar esos identificadores al cambiar el nombre del proyecto.
- Work produce y revisa únicamente español. Las traducciones existentes se conservan; su actualización corresponde al usuario.
- El contenido sigue en `app/src/main/assets/content/` hasta que Codex integre una ubicación común compatible y Work compruebe su conservación. No mantener dos copias activas del currículo.
- Primero recuperar en Windows lo ya construido. Programación 1 sigue abierto. No ampliar rutas ni comenzar la evaluación visual de proyectos antes de disponer de la base Windows y sus verificaciones correspondientes.
- Conservar los compiladores, modelos, audios, pruebas, preferencias y evidencias originales. No borrar cachés ni importar datos reales del estudiante para probar.

## Herramientas y plataformas independientes

- Windows debe usar las mejores versiones estables compatibles con sus propios requisitos. No limitar su JDK, bibliotecas ni capacidades por las dependencias de Android 14.
- Android debe funcionar principalmente en Android 14 (API 34), incluido Honor y LDPlayer. Verificar la versión real del dispositivo; no deducirla del nombre del emulador. Conservar la versión mínima admitida salvo necesidad expresamente justificada; actualizar el objetivo no obliga a elevar ese mínimo.
- Investigar versiones estables y compatibilidad en documentación oficial antes de actualizar. Conservar una versión anterior sólo por una incompatibilidad concreta, no por un pin histórico. Windows declara sus herramientas en `toolchains/windows.versions.json` y sus bibliotecas JVM en `native/narration/worker/dependencies.lock.json`.
- Preferir las herramientas Android y la descarga del JDK completo desde Android Studio. Reutilizar ese JDK para Windows cuando sea compatible y contenga JNI para Windows (`jni.h`, `jni_md.h` y `jvm.dll`); no instalar otro JDK por WinGet para suplir la misma necesidad. Un componente del SDK/NDK Android no sustituye una dependencia Windows.
- Compartir instalaciones compatibles sin forzar una unificación entre proyectos. Revisar los requisitos de los proyectos vecinos antes de retirar herramientas; no modificar instalaciones compartidas mientras otro proceso las use. No borrar modelos, grabaciones, cachés, configuraciones personales ni progreso para limpiar una instalación.
- No guardar dependencias necesarias de las aplicaciones en `Documents\Codex`, en carpetas de conversaciones ni en datos privados del agente. Usar los directorios de los proyectos y las instalaciones compartidas estables del equipo; compilar y ejecutar no puede depender de conservar un chat o su respaldo.
- Comprobar también las rutas físicas: las escrituras de un proceso MSIX en AppData pueden acabar en `Packages\OpenAI.Codex...\LocalCache` aunque la ruta solicitada parezca compartida. Las herramientas extraídas Windows se alojan en `C:\LGP-Tools`; los builds grandes regenerables, en `C:\LGP-ToolchainBuilds`. Conservar fuentes descargadas y artefactos duraderos en E:. Las recetas no pueden requerir el PowerShell u otro runtime incluido en los datos del agente; usar la instalación compartida cuando necesiten PowerShell 7.
- Mantener los botones de Playground y sus selecciones persistentes. No depender de rutas de un Windows anterior ni de ajustes manuales que sólo duren mientras permanezca abierta una consola. Codex no ejecuta los botones que publican, incorporan cambios o compilan la copia de Work; el usuario controla ese flujo.

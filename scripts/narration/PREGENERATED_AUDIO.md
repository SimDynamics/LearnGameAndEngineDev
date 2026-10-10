# Audio Kokoro pregenerado (sin inferencia al reproducir)

La aplicación intenta obtener cada oración en este orden:

1. Archivo de audio en la caché persistente privada de la app:
   `files/kokoro-narration-cache/<SHA256>.ogg` o `.wav`.
2. Archivo incluido al compilar Android:
   `app/src/main/assets/narration/prerendered/<SHA256>.ogg` o `.wav`.
3. Si no está disponible, Kokoro genera la oración con NNAPI/XNNPACK/CPU
   en el teléfono y guarda el WAV persistentemente para las siguientes visitas.

La clave SHA-256 está calculada por `KokoroNarrationCache.key()` a partir
de la versión del formato, la revisión del modelo, idioma, ID de voz y texto
pronunciado, separado por NUL. Al cambiar la voz, idioma, revisión o texto
se obtiene otra clave, sin riesgo de reproducir un audio desactualizado.

## Empaquetar contenido

El archivo WAV debe generarse con la misma versión de la app y el mismo
motor fonético y modelo. Una vez exportadas las grabaciones desde la caché
privada de una instalación de depuración, desde la raíz del repositorio:

```powershell
python scripts/narration/package_prerendered_opus.py "CARPETA_DE_WAVS_EXPORTADOS"
```

Es necesario disponer de `ffmpeg` con `libopus` en la computadora que
empaqueta. El script produce archivos Opus mono a 40 kb/s (Ogg),
compatible con Android 10 o superior. Gradle incluirá esos archivos
en el siguiente APK sin generar audio en tiempo de ejecución.

**Importante:** el repositorio no incluye las voces WAV/Opus ya generadas.
Incorporar el lector no produce por sí mismo un pack de audio.
La primera visita de una oración no empaquetada aún necesita inferencia
en el dispositivo; las visitas posteriores aprovechan la caché privada.

**Tamaño y distribución:** elegir una sola voz predeterminada por idioma
reduce mucho el tamaño inicial. Para muchos cursos o voces conviene
distribuir packs de audio opcionales, no inflar todos los APK.
La caché privada no elimina audios completos automáticamente. En
**Opciones globales → Lectura en voz alta → después de Instalar o eliminar
voces** aparece un deslizador de almacenamiento. Su techo físico se calcula
como bytes libres actuales + tamaño de audios existentes - 1 GiB. El límite
elegido controla únicamente la pregeneración automática: si el consumo
actual lo supera se suspende, sin borrar ningún archivo existente.
La lectura solicitada por el usuario sigue teniendo prioridad.
Al liberar espacio o aumentar el límite, abrir la app permite reprogramar
el trabajo pendiente. Los modelos Kokoro instalados no se eliminan.

## Arquitectura automática (octubre de 2026)

La cola `KokoroBackgroundNarrationWorker` se crea con WorkManager desde
`MainActivity.onCreate`. El lector recorre las rutas disponibles en este
orden fijo: Programación, Matemáticas/Física; dentro, curso por número,
unidad por número, lección por número, página y oración. La clave única
de voz y contenido evita volver a generar un audio existente.

Cada trabajo tiene un presupuesto aproximado de cuatro minutos. Al terminar
una oración se conserva el WAV permanentemente y se programa el trabajo
siguiente, de acuerdo con las restricciones de Android. Tras interrumpirse
el proceso, se vuelve a recorrer el índice del temario y se reanuda desde
el primer audio faltante, incluso si alguien eliminó archivos puntuales.
**No exige abrir manualmente la pantalla del lector.**

La inferencia automática usa **dos hilos de ONNX/XNNPACK**, sin NNAPI para
no competir con la GPU de los ejercicios gráficos. Android elige los núcleos
físicos; limitar los hilos no equivale a fijar afinidad a dos núcleos concretos.

`NarrationWorkPriority` da preferencia a lectura solicitada, compilación
y ejecución C++, benchmark de GPU y simulación física. Esas tareas cancelan
la inferencia automática en curso; la oración incompleta se regenerará
después y las ya completadas se conservarán.

La cola respeta `batteryNotLow`, se detiene si queda menos de 1 GiB libre
y **no promete ejecución continua ilimitada con la app cerrada**:
WorkManager y JobScheduler pueden posponer o detener trabajos por batería,
restricciones del fabricante o cuotas. El trabajo termina por rebanadas para
que Android pueda reprogramarlo. Mantener un proceso de inferencia activo
indefinidamente con la pantalla apagada requeriría un servicio en primer
plano, notificación persistente y gestión energética adicional.

### Exportar y verificar el primer paquete Opus desde el teléfono

La app de depuración escribe automáticamente el archivo
`files/kokoro-first-unit-manifest.json` utilizando **exactamente el mismo
texto narrado, idioma y voz** del lector. El archivo contiene la lista
deduplicada de grabaciones necesarias para las tres primeras lecciones.

Cuando el teléfono haya terminado de pregenerar esas grabaciones:

```powershell
python scripts/narration/export_first_unit_from_android.py
```

Requisitos: teléfono conectado con ADB, APK de depuración instalado, audio
Kokoro realmente generado y `ffmpeg` con `libopus` accesible en PATH.
El script usa `adb exec-out run-as`; no necesita root ni reinstalar Clang.
Si falta una sola grabación, se detiene con la cantidad de oraciones que
siguen pendientes y **NO presenta un paquete incompleto como válido**.
Si están todas, extrae solo esos WAV, convierte a Opus y comprueba que
los Ogg resultantes son válidos bajo
`app/src/main/assets/narration/prerendered/`. Entonces sí se podrá
recompilar el APK y verificar la voz lista para reproducción inmediata.

### Paquete inicial para primer curso, primera unidad

El paquete de arranque deseado es la unidad
`programming/cpp_fundamentals/intro_programming`, con estas lecciones:

- `what_is_programming`
- `first_contact_cpp`
- `program_structure`

**Estado del paquete:** PENDIENTE de renderizado real. El reproductor puede
usar `app/src/main/assets/narration/prerendered/<hash>.ogg` y ya existe
`package_prerendered_opus.py`, pero por ahora no hay archivos Ogg de esa
unidad incluidos en el repositorio. No atribuir al APK las voces comprimidas
hasta haber generado las grabaciones con el modelo, fonetizador, idioma y
voz exactos de la app, validado todas las oraciones y reconstruido el APK.

Incluir una voz inicial por idioma es razonable; incluir cientos de voces
y todos los cursos inflaría extraordinariamente la descarga. Los demás
audios deben quedarse en almacenamiento privado y eventualmente usar
paquetes opcionales si se vuelven muy grandes.

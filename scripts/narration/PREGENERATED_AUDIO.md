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
La caché privada se limita a 384 MiB y se purga mediante LRU al iniciar
el controlador, sin eliminar los modelos Kokoro instalados.

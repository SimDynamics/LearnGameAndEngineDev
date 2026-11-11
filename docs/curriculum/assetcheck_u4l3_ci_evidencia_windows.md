# AssetCheck U4L3: evidencia curricular de Windows en GitHub Actions

**Alcance:** reproducción *aislada* de la preparación de CMake de U4L3.
No es la instalación del estudiante, no es una primera instalación de Visual
Studio, no es el proyecto independiente de AssetCheck, y no acredita sus dos
clientes ni sus seis suites CTest. Los pendientes correspondientes permanecen
abiertos.

## Origen verificable

- Repositorio: `SimDynamics/LearnGameAndEngineDev`, rama `Codex`.
- Commit examinado: `033eba20445f7a9fdf0ad7bf02703a8b99f4fe89`.
- Ejecución: https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/42963867937
- Resultado observado: los **cinco trabajos terminaron correctamente**.
- Artefacto de capturas: `assetcheck-u4l3-real-console-033eba20445f7a9fdf0ad7bf02703a8b99f4fe89`, ID `13830098521`.
- Enlace directo: https://github.com/SimDynamics/LearnGameAndEngineDev/actions/runs/42963867937/artifacts/13830098521
- Fecha del registro: 11 de noviembre de 2026, UTC.
- Retención configurada: **14 días**. El ZIP y sus PNG deben conservarse fuera
  de GitHub Actions si se requiere acceso después de su caducidad. Esta ficha
  persiste, pero **no contiene las imágenes**.

## Qué se comprobó

El ejecutor fue Windows Server 2025 x64, con Visual Studio 2026 Enterprise,
MSVC 19.51.36260.0, CMake 4.4.5 y Ninja 1.13.2. Las consolas mostraron
`Host=x64` y `Destino=x64`. Se configuró un proyecto temporal usando
`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`, se construyó el
objetivo `preparar_entorno` y se comprobó `AssetCheck: entorno listo`
con código cero. Tras editar el fuente, la ejecución anterior aún imprimía
`entorno listo`; la reconstrucción produjo
`AssetCheck: entorno actualizado`, también con código cero.

Las cuatro capturas son **píxeles de ventanas CMD reales** obtenidos mediante
Win32 `PrintWindow`; no son imágenes compuestas de texto. El script exige
contenido visual claro, comprueba que los archivos son diferentes entre sí,
y guarda tamaños y huellas en `provenance.json`.

**Importante:** la procedencia confirmó que el proceso del ejecutor estaba
**elevado** y el encabezado de las ventanas mostraba «Administrador».
Esto es una propiedad del entorno de GitHub, **no** indica que la práctica
U4L3 requiera ejecutar una consola como administrador. Las instrucciones de
la lección para el estudiante siguen siendo sin elevación.

## Archivos y huellas SHA-256 del artefacto

| Archivo | Bytes | SHA-256 |
|---|---:|---|
| `u4l3_herramientas.png` | 52561 | `d31fa3534ec4061c92728bb1a9825ac4a24d1603938daf7b42f9b7310b59be2b` |
| `u4l3_configurar.png` | 55634 | `4310c229ec73a762c4b56cd17fc2a1937119307ce2ab41cec0d3fbf775de3c16` |
| `u4l3_construir.png` | 30021 | `cc84682eb094459f8f1305a8ad3f7f03ae2524558a375b569f056104257be9bf` |
| `u4l3_reconstruir.png` | 35051 | `2e71040156e3448c4940a4408462ce9073c61855a295de0b0435b03b7ab1782e` |

Las cuatro huellas y longitudes fueron contrastadas con los bytes del ZIP de
la ejecución. El mismo ZIP también contiene los cuatro scripts `.cmd` que
se ejecutaron y el registro `provenance.json`. Estas huellas permiten comprobar
copias locales con `Get-FileHash -Algorithm SHA256` en PowerShell.

## Fuentes de la comprobación

- `scripts/windows/Capture-AssetCheckCMakeU4L3.ps1`: recetas CMD,
  capturas reales, condiciones de ejecución y metadatos.
- `scripts/curriculum/verify_assetcheck_cmake_preparation_windows.py`:
  comprobación independiente de configuración, construcción y reconstrucción.
- `.github/workflows/assetcheck-curricular-oracles.yml`:
  ejecución de Windows, GCC/MSVC curricular y retención de artefactos.
- `app/src/main/assets/content/es/programming/programming_practice_tools/proyecto_assetcheck/lesson_preparar_y_comprobar_cmake_en_windows.json`:
  fuente real del CMakeLists y de las dos variantes de `main.cpp`.

**No cerrar** los pendientes de captura personal ni la integración física
de AssetCheck por la existencia de esta evidencia de CI.

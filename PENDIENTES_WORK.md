# Notas internas de continuidad de Work

Encargos activos sólo en PENDIENTES_RECURSOS_CURRICULARES.md.

Traslado de carpetas (2026-11-10): el código Git reside ahora bajo `E:\Projects`. En Git se corrigieron las referencias de Codex; falta comprobar la sincronización del maestro físico `E:\Projects\LearnGameAndEngineDev-Playground\Instrucciones_maestras_cursosWindows.txt`. El gestor de ramas, los botones personales y su prueba `scripts/windows/Test-BranchButtons.ps1` quedan deliberadamente sin modificar para la revisión manual indicada por el usuario.

Continuar Programación 2 desde U4L3, «Preparar y comprobar CMake en Windows», integrada en `app/src/main/assets/content/es/programming/programming_practice_tools/proyecto_assetcheck/lesson_preparar_y_comprobar_cmake_en_windows.json`. El usuario confirmó el 2026-11-10 la ejecución externa de `build\preparar_entorno.exe` en Windows x64, con stdout `AssetCheck: entorno listo` y retorno `Codigo=0`, tras corregir la ruta a `E:\Projects\LGAEDProjects\02_construccion_depuracion_y_practica_profesional\01_prueba_cmake`. Se identificaron VS Community 2026 18.11.3, MSVC 19.51.36260, SDK 10.0.28000.0, CMake 4.3.1-msvc1 y Ninja 1.13.2. Esto no incluye los logs completos de configuración y construcción, capturas auténticas, una instalación desde cero o una reconstrucción posterior a editar main.cpp. Conservar activa únicamente esa parte pendiente de `P2_PROYECTO_PREPARACION_CMAKE_WINDOWS` y solicitar sus evidencias cuando puedan obtenerse. U4L1–L3 fijan el contrato y enseñan la prueba mínima; U4L4 deberá enseñar y construir gradualmente el lector de un registro del manifiesto, sin anticipar la solución completa. U1L1–L5, U2L1–L4 y U3L1–L10 mantienen la revisión editorial publicada; la conclusión funcional sigue sujeta a las capacidades técnicas de `PENDIENTES_RECURSOS_CURRICULARES.md`. Conservar los veinte elementos históricos del índice, las tres lecciones de U4, las traducciones y todos los estados; Referencias conserva su ID y continúa al final. Programación 1 permanece cerrado y no se abre Programación 3. Leer íntegro `docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt` al reanudar.

## Base conservada de la importación

- Directorio de Work: `E:\Projects\LearnGameAndEngineDev-Work`, rama `main`.
- Origen: `E:\Projects\LearnGraphicsProgramming`, `main` `911118f2a92066569911e6785f4a98660ade0124`. Su historial se conserva en este repositorio local; el proyecto original permanece intacto.
- Importación verificada byte por byte: 1.727 archivos versionados, incluidos los 878 JSON curriculares, pruebas, modelos ingleses y audios Ogg existentes. Se conservaron también los dos archivos locales de `.codex-qa/u4-recorridos-anidados/`, incluido el ejecutable, excluidos de Git. Evidencia local: `.migration-qa/import-verification.json`.
- Registro vigente de pedidos: `PENDIENTES_RECURSOS_CURRICULARES.md`, editado exclusivamente por Work. Los pedidos Windows se trasladaron allí con sus identificadores; Codex informa únicamente en el chat.
- **Estado curricular al importar:** Programación 1 seguía abierto. Se conservan las seis correcciones editoriales, las dos predicciones y el editor de recuperación de frecuencias. Su importación no acredita aceptación Windows ni cierra las comprobaciones anteriores pendientes.
- **Estado de Windows:** base nativa, recorrido U1L2 e importación sintética incorporados. Consultar su aceptación y límites en el registro único. El progreso personal del teléfono permanece intacto y no se ha importado para QA.

## Recursos locales que se conservan en el origen

Las siguientes rutas parten de `E:\Projects\LearnGraphicsProgramming`. Son recursos reutilizables; conservar sus ubicaciones y contrastar su compatibilidad antes de adaptarlos. No publicar snapshots privados ni copiar entornos y cachés completos al nuevo Git.

- `.gradle/cpp-content-qa/` y `.gradle/math-content-qa/`: pruebas y evidencias anteriores. Sus resultados mantienen la plataforma, versión y alcance que acreditaron.
- `build/kokoro/downloads/1939ad2a8e416c0acfeecc08a694d14ef25f2231/`: modelo, voces y léxicos locales. `build/kokoro/audio/`: audios y textos de comprobación. Los recursos ingleses versionados ya están importados bajo `english_misaki/src/main/assets/`.
- `.gradle/clang23-android-arm64/`, `.gradle/cpp-runtime-cache/`, `.gradle/cmake-android-arm64/`, `tools/runtime-spike/` y `tools/wasmer-spike/`: compiladores, descargas y experimentos conservados. No son por sí mismos ejecutables ni una aplicación Windows.
- Submódulo `third_party/wasm-micro-runtime`: versión conservada `25bd7eb63e828e4bd242cc9b38d260b4b31c6605`, origen `https://github.com/wasm-micro-runtime/wasm-micro-runtime.git`. El nuevo checkout conserva su referencia; no se ha inicializado ni se ha copiado el enlace `.git` del checkout anterior.

# Notas internas de continuidad de Work

Encargos activos sólo en PENDIENTES_RECURSOS_CURRICULARES.md.

Traslado de carpetas (2026-11-10): el código Git reside ahora bajo `E:\Projects`. En Git se corrigieron las referencias de Codex; falta comprobar la sincronización del maestro físico `E:\Projects\LearnGameAndEngineDev-Playground\Instrucciones_maestras_cursosWindows.txt`. El gestor de ramas, los botones personales y su prueba `scripts/windows/Test-BranchButtons.ps1` quedan deliberadamente sin modificar para la revisión manual indicada por el usuario.

Continuar Programación 2 desde U4L4, «Leer y validar un registro del manifiesto», integrada en `app/src/main/assets/content/es/programming/programming_practice_tools/proyecto_assetcheck/lesson_leer_y_validar_un_registro.json`. Conservar U4L1–L3: la consola Windows x64 de VS Community 2026 18.11.3 quedó identificada y el usuario ejecutó `preparar_entorno.exe` con `AssetCheck: entorno listo`, `Codigo=0`, en `E:\Projects\LGAEDProjects\02_construccion_depuracion_y_practica_profesional\01_prueba_cmake`. Aún faltan logs completos de las fases, capturas reales y reconstrucción después de editar; no atribuir instalación sobre un equipo vacío. U4L4 enseña únicamente la lectura local de un registro, tiene nueve páginas con actividades compatibles, un editor de conversión decimal y su referencia protegida. Se preparó una biblioteca C++20 con CMake, casos de rechazo y pruebas independientes: **28 casos y seis mutantes comprobados en Linux con GCC 14.2/CMake 3.31.6/Ninja 1.12.1**. El proyecto externo todavía no está implementado ni ejecutado en Windows por Work; su integración pertenece al encargo `P2_PROYECTO_LECTOR_LOCAL_WINDOWS` del registro único. Continuar luego con U4L5 (lote, primera admisión y duplicados), al incorporar cada parte del proyecto en paralelo a la enseñanza; no presentar el código parcial como proyecto final. Las traducciones y veinte elementos históricos previos se conservan; Referencias mantiene su ID y permanece al final. Las capacidades técnicas abiertas de U1–U3 y la validación multicurso siguen en `PENDIENTES_RECURSOS_CURRICULARES.md`. Programación 1 continúa cerrado; no comenzar Programación 3. Leer íntegro `docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt` al reanudar.

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

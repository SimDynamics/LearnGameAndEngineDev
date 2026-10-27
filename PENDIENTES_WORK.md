# Notas internas de continuidad de Work

Uso interno de Work. Este archivo no contiene pedidos activos ni es un canal de encargos para Codex. Consultar `PENDIENTES_RECURSOS_CURRICULARES.md` para las capacidades pendientes y su aceptación. No crear `ACTUALIZACIONES_CODEX.md`.

Continuidad curricular: Programación 1 ES abierto en main 879deda. U11L6 (37 bloques) y U11L7 (42 bloques/19 casos) revisadas con fuentes, énfasis, guías y referencias posteriores. MSVC: L6 cinco ejemplos, 24 fronteras y dos variantes; L7 tres programas/63 ejecuciones, con 19 casos conservados y dos contrastes adicionales que detectan prioridades incorrectas. La ampliación compatible a 21 casos queda especificada en CPP_CASOS_ADICIONALES_CON_PROGRESO; no modificar la lista protegida todavía. Predicciones y pruebas anteriores reutilizadas por hash. QA aislada work-orden-lineas-b3d9a79 cerrada normalmente en U11L6 p9: reintentos, compilador incorrecto/correcto, referencias posteriores y reapertura del acierto comprobados; no finalizada ni aceptada integralmente, escucha pendiente. U11L7 aún requiere aceptación de interfaz. Se conservan distractores/rótulo pendientes U11L5–L7, rechazos editoriales U11L2 p3 b2/U1L2 p4 b3, equivalencias de ordenación y expectativa de native.state_import. Últimos contratos: 54 Python, 105 familias y native.lesson_contract/native.lesson_composition aprobados; validador general conserva sólo esos dos rechazos. Evidencias out/qa/u11-operacion-879deda/ y out/qa/u11-lector-879deda/. Próximo trabajo: aceptación de U11L7 y continuar U11L8 sin saltar las carencias registradas. Releer íntegro docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt.

## Base conservada

- Directorio de Work: `E:\PersonalFiles\Projects\LearnGameAndEngineDev-Work`, rama `main`.
- Origen: `E:\PersonalFiles\Projects\LearnGraphicsProgramming`, `main` `911118f2a92066569911e6785f4a98660ade0124`. Su historial se conserva en este repositorio local; el proyecto original permanece intacto.
- Importación verificada byte por byte: 1.727 archivos versionados, incluidos los 878 JSON curriculares, pruebas, modelos ingleses y audios Ogg existentes. Se conservaron también los dos archivos locales de `.codex-qa/u4-recorridos-anidados/`, incluido el ejecutable, excluidos de Git. Evidencia local: `.migration-qa/import-verification.json`.
- Registro vigente de pedidos: `PENDIENTES_RECURSOS_CURRICULARES.md`, editado exclusivamente por Work. Los pedidos Windows se trasladaron allí con sus identificadores; Codex informa únicamente en el chat.
- **Estado curricular:** Programación 1 sigue abierto. Se conservan las seis correcciones editoriales, las dos predicciones y el editor de recuperación de frecuencias. Su importación no acredita aceptación Windows ni cierra las comprobaciones anteriores pendientes.
- **Estado de Windows:** base nativa, recorrido U1L2 e importación sintética incorporados. Consultar su aceptación y límites en el registro único. El progreso personal del teléfono permanece intacto y no se ha importado para QA.

## Recursos locales que se conservan en el origen

Las siguientes rutas parten de `E:\PersonalFiles\Projects\LearnGraphicsProgramming`. Son recursos reutilizables; conservar sus ubicaciones y contrastar su compatibilidad antes de adaptarlos. No publicar snapshots privados ni copiar entornos y cachés completos al nuevo Git.

- `.gradle/cpp-content-qa/` y `.gradle/math-content-qa/`: pruebas y evidencias anteriores. Sus resultados mantienen la plataforma, versión y alcance que acreditaron.
- `build/kokoro/downloads/1939ad2a8e416c0acfeecc08a694d14ef25f2231/`: modelo, voces y léxicos locales. `build/kokoro/audio/`: audios y textos de comprobación. Los recursos ingleses versionados ya están importados bajo `english_misaki/src/main/assets/`.
- `.gradle/clang23-android-arm64/`, `.gradle/cpp-runtime-cache/`, `.gradle/cmake-android-arm64/`, `tools/runtime-spike/` y `tools/wasmer-spike/`: compiladores, descargas y experimentos conservados. No son por sí mismos ejecutables ni una aplicación Windows.
- Submódulo `third_party/wasm-micro-runtime`: versión conservada `25bd7eb63e828e4bd242cc9b38d260b4b31c6605`, origen `https://github.com/wasm-micro-runtime/wasm-micro-runtime.git`. El nuevo checkout conserva su referencia; no se ha inicializado ni se ha copiado el enlace `.git` del checkout anterior.

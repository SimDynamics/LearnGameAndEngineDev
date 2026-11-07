# Notas internas de continuidad de Work

Uso interno de Work. Los encargos activos y criterios están únicamente en `PENDIENTES_RECURSOS_CURRICULARES.md`; no crear registros paralelos.

Continuar Programación1 en Work/main, sin cerrar el curso ni pasar a P2. Leer íntegro E:\PersonalFiles\Projects\LearnGameAndEngineDev-Playground\Instrucciones_maestras_cursosWindows.txt y espejo Work, ahora SHA837d0f77…: prosa por defecto y retirada de inferencias ambiguas según instrucción del usuario. Guardadas74citasKöppe abreviadas/64textos, marcado completo U2L8 y seis referencias L27; validación152/6141 y173planes sin audio. En PENDIENTES_RECURSOS_CURRICULARES.md quedan46fichas: conservar las45anteriores y la nueva capacidad de capturas. Tres ampliaciones generales pendientes para Codex: inferencias ambiguas de prosa, títulos fuera de bloques y revisión sólo bibliográfica de10explicaciones/11citas protegidas (ubicaciones exactas registradas). Tras integrar soporte, aplicar esas correcciones y seguir preparando el proyecto Windows dentro de P1, antes de su referencia final, conservando IDs. Proyecto01 aún vacío; no anticipar implementación, pasos ni capturas. Usuario revisa imagen/escucha mientras estudia. Sin motor/tests/Codex, commits ni publicaciones.

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

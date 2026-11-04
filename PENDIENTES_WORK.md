# Notas internas de continuidad de Work

Uso interno de Work. Este archivo no contiene pedidos activos ni es un canal de encargos para Codex. Consultar `PENDIENTES_RECURSOS_CURRICULARES.md` para las capacidades pendientes y su aceptación. No crear `ACTUALIZACIONES_CODEX.md`.

Continuidad curricular: «+1» significa continuar. Programación 1 ES en Work/main 6ed8e967, con avances locales sin commits/publicaciones del principal. U11L15 SHA a1082ad6… integra 19/5 informes, segundo inicial de 266 líneas y etiqueta99+2; conserva históricos completos. Validador 152/6110 y cuatro nativas Debug aprobadas; cuatro runs UI 14/19/4/5 rechazan los borradores anteriores y aceptan las referencias ampliadas, sin atribuir construcción independiente. Reapertura conserva programas/logros y matching corregido; QA cerrada normalmente en p11, sin Finalizar/Reiniciar. L16 p3 ya muestra «Lanzar la excepción» sobre el acierto conservado y al reabrir; no se rehízo la respuesta. Evidencias y límites en el registro único, out/qa/u11-historial-pruebas/integration-6ed8e96-20261104/ y out/qa/u11-resto-asociacion/acceptance-6ed8e96-20261104/. Próximo contenido nuevo: U11L17 p1, no U12; U11 tiene 22 lecciones. Permanecen escucha, presentación completa, estados/importación y demás pendientes de Codex; no cerrar lecciones ni curso. QA Debug 39b0596f…; perfil work-u11l14-expanded-20261103. App personal intacta. Proyecto 1 planificado, implementación paralela al llegar a sus secciones, sin adelantar curso 3. Releer íntegro el maestro Windows indicado en AGENTS.

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

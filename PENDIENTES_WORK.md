# Notas internas de continuidad de Work

Uso interno de Work. Este archivo no contiene pedidos activos ni es un canal de encargos para Codex. Consultar `PENDIENTES_RECURSOS_CURRICULARES.md` para las capacidades pendientes y su aceptación. No crear `ACTUALIZACIONES_CODEX.md`.

Continuidad curricular: «+1» significa continuar. Programación 1 ES abierto en Work/main 477bde9; conservar cambios locales, sin commits ni publicaciones. Continuar en U11L14 p1, abierta sin responder; QA work-orden-lineas-b3d9a79 cerrada normalmente, un intento disponible. U11L13 alcanza 10/10 sólo en QA tras p5–p10: cuatro predicciones y selección acertadas, seguimiento trasladado a p6 después del intento, reapertura parcial de p7 y referencias intactas. Programas/catorce informes conservados; reutilizar la evidencia MSVC existente. Usar otra QA aislada si hacen falta rechazos, sin reiniciar progreso. Registro único conserva ampliación de dos contrastes U11L13, pregunta exacta bloqueada p8, parcial U11L12, excepción/diagnóstico, fixtures de retorno U2L3, color/subrayado, narración, estados históricos/importación y demás aceptaciones. No volver a retrasar los dos retornos correctos U2L3 por sus fixtures. Pendiente visual focal de la última precisión de p2 U11L13; no reconstruir contenido ya comprobado. El progreso QA no cierra la lección ni el curso. Releer íntegro el maestro Windows indicado en AGENTS.md.

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

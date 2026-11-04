# Notas internas de continuidad de Work

Uso interno de Work. Este archivo no contiene pedidos activos ni es un canal de encargos para Codex. Consultar `PENDIENTES_RECURSOS_CURRICULARES.md` para las capacidades pendientes y su aceptación. No crear `ACTUALIZACIONES_CODEX.md`.

Continuidad curricular: «+1» significa continuar. Programación 1 ES, Work/main 90e510f, sin commits/publicaciones del principal. U11L17 SHA 33503678…: ocho páginas/32 bloques, v2 desde familia real; 31 bloques originales, seis fuentes completas y fragmento previo intactos. Teoría precisada, siete guías, editor obligatorio p6 b5, seis referencias posteriores y nueva predicción p7 b2 sobre reserva/coherencia (storage 8/0, progressPage page_0006). Validador 152/6111, cuatro nativas Debug y nueve compilaciones/nueve ejecuciones MSVC pasan. Evidencias y límites en el registro único y out/qa/u11-nombres-tokens/. QA existente work-u11l14-expanded-20261103: p1 error/guía/reapertura/reintento correcto; p2 relleno 0/Mesa9 acertado; p3 error/guía/tercero correcto. Última vista p3 con su explicación completa. Intento de Continuar detectó intervención; ventana no disponible después, sin atribuir causa ni cierre normal. Proceso QA 22104 sigue vivo; lectura posterior confirma tres aciertos/página 3/posiciones y checksum válido. No reabrir duplicados: comprobar primero su ventana propia al retomar. Siguiente revisión visual U11L17 p4; no dar por comprobadas p4–p8, el editor ni la nueva recuperación dentro de la app. Escucha, color y estados/importación conservan sus encargos. U11 tiene 22 lecciones; no avanzar ahora a U12 ni curso 3. Debug construido 2ad9b2bd…, sin instalar; perfil personal intacto. Proyectos de portafolio: plan y reglas en AGENTS/maestro, implementación paralela al llegar a sus secciones. Releer íntegro el maestro Windows indicado allí.

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

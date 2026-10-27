# Notas internas de continuidad de Work

Uso interno de Work. Este archivo no contiene pedidos activos ni es un canal de encargos para Codex. Consultar `PENDIENTES_RECURSOS_CURRICULARES.md` para las capacidades pendientes y su aceptación. No crear `ACTUALIZACIONES_CODEX.md`.

Prioridad inmediata: retiro de repeticiones U1L2/L3/L5 integrado contra main 52d057d: seis códigos estáticos y tres bloques introductorios redundantes; todas las actividades, fuentes, respuestas y traducciones conservadas. Validador 152/6110 y 109 familias pasan. Cuatro errores de fixtures heredadas y expectativa nativa antigua localizados en CPP_RETIRO_DE_REPETICIONES_CON_ESTADO. Falta aceptación visual/lectura; no usar GUI mientras Codex mantenga la reserva. El usuario confirmó que el botón Finalizar ya reapareció: no abrir encargo ni censo por ese bug. Evidencia out/qa/u1-referencias-repetidas-52d057d/.

Continuidad curricular: Programación 1 ES abierto en main 1fe0975. U11L8–L11 revisadas con fuentes, respuestas y ocho traducciones conservadas (135 bloques históricos). U11L10/L11 guardadas: MSVC confirma ejemplos/fronteras y variantes; seis predicciones/90 artefactos y baterías previas reutilizados. Ampliaciones de ceros largos y dominio parcial pendientes en CPP_CASOS_ADICIONALES_CON_PROGRESO; dos explicaciones L11 rechazadas exactamente por el validador en CPP_VALIDACION_EDITORIAL_U11L11. 73 Python y 109 familias pasan. QA propia work-orden-lineas-b3d9a79 permanece abierta, sin interacción, en U11L9 p6 (p5 acreditada y restaurada); faltan p6–p9, color, repintado y escucha. U11L7 conserva 3/19 casos. Codex mantiene escritorio exclusivo; no GUI ni tests con foco hasta devolución coordinada. Continuar aceptación pendiente o estudio de U11L12 sin cerrar requisitos. Evidencia out/qa/u11-{buffer,numeros,gramatica}-1fe0975/. Releer íntegro docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt.

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

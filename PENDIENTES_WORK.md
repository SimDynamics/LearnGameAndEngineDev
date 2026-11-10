# Notas internas de continuidad de Work

Encargos activos sólo en PENDIENTES_RECURSOS_CURRICULARES.md.

Continuar Programación 2 con la enseñanza y construcción paralela del proyecto externo previsto en `AGENTS.md`: AssetCheck, nombre todavía provisional. Empezar por concretar el formato del manifiesto, los dominios y criterios de aceptación a partir de lo realmente enseñado, y comprobar la preparación de CMake en Windows. Cada parte se implementa al redactar la sección que enseña a construirla, con comandos completos, capturas reales de los pasos manuales y comprobaciones; no anticipar la solución terminada. U1L1–L5, U2L1–L4 y U3L1–L10 tienen la revisión editorial y el marcado explícito incorporados. El cierre funcional conserva los objetivos abiertos, diagnósticos por canal, criterios y compatibilidad de proyectos CMake, reglas nombradas, recuperación selectiva, aserciones y validación multicurso del registro único `PENDIENTES_RECURSOS_CURRICULARES.md`. El integrador U3L10 dentro de la app no cierra el proyecto externo ni el curso. Conservar los veinte elementos actuales del índice, las traducciones y la distribución de libros; añadir la enseñanza externa mediante las transiciones compatibles necesarias. Programación 1 permanece cerrada y no se abre Programación 3. Leer íntegro `docs/curriculum/Instrucciones_maestras_cursos_corregidas.txt` al reanudar.

## Base conservada de la importación

- Directorio de Work: `E:\PersonalFiles\Projects\LearnGameAndEngineDev-Work`, rama `main`.
- Origen: `E:\PersonalFiles\Projects\LearnGraphicsProgramming`, `main` `911118f2a92066569911e6785f4a98660ade0124`. Su historial se conserva en este repositorio local; el proyecto original permanece intacto.
- Importación verificada byte por byte: 1.727 archivos versionados, incluidos los 878 JSON curriculares, pruebas, modelos ingleses y audios Ogg existentes. Se conservaron también los dos archivos locales de `.codex-qa/u4-recorridos-anidados/`, incluido el ejecutable, excluidos de Git. Evidencia local: `.migration-qa/import-verification.json`.
- Registro vigente de pedidos: `PENDIENTES_RECURSOS_CURRICULARES.md`, editado exclusivamente por Work. Los pedidos Windows se trasladaron allí con sus identificadores; Codex informa únicamente en el chat.
- **Estado curricular al importar:** Programación 1 seguía abierto. Se conservan las seis correcciones editoriales, las dos predicciones y el editor de recuperación de frecuencias. Su importación no acredita aceptación Windows ni cierra las comprobaciones anteriores pendientes.
- **Estado de Windows:** base nativa, recorrido U1L2 e importación sintética incorporados. Consultar su aceptación y límites en el registro único. El progreso personal del teléfono permanece intacto y no se ha importado para QA.

## Recursos locales que se conservan en el origen

Las siguientes rutas parten de `E:\PersonalFiles\Projects\LearnGraphicsProgramming`. Son recursos reutilizables; conservar sus ubicaciones y contrastar su compatibilidad antes de adaptarlos. No publicar snapshots privados ni copiar entornos y cachés completos al nuevo Git.

- `.gradle/cpp-content-qa/` y `.gradle/math-content-qa/`: pruebas y evidencias anteriores. Sus resultados mantienen la plataforma, versión y alcance que acreditaron.
- `build/kokoro/downloads/1939ad2a8e416c0acfeecc08a694d14ef25f2231/`: modelo, voces y léxicos locales. `build/kokoro/audio/`: audios y textos de comprobación. Los recursos ingleses versionados ya están importados bajo `english_misaki/src/main/assets/`.
- `.gradle/clang23-android-arm64/`, `.gradle/cpp-runtime-cache/`, `.gradle/cmake-android-arm64/`, `tools/runtime-spike/` y `tools/wasmer-spike/`: compiladores, descargas y experimentos conservados. No son por sí mismos ejecutables ni una aplicación Windows.
- Submódulo `third_party/wasm-micro-runtime`: versión conservada `25bd7eb63e828e4bd242cc9b38d260b4b31c6605`, origen `https://github.com/wasm-micro-runtime/wasm-micro-runtime.git`. El nuevo checkout conserva su referencia; no se ha inicializado ni se ha copiado el enlace `.git` del checkout anterior.

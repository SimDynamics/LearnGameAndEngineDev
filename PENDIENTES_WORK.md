# Notas internas de continuidad de Work

Uso interno de Work. Los encargos activos y criterios están únicamente en `PENDIENTES_RECURSOS_CURRICULARES.md`; no crear registros paralelos.

Continuar Programación 1 en Work/main. Leer íntegro E:\PersonalFiles\Projects\LearnGameAndEngineDev-Playground\Instrucciones_maestras_cursosWindows.txt; copia sincronizada SHA 2c91c180…. HEAD 0bd62e3. U12L20: marcado explícito completo compatible, 124 campos/302 fragmentos, SHA e5fdf072…/49 aristas/59 identidades; fuentes, objetivos y respuestas intactos. Validador 152/6141 y preparación de los 124 campos; 15 segmentos de guías anticipados, sin escucha. Release 4b5d1f22… compilado sobre 0a5794d e instalado: 109 comprobaciones. QA work-u12l20-20261106-6f40ea5e: primer editor rechaza vacío/parcial con guías completas y acepta batería de 844 bytes exactos; nueve runs acumulados. Reapertura conserva rechazo y guía, pero tras acierto pierde explanation aunque conserva logro/borrador: registrado en ficha del compilador. Segundo editor conserva objetivo original; añadir guías sigue bloqueado por guarda legacy en main 0bd62e3. La reproducción QA sigue bloqueada por la ruta física del WAV. QA cerrada normalmente en p6; el usuario cerró su app personal para compilar, datos intactos. Quedan aceptación de color/lector/escucha, estados históricos/importación/Reiniciar, trece contratos U6/L26 y documentación. No cerrar L20/curso ni adelantar proyectos. Conservar las 45 fichas, 371 objetivos obligatorios, pausa pre-v2 L4 y marcado U2L6/U2L7 de la tanda anterior. Sin modificar motor, pruebas ni Codex; sin commits ni publicaciones.

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

# Notas internas de continuidad de Work

Leer íntegro el maestro Windows físico E:\Projects\LearnGameAndEngineDev-Playground\Instrucciones_maestras_cursosWindows.txt y AGENTS.md. Work local en main, base comprobada 375fad94; sin commits ni publicaciones del principal. La antigua copia del maestro fue eliminada: no recrearla ni seguir sus rutas antiguas.

Avance real: Programación 1 ya contiene U12L27–L36 y referencias L37; su único proyecto es E:\Projects\LGAEDProjects\01_programar_desde_cero_con_cpp, repositorio del mismo nombre. Conservar código, historial y capturas de InspectionLog. Programación 2 llega a U4L7; la integración física Windows de AssetCheck y sus capturas siguen pendientes en el registro único. No iniciar Programación 3 ni rehacer el proyecto 1.

Continuar la revisión editorial de legibilidad desde U6L16. U6L15 quedó con código espaciado y ensayos opcionales separados; los casos históricos se conservan. Revisar después las baterías agrupadas de U6L19–L20 registradas en PENDIENTES_RECURSOS_CURRICULARES.md. Presentar términos antes de usarlos, escribir para estudiantes y separar datos en listas sin viñetas. Sólo el primer caso correcto de cada editor es obligatorio. Conservar los avances posteriores mientras se revisan.

Antes de desarrollar cursos posteriores, contrastar el catálogo antiguo de 44 cursos con la planificación vigente de 136 del Excel autorizado, conservando IDs e historial. Tras 1–3, la base pertinente es 4 y después estructuras/algoritmos con sus prerrequisitos; no prometer preparación Amazon por números de curso ni una fecha sin evidencia de resolución autónoma.

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

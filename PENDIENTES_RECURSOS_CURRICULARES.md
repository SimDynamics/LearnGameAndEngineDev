# Recursos y capacidades curriculares

Work mantiene este registro y retira cada ficha o subencargo al confirmar que su integración está completa. Work conserva en [PENDIENTES_WORK.md](PENDIENTES_WORK.md) las incorporaciones de capacidades ya implementadas que esperan la revisión curricular de su lección. Los encargos a Codex se registran cuando se identifica una capacidad pendiente o un fallo técnico reproducible del motor o sus herramientas de autoría.

La revisión visual y la escucha rutinarias corresponden al usuario durante el estudio y no mantienen abiertos encargos técnicos concluidos. Sus anomalías concretas sí se registran y atienden. Las capturas reales necesarias para enseñar proyectos siguen siendo responsabilidad de Work.

## IMÁGENES POR GENERAR E INTEGRAR

Sin pendientes.

## CAPACIDADES POR IMPLEMENTAR E INTEGRAR

### Revisar pistas de error antes de publicar su primera versión

- **Encargo a Codex.** El validador debe reconocer dos ediciones sucesivas de un puzle cuando Work incorpora una pista y luego mejora su redacción antes de publicar. Debe comprobar de qué texto parte cada cambio y a qué opción pertenece cada pista.
- **Caso reproducido en Programación 1, U3L5.** La publicación inicial carece de pistas de error. La primera edición local las añade a once puzles y la segunda precisa su redacción. El validador acepta cada paso por separado. Al comparar directamente la publicación inicial con la segunda edición, da el error «An unchanged review hint cannot acquire invented editorial provenance». La reproducción mínima de un solo puzle conserva las tres versiones en [la prueba de autoría](C:/LGAED-QA/work-u3l5-seis-paginas-6b53ektp/repro_guia_revisada.py).
- **Aceptación.** Comprobar el cambio directo y los dos pasos sucesivos con una pista individual y con pistas por opción. Verificar que el lector presenta y narra la pista vigente, que conserva las respuestas y que el validador sigue rechazando una pista atribuida a otra opción o a una versión previa incorrecta.

### Revisión editorial de varias opciones de un mismo puzle

- **Encargo a Codex.** Permitir que Work precise varias opciones de una actividad de selección en una edición y que pueda volver a editarlas en publicaciones posteriores. La actividad conserva el significado evaluado, su identidad, el orden de las alternativas y el índice correcto.
- **Reproducción en Windows.** En P1 U3L4, página 5, bloque `block_0004_0002` de `lesson_alternativas_y_fronteras.json`, aplicar `revise_choice_editorial` a dos índices distintos. La primera revisión pasa. La segunda da «A reviewed choice cannot be revised twice under one historical archive». Aplicar las tres por actualización directa tampoco supera la transición de publicación. Las tres redacciones y sus marcas están preparadas en `C:/LGAED-QA/work-u3l4-opciones-completas-669ulrsg/opciones.json`.
- **Caso de aceptación.** La misma cantidad entra en A (`cantidad <= 4`) y en B (`cantidad < 4`). Los tres botones han de admitir sus respectivas predicciones de A y B. Conservar las cantidades en el orden 5, 4, 3 y la respuesta correcta en la segunda posición. Verificar la transición completa desde la publicación auténtica, una edición posterior del mismo puzle y la carga de la lección y de sus metadatos de progreso. En perfiles de prueba, conservar la selección, calificación e intentos anteriores al reabrir o importar y comprobar el acceso directo a la página siguiente. El texto, los resaltados y la narración visibles corresponden a cada redacción vigente.
- **Compatibilidad con las guías de cada opción.** En P1 U3L5, el bloque `block_0003_0007` permite incorporar primero sus guías de error y después precisar una opción en dos pasos verificados por separado. Al cargar la edición que reúne ambos pasos, el validador rechaza la transición desde la publicación original con «A new choice revision must preserve its complete previous published block». Comprobar esa transición también en sentido inverso de autoría, con la guía y el marcado de cada índice en la versión que les corresponda. Reproducción mínima en `C:/LGAED-QA/work-p1-u3l5-logica-ykh2sz7m/repro_guia_opcion.py`. U3L5 puede incorporar sus guías con las opciones vigentes.
- **Integración de Work.** Aplicar las tres etiquetas preparadas en los controles de U3L4, eliminar de la consigna la lista provisional que muestra las predicciones y hacer la segunda lectura de la página.

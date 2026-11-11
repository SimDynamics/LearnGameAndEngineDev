# Compatibilidad de formato físico del código curricular

## Propósito y alcance

Este contrato permite editar espacios, sangrías y **saltos de línea reales** del
código curricular español sin transformar la edición en una actividad nueva.
La fuente que recibe el estudiante, copia o compila será la versión corregida.
No se trata de un espaciado visual superpuesto al editor.

El validador autoriza cambios de formato en los campos de código existentes
de los ejemplos, predicciones, `fill_code`, `order_code`, `drag_fill_code`,
`inline_fill_code` y `cpp_compiler`, incluidos `projectFiles` C++ y CMake.
No se inventan campos de respuestas, casos, identificadores ni ejercicios.
Los archivos de proyecto de tipo desconocido son inmutables bajo este contrato.

## Autoría de Work: conservar el origen publicado

Para una lección española con `stateLayout` v2, Work edita únicamente los
campos de fuente reales cuyo formato necesita corregir. Luego ejecuta la
transición curricular usando el **documento anterior publicado como base**,
mediante el flujo habitual `prepare_v2` y validación de transición.

La transición incorpora a `stateLayout.sourceFormatRevisions` un registro por
actividad modificada, de este formato:

```json
"sourceFormatRevisions": [
  {
    "id": "block_0002_0003",
    "block": {
      "type": "code",
      "code": "int main(){return 0;}"
    }
  }
]
```

El `block` debe ser el **bloque anterior completo y exacto**, no un ejemplo
sintético ni un fragmento copiado parcialmente. El ejemplo precedente sólo
ilustra la forma de la propiedad, no sirve como snapshot para una lección real.
El identificador corresponde al `stateLayout.current` de la lección.
Las revisiones son de una sola asignación por identidad y se conservan en
transiciones posteriores. Las pruebas editoriales v2 siguen siendo obligatorias
para toda modificación real del bloque.

No modificar el `origin`, `family`, `additions`, direcciones históricas,
`correctOrder`, respuestas, casos de `outputCheck`, fuentes iniciales de
otros ejercicios, mapas de progreso ni archivos de traducción. No regenerar
huellas históricas para hacerlas coincidir artificialmente con un texto nuevo.

## Protección semántica

La comparación exige conservar los elementos léxicos de la fuente, incluidos
identificadores, valores, operadores, cadenas, comentarios y directivas. Los
saltos físicos dentro de cadenas crudas, literales y argumentos entre corchetes
CMake tienen significado y no pueden retocarse. Se protegen las continuaciones
con barra invertida y salto físico. No se permite usar este mecanismo para
cambiar fórmulas, reparaciones, dependencias, resultados de programa ni
expectativas exactas de stdout/stderr.

El comparador es **conservador**: si no demuestra que los límites léxicos se
conservan, rechaza la revisión. No sustituye compilar, ejecutar y comprobar
los programas. Los ejemplos que requieran una nueva distribución semántica
de comentarios o macros necesitan revisión específica, no forzar la huella.

En `order_code` se conserva la identidad de cada fragmento y la secuencia
de índices, aunque su representación textual pueda mejorar. Los separadores
`orderVisualSeparators` siguen siendo decorativos; no sustituyen las líneas
físicas que Work haya incorporado a la fuente.

## Estado del estudiante

La aplicación conserva las claves históricas de actividad y borrador. El
código actual se presenta o compila con su espaciado corregido; las huellas de
objetivos y de orden consultan la instantánea publicada anterior cuando existe
una revisión de formato autenticada. Se conservan las respuestas e intentos
ganados. El editor no sobrescribe automáticamente borradores del estudiante.
Los ejecutables anteriores pueden volver a construirse con el código nuevo.

**Verificación previa a usarlo de forma general:** pasar las pruebas de
`test_code_format_compat.py`, los contratos existentes de ambos cursos,
`native.code_format_compat`, las regresiones de lectura/estado Windows y
probar al menos una lección real con edición, reapertura y recuperación de
borrador y logro. La aceptación visual y auditiva permanece con el usuario.

## Separación de responsabilidades

Codex implementa la compatibilidad y verifica los contratos en `Codex`.
Work revisa la legibilidad pedagógica de todos los cursos y publica únicamente
sus modificaciones curriculares en `main`. El usuario controla la
sincronización de ramas. Sólo Work modifica
`PENDIENTES_RECURSOS_CURRICULARES.md` y elimina lo efectivamente concluido.

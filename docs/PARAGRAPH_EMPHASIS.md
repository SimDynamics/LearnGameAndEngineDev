# Negrita y cursiva parciales opcionales en párrafos y notas

Los bloques `paragraph` y `note` pueden declarar `emphasis`, un array opcional de fragmentos **exactos del campo `text` existente**. No se interpreta Markdown ni HTML y no se modifica, duplica ni normaliza el texto visible o narrado.

```json
{
  "type": "paragraph",
  "text": "Una biblioteca reúne recursos preparados. Comprueba el resultado.",
  "emphasis": [
    { "text": "biblioteca" },
    { "text": "Comprueba el resultado" }
  ]
}
```

Cada fragmento debe aparecer una sola vez. Si se repite, `occurrence` selecciona explícitamente su aparición, contando desde 1: `{ "text": "valor", "occurrence": 2 }`. Se cuentan coincidencias exactas, incluidas las superpuestas. Las decoraciones ausentes, ambiguas, obsoletas o inválidas se ignoran; el párrafo completo permanece disponible. Los offsets internos conservan los caracteres UTF-16 originales y nunca cortan un par sustituto.

Cada entrada admite además `style`, opcional, con uno de estos valores exactos:

- `"bold"`: negrita, también el comportamiento de las entradas existentes que omiten `style`.
- `"italic"`: cursiva.
- `"bold_italic"`: negrita y cursiva.

```json
{
  "type": "paragraph",
  "text": "Consulta Working Draft: Programming Languages — C++; conserva su título.",
  "emphasis": [
    { "text": "Working Draft: Programming Languages — C++", "style": "italic" },
    { "text": "conserva su título", "style": "bold_italic" }
  ]
}
```

El renderer añade únicamente `StyleSpan` nativos a los fragmentos designados y conserva los demás spans, colores de sintaxis, enlaces, saltos, citas y rangos de lectura. Se pueden superponer entradas de negrita y cursiva: sus estilos se combinan en el fragmento común. Un estilo desconocido, nulo o mal formado sólo descarta esa decoración. Al reaplicar o retirar `emphasis`, se reemplazan exclusivamente los spans propios de este decorador; los estilos ajenos permanecen intactos.

En las notas, la aplicación conserva su encabezado localizado existente («Nota», «Note» o «Remarque»). La búsqueda de fragmentos y `occurrence` sólo considera el campo `text` original, excluyendo ese encabezado. El renderer traslada los rangos al cuerpo visible sin cambiar el encabezado, su narración ni los offsets originales de lectura.

El ancho, los márgenes y la separación del componente siguen los parámetros existentes; como cualquier estilo nativo, los glifos designados pueden redistribuirse dentro del mismo ancho. El sombreado de palabras conserva los estilos y los offsets originales al avanzar o retroceder. No se adivina qué idea es central ni qué texto es un título bibliográfico: Work selecciona los fragmentos correspondientes a cada referencia. No se cambia código, respuesta, identidad, progreso, narración o caché. El énfasis no cambia las voces ni introduce marcas en el audio.

Los JSON existentes no requieren migración y no adquieren énfasis automático. Este contrato se aplica a los campos `text` de bloques `paragraph` y `note`; no declara campos nuevos en puzzles ni retroalimentaciones. Work integrará los fragmentos que correspondan y reaceptará en Honor los pasajes curriculares registrados; las pruebas técnicas usan fixtures aisladas, sin editar lecciones.

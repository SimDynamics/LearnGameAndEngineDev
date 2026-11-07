# Imágenes curriculares locales en Windows

El bloque `image` presenta una imagen local como parte de una lección. Es un
recurso de explicación, no una actividad evaluable: verlo no acredita una
respuesta, una ejecución, una acción manual ni un proyecto.

## Contrato de autoría

Ejemplo de estructura; la ruta no activa ni proporciona una captura real:

```json
{
  "type": "image",
  "imageId": "installation_01",
  "src": "images/programming1/installation_01.png",
  "alt": "Descripción de los elementos que debe identificar el estudiante.",
  "caption": "Paso y resultado observado, con su explicación pertinente."
}
```

`imageId` es obligatorio y contiene entre 1 y 128 caracteres de `[a-z0-9_]`.
El nombre del archivo, sin extensión, coincide exactamente con ese identificador.
Para reutilizar un mismo recurso, se puede usar el mismo identificador y ruta en
varios bloques; el identificador de la imagen no reemplaza la identidad estable
del bloque de `stateLayout`.
El campo genérico `id` del bloque es opcional; si aparece, requiere texto no
vacío, Unicode válido y sin NUL. No sustituye al `imageId` obligatorio.

`src` es una ruta relativa a `app/src/main/assets/content/`, empieza por
`images/`, utiliza `/` y admite hasta 1024 bytes UTF-8. Las extensiones
admitidas son `.png`, `.jpg` y `.jpeg`,
sin distinguir mayúsculas de la extensión. Se rechazan rutas absolutas, URI,
red, `\`, segmentos vacíos, `.` o `..`, consultas, fragmentos, flujos ADS,
caracteres de control y caracteres reservados de Windows. Tampoco se admiten
segmentos terminados en punto o espacio ni nombres de dispositivos Windows.
La resolución física debe permanecer dentro de la raíz de contenido y apuntar
a un archivo regular; una redirección fuera de esa raíz no es un recurso local
admitido.

`alt` es obligatorio y no puede estar vacío ni contener sólo espacios.
`caption` es opcional; si aparece, también requiere texto no vacío. Ambos
campos deben contener Unicode válido sin NUL. No se admiten otros campos, ni
dimensiones del autor, HTML, una URL de descarga o una respuesta oculta.

Los límites del recurso son 64 MiB de archivo, 32768 píxeles por dimensión y
40 millones de píxeles en total. Las dimensiones deben ser positivas. La
extensión tiene que coincidir con la firma real PNG o JPEG. El validador de
autoría comprueba la ruta, la existencia, el tipo y tamaño del archivo y su
cabecera/contenedor; el decodificador Windows comprueba además que puede
decodificar sus datos. Estas comprobaciones no certifican la legibilidad ni el
contenido pedagógico de una imagen.

## Presentación, accesibilidad y lectura

La vista conserva la relación de aspecto obtenida de la imagen y ajusta su
presentación al espacio disponible. El detalle permanece dentro de la ventana
principal, con ajuste al espacio, escala del 100 %, zoom y desplazamiento. No
se abre una ventana independiente del sistema ni una aplicación externa.

El texto alternativo describe la información necesaria para comprender la
imagen; el pie sitúa la imagen en la explicación. Ambos son textos públicos
para accesibilidad y narración. El lector no extrae texto de los píxeles ni
narra `src` o `imageId`, y la imagen no sustituye las instrucciones escritas de
una acción. Las marcas opcionales de `textSemantics` pueden vincular `/alt` y
`/caption` a sus fuentes exactas mediante el mecanismo vigente de autoría; sin
marcas se tratan como prosa. No se admiten marcas sobre la ruta o identificador.

Si el recurso desaparece, no se puede leer o no se puede decodificar, la
aplicación informa del fallo de imagen y conserva sus textos públicos. No
simula una captura, descarga otra imagen ni atribuye al estudiante haber
realizado el paso.

## Conservación e integración

La imagen no crea una respuesta, un borrador del compilador ni un logro. El
payload estable conserva `type`, `imageId` y `src`; `alt` y `caption` son texto
editorial, como la redacción de un párrafo. Los cambios editoriales siguen
requiriendo la procedencia y las revisiones válidas del contrato existente.
Una imagen puede ser presentación dependiente en `referenceAfter`; no se
convierte por ello en un ejercicio. Una retirada debe conservar su bloque
original completo mediante el mecanismo de archivo vigente.

Work incorpora cada recurso real junto con su bloque y sus identidades
estables. Las capturas de instalación, configuración, depuración o publicación
se obtienen durante los pasos reales y deben proteger credenciales y datos
personales. Un ejemplo JSON o una imagen sintética de pruebas no demuestra
que esos pasos se hayan realizado. No activar rutas de recursos todavía
inexistentes.

La validación de Programación comprueba los recursos presentes mediante
`scripts/curriculum/image_resources.py`; sus regresiones aisladas se ejecutan
con `python -B -m unittest scripts/curriculum/test_image_resources.py`. Esas
pruebas utilizan archivos sintéticos temporales y no alteran imágenes activas,
progreso ni perfiles personales. La aceptación curricular de una captura
requiere su incorporación real y la comprobación de su explicación y detalle.

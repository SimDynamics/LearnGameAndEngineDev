# ServerTec: etapa 02, prueba local reproducible

## Alcance de esta entrega

Todavía no se conecta la app Android ni se abre el router. Esta entrega añade una autoprueba ejecutable en la PC, sin iniciar una sesión Google ni crear un estado persistente. No es una implementación de descubrimiento de IP dinámica. No añade servicios externos, cuotas de hosting, dependencias nuevas ni GitHub Actions.

`autoprueba.py` crea certificados y SQLite en una carpeta temporal separada, inicia un proceso HTTPS en **127.0.0.1 y un puerto temporal**, usa credenciales aleatorias exclusivas de esa prueba y comprueba el contrato de corazones/progreso. Después detiene únicamente el proceso que creó y elimina ese estado temporal. No instala autoridades certificadoras de Windows ni utiliza los corazones/progreso de la app. En Windows restringe los permisos de su carpeta temporal antes de generar las claves.

Se verifica HTTPS real (certificado y nombre), rechazo sin autenticación, descuento repetido sin doble cobro, separación de rutas, rechazo del mismo UUID en otra ruta, unidades/cursos sin lecciones, rechazo de `premium` enviado por el cliente, rechazo de CA/nombre incorrectos y persistencia después de cerrar el proceso. No demuestra que Google real, Honor, LDPlayer o Telmex ya estén conectados.

## Corrección del resumen IPv6

La versión anterior ocultaba la dirección cuando la longitud era /64, pero conservaba una dirección completa cuando Windows informaba /128. Ahora el resumen utiliza una proyección limitada: IPv6 con un máximo de /64 y IPv4 con un máximo de /24. `BitsAsignados` mantiene la longitud original para no confundir una referencia recortada con la máscara real del adaptador. Las direcciones completas y los identificadores ADB permanecen solo en el informe local privado, no en el bloque para compartir.

`Red-Resumen.ps1` contiene la transformación independiente. `Probar-Resumen-Red.ps1` añade nueve comprobaciones con direcciones de documentación, incluido /128, enlaces locales con zona y IPv4 /32. Se ejecutan en Windows desde `Preparar` o `Pruebas`, sin Pester y sin consultar adaptadores. Estas nueve comprobaciones PowerShell todavía no han sido ejecutadas en Windows por el asistente.

## Actualización sin borrar datos

El paquete de entrega incluye `release-manifest.json`, generado junto al ZIP, con hashes del contenido nuevo y del código anterior conocido. El actualizador `actualizar.py` no necesita FastAPI ni instalar paquetes: usa la biblioteca estándar de Python.

Ejecutarlo indicando el ZIP, su SHA-256 publicado por separado y el destino. Antes de reemplazar un archivo revisa todo el paquete, sus rutas y cada archivo existente. Acepta el contenido anterior conocido o el nuevo; si encuentra modificaciones personales, aborta antes de sobrescribir. Conserva `.venv`, archivos desconocidos y estado/certificados fuera del código. No borra documentos, backups o carpetas de usuario. Hay reversión de los archivos sustituidos si una operación falla durante el proceso; no equivale a una transacción resistente a cortes de luz. Detener el servidor local antes de actualizar.

El manifiesto protege la integridad al combinarlo con el hash del ZIP recibido por un canal confiable. No es una firma digital independiente ni un mecanismo de actualizaciones remotas del servidor.

## Pasos en tu Windows, después de actualizar

PowerShell normal:

```powershell
Set-Location -LiteralPath 'E:\PersonalFiles\Projects\ServerTec'
powershell -NoProfile -ExecutionPolicy Bypass -File .\ServerTec.ps1 -Accion Preparar
```

Si esa operación termina correctamente:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ServerTec.ps1 -Accion AutoPrueba
```

`Preparar` comprueba Python 3.13 o posterior, conserva el entorno existente si ya está creado, instala las dependencias declaradas y ejecuta `pip check`, pruebas Python y las comprobaciones del resumen PowerShell. Las instalaciones de paquetes sí requieren acceso al registro de paquetes; la autoprueba no llama a Internet ni a Google.

`AutoPrueba` termina por sí sola. Debe mostrar `Resultado: 9/9` entre los marcadores `AUTOPRUEBA PARA COMPARTIR` y `FIN AUTOPRUEBA`. No requiere `Inicializar`, no pide tu frase de CA y no deja un servidor permanente en segundo plano.

Para revisar el resumen de red corregido, puede ejecutarse `-Accion Diagnostico`. No hace falta volver a reiniciar el módem ni enviar el informe privado completo.

**No ejecutar botones de Compilar/Instalar Main-Honor o codex-chat-LDPlayer para esta etapa**: no cambia ningún archivo Android ni existe todavía un cliente de esta API dentro del APK.

## Resultado y límites

Ver `RESULTADOS.md` para las ejecuciones realizadas. No hay prueba de capacidad para 10 000 usuarios, auditoría externa, conexión pública o garantía de continuidad 24/7. El descubrimiento público tras cambios de IP y la configuración contractual/técnica de la línea siguen siendo asuntos separados.

Referencias de implementación consultadas:

* https://learn.microsoft.com/en-us/powershell/module/nettcpip/get-netipaddress
* https://docs.python.org/3.13/library/ssl.html
* https://docs.python.org/3.13/library/subprocess.html
* https://uvicorn.dev/settings/

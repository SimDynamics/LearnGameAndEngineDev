# Validación de la primera entrega

2026-10-17. Ejecución local en entorno aislado Linux, Python 3.13.5.

Comando: `python -m pytest -q -p no:cacheprovider`.

Resultado inicial: **24 pruebas aprobadas**. Incluye una prueba de HTTPS con sockets reales, Uvicorn real, cadena de certificados privada y SQLite real. Todas las claves utilizadas en las pruebas se generan temporalmente; ninguna se entrega como credencial.

Se comprobó: recuperación horaria, límite de saldo, reloj atrasado, persistencia al reabrir DB, separación de cuentas/rutas, concurrencia con el mismo UUID, reutilización incorrecta de UUID, recibos de saldo cero, copia de WAL mediante backup, límites de petición, rechazo de datos extra (Premium/saldo falsos), SQL malicioso, autenticación obligatoria, ausencia de docs abiertas, credencial de pruebas restringida a loopback, no confiar en X-Forwarded-For, verificación RS256 real con claves temporales, rechazo de emisor/audiencia/proveedor/caducidad incorrectos y algoritmo none, rechazo TLS de nombre incorrecto y CA no confiable, ausencia del token en logs.

No comprobado aún: scripts PowerShell en Windows, acceso desde Honor/LDPlayer, Telmex entrante, Google/Firebase reales, Google Play Billing, disponibilidad 24/7, renovación de direcciones/certificados y capacidad a gran escala. No se realizó una auditoría externa de seguridad ni de vulnerabilidades de dependencias.

## Etapa 02: privacidad del diagnóstico y autoprueba local

2026-10-17. Ejecución actual en Linux con Python 3.13.5, usando las mismas versiones directas de dependencias de la primera entrega.

* `python -m pytest -q -p no:cacheprovider`: **40 pruebas aprobadas** (las 24 anteriores más 16 nuevas). Última ejecución: 7,22 s.
* `python autoprueba.py`: **9/9 comprobaciones aprobadas** con sockets HTTPS, certificados, proceso Uvicorn y SQLite reales. Proceso hijo detenido y estado temporal retirado al terminar.
* `python -m compileall -q . -x '/\.git/'`: sin errores de sintaxis Python.

Las nuevas pruebas cubren: autoprueba aislada sin abrir datos reales; rechazo de CA/nombre incorrectos; contexto TLS que mantiene verificación obligatoria y no exporta claves mediante SSLKEYLOGFILE; cierre exclusivo del proceso creado; limpieza ante fallos; actualización con hashes; conservación de archivos ajenos, .venv y datos; rechazo de cambios locales; rutas relativas/absolutas maliciosas; archivos privados en paquetes; enlaces simbólicos; reversión tras un fallo de escritura.

También se añadieron **9 comprobaciones PowerShell** del resumen recortado, sin dependencia de Pester. **No se ejecutaron aquí**, porque este entorno no incluye PowerShell. Se ejecutarán en Windows mediante `Preparar` o `Pruebas`. No deben confundirse con las 9 comprobaciones HTTPS de la autoprueba, que sí se ejecutaron.

El motor `servertec.py`, el aprovisionamiento `manage.py`, las dependencias, Android y el contrato de corazones/progreso no cambiaron en esta etapa. Sigue pendiente comprobar Windows 11/PowerShell 5.1 reales, Honor/LDPlayer, IPv4 WAN y conexiones entrantes, Google real y el descubrimiento de direcciones públicas cambiantes. No se abrieron puertos, no se instaló ningún certificado en el sistema y no se contrató ningún servicio.

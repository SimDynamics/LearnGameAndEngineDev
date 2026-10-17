# Validación de la primera entrega

2026-10-17. Ejecución local en entorno aislado Linux, Python 3.13.5.

Comando: `python -m pytest -q -p no:cacheprovider`.

Resultado inicial: **24 pruebas aprobadas**. Incluye una prueba de HTTPS con sockets reales, Uvicorn real, cadena de certificados privada y SQLite real. Todas las claves utilizadas en las pruebas se generan temporalmente; ninguna se entrega como credencial.

Se comprobó: recuperación horaria, límite de saldo, reloj atrasado, persistencia al reabrir DB, separación de cuentas/rutas, concurrencia con el mismo UUID, reutilización incorrecta de UUID, recibos de saldo cero, copia de WAL mediante backup, límites de petición, rechazo de datos extra (Premium/saldo falsos), SQL malicioso, autenticación obligatoria, ausencia de docs abiertas, credencial de pruebas restringida a loopback, no confiar en X-Forwarded-For, verificación RS256 real con claves temporales, rechazo de emisor/audiencia/proveedor/caducidad incorrectos y algoritmo none, rechazo TLS de nombre incorrecto y CA no confiable, ausencia del token en logs.

No comprobado aún: scripts PowerShell en Windows, acceso desde Honor/LDPlayer, Telmex entrante, Google/Firebase reales, Google Play Billing, disponibilidad 24/7, renovación de direcciones/certificados y capacidad a gran escala. No se realizó una auditoría externa de seguridad ni de vulnerabilidades de dependencias.

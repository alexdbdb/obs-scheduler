# Compilación Windows del 7 de septiembre de 2026

Instalador generado desde el código simplificado de esta carpeta:
`artifacts/Broadcast-Scheduler-0.1.0-Setup.exe` (2.901.296 bytes).

SHA-256: `3264DF8962C57E626E22B60949C58688A83823F7E2CC6C1A7F8F49F073FD03B4`.

Compilado con MSVC 19.44, Qt 6.11.1 y las cabeceras de OBS 32.2.2.
Las bibliotecas de importación se generaron desde las DLL del OBS instalado.
No se recompiló ni reinstaló OBS. El instalador se generó con Inno Setup 6.7.3.

Verificaciones:

- QtTest: 20 comprobaciones aprobadas, ninguna fallida ni omitida.
- API HTTP real: 5 pruebas aprobadas, incluyendo horarios locales.
- Carga de la DLL empaquetada contra las bibliotecas del OBS instalado: correcta.
- Los tests utilizan bases de datos temporales.

La comprobación de carga no abre el panel ni graba vídeo. Esta nueva versión
todavía no se ha instalado ni probado en una sesión interactiva de OBS.
Registro: `artifacts/build-installed-obs.log`.

Para repetir: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-installed-obs.ps1`.

# Auditoría de Broadcast Scheduler — 6 de septiembre de 2026

> Nota histórica: esta auditoría describe el commit indicado, anterior al refactor. Consulta [validación actual](validation.md) para el estado posterior.

El plugin carga y puede iniciar y detener una grabación local, pero tiene un bloqueo reproducible al crear eventos y varios defectos de fiabilidad que desaconsejan usarlo todavía para grabaciones desatendidas.

## Entorno y pruebas realizadas

- Código revisado: commit `7c85c3f` (`Fix ISO datetime parsing across Qt platforms`). No se ha modificado el código del plugin ni instalado otra versión.
- OBS instalado: 32.2.2, Windows de 64 bits; Qt compilado y ejecutado: 6.11.1.
- DLL instalada: SHA-256 `D0227BFC2878DBBC612EB582E2C1192D3D81DBB769DFA16DAA90D928C075B221`. Coincide exactamente con el paquete local `artifacts/ci-windows-datetime-fix/broadcast-scheduler-0.1.0-windows-x64.zip`.
- Apertura de OBS, carga del módulo y apertura del panel: correctas.
- Pulsar «Añadir evento»: error reproducido antes de que aparezca el formulario.
- Inicio y parada mediante los botones del plugin: correctos, con confirmaciones de OBS en su registro. Grabación desde 22:07:09 hasta 22:07:52, 2558 fotogramas de salida y archivo `recording-test.mkv` de 32.055.239 bytes. Se comprobó su existencia y tamaño; no se hizo una validación audiovisual ni con ffprobe.
- Consulta de SQLite en modo de solo lectura: cero eventos y cero ejecuciones; las operaciones manuales aparecen en logs, pero no en la tabla de historial de ejecuciones.
- Inspección visual de etiquetas, confirmación de parada, código de planificación, almacenamiento, adaptador OBS, proveedores, API y pruebas existentes.

El planificador ya estaba pausado y la API deshabilitada antes de las pruebas. Se mantienen así. No se inició ninguna emisión ni se conectó Google. La grabación de prueba queda conservada y detenida; OBS queda abierto con el panel visible.

No se ejecutó la batería C++/HTTP en este portátil: los ejecutables existentes en `build` y las cachés de compilación corresponden a Linux; no se encontraron CMake/CTest ni Docker en PATH, y WSL informa de que no está instalado. Los resultados antiguos de `docs/validation.md` no se presentan como pruebas de esta sesión. El flujo de alta manual bloqueado impidió completar la prueba ordinaria de grabación programada a través de ese formulario.

## Hallazgos priorizados

### 1. Alta: no se pueden crear eventos manuales — reproducido en OBS

Ubicación: `src/ui.cpp:407`, especialmente las lecturas de las líneas 418–433.

Reproducción: abrir Broadcast Scheduler y pulsar «Añadir evento». Aparece `Datetime must be ISO 8601 with Z or an explicit offset`. El registro de esta sesión lo confirma a las 22:06:20.176.

La función recibe un `QJsonObject e` mutable y inicialmente vacío. Consultar `e["title"]`, `e["description"]` y `e["timezone"]` inserta claves nulas. Cuando después se evalúa `e.isEmpty()`, ya es falso: se intenta interpretar `e["start"]`, que está vacío. Por tanto, el parche del analizador ISO no resuelve la causa del alta. Este comportamiento de `operator[]` está documentado por [Qt](https://doc.qt.io/qt-6/qjsonobject.html#operator-5b-5d).

Corrección propuesta: determinar si es nuevo antes de cualquier acceso mutable y utilizar `value()` para las lecturas. Aplicar el mismo criterio a la asignación del ID al guardar. Añadir una prueba real del formulario vacío, guardado, reapertura y duplicación: las pruebas actuales de serialización no pasan por esta función.

### 2. Alta: desactivar o eliminar un evento activo puede dejar su grabación sin parada — detectado en código

Ubicación: `src/model.cpp:148`, `src/store.cpp:162`, `src/scheduler.cpp:1` y gestión de propietarios en `src/obs-adapter.cpp`.

Escenario: A inicia una grabación; antes de terminar se desactiva/elimina A, o desaparece al sincronizar su calendario. Su acción de parada desaparece de la planificación, pero el adaptador conserva a A como propietario. No existe reconciliación entre cambios del calendario y propietarios activos.

Con eventos solapados puede ser peor: B termina, encuentra al propietario huérfano A y omite la parada porque cree que otro evento aún necesita la salida.

Corrección propuesta: definir y aplicar una política explícita para eventos activos al editarlos o retirarlos, conservando o resolviendo sus paradas y liberando propietarios obsoletos. Verificar A/B solapados y eliminación mediante sincronización. No se provocó este escenario sobre la configuración real.

### 3. Alta: una excepción puede parar definitivamente el motor sin reflejarlo en su estado — detectado en código

Ubicación: `src/runtime.cpp:77`, `src/runtime.cpp:87`, `src/runtime.cpp:188`, `src/runtime.cpp:275` y `src/ui.cpp:142`.

Una excepción durante el tick, la expansión o la sincronización ejecuta `timer->stop()`. Sin embargo, el indicador de actividad y la API consultan el ajuste `enabled`, que puede continuar a true. El botón Pausar/Reanudar cambia ese ajuste, pero no vuelve a arrancar el temporizador.

Una incidencia de almacenamiento, por ejemplo, puede dejar el programa sin ejecutar más acciones aunque parezca habilitado, incluso tras pulsar Reanudar. Se emite un aviso inicial; el defecto es que el estado persistente no refleja la avería y el control de reanudación no la recupera.

Corrección propuesta: representar por separado habilitación y salud del motor, publicar el fallo y permitir una recuperación explícita que reinicie el temporizador tras comprobar sus dependencias.

### 4. Alta: perder la confirmación de arranque puede dejar una salida tardía sin propietario — detectado en código

Ubicación: `src/obs-adapter.cpp:144` y `src/obs-adapter.cpp:25`.

A los 30 segundos sin confirmación se marca el inicio como fallido y se borran `pending`, `pendingKey` y los eventos en espera. No se cancela ni se reconcilia la solicitud que OBS puede seguir procesando. Si OBS confirma el arranque después, el callback no asigna propietario; la parada programada posterior la considera salida del operador y la omite.

Corrección propuesta: mantener una identidad verificable de solicitudes vencidas y reconciliar la respuesta tardía con el estado real, sin apropiarse de un arranque manual distinto. Probar con un adaptador que retrase la confirmación más de 30 segundos; no se simuló una red lenta ni una emisión externa en el portátil.

### 5. Media: el historial no resuelve correctamente varias operaciones pendientes — detectado en código

Ubicación: `src/store.cpp:74`, `src/obs-adapter.cpp:25`, `src/obs-adapter.cpp:186` y `src/obs-adapter.cpp:238`.

Al reiniciar solo se convierten en `indeterminate` las filas `claimed`. Si el despacho llegó a persistir `requested` y OBS salió antes de confirmarlo, esa fila permanece solicitada indefinidamente, aunque la documentación describe una recuperación a estado indeterminado.

Además, pausa/reanudación de grabación y operaciones de replay devuelven `requested`, pero el callback solo procesa inicio/parada de grabación o emisión. No existe confirmación final para aquellas operaciones. Las paradas de grabación/emisión tampoco tienen un plazo de confirmación equivalente al de inicio.

Corrección propuesta: completar cada operación con la señal o comprobación apropiada y clasificar como indeterminadas al reiniciar las solicitudes que ya no pueden verificarse. La prueba actual `indeterminate()` solo cubre `claimed`.

### 6. Media: el orden de acciones simultáneas depende de UUID — detectado en código

Ubicación: `src/model.cpp:154`; clave de ejecución en `src/model.hpp`.

Con igual instante se ordena por `event.id/action.id`, no por el orden de las filas de la plantilla ni por una prioridad funcional. Al generar IDs aleatorios, poner «cambiar escena» antes de «iniciar grabación» a la misma hora no garantiza ese orden. Entre eventos consecutivos, el orden relativo de parar/iniciar también puede variar según sus IDs.

Corrección propuesta: definir prioridad entre acciones y conservar un índice de secuencia, con reglas explícitas para solapamientos. La prueba `simultaneousAndClock()` cuenta llamadas, pero no verifica el orden ni el comportamiento de OBS.

### 7. Media: traducción incompleta y claves internas en la interfaz — reproducido en OBS

Ubicación: `src/ui.cpp:26`, `src/ui.cpp:184`, `src/ui.cpp:211` y `src/ui.hpp:10`.

Se observaron botones en español junto a pestañas `Upcoming`, `Calendar`, `History`, `Logs` y un aviso de parada titulado `StopRecording` cuyo mensaje era `ConfirmStop`. Los archivos instalados sí contienen esas traducciones.

La causa en el código es la colisión entre la función libre `bs::tr`, que usa los textos de OBS, y `Dock::tr` generado por `Q_OBJECT`: dentro de métodos de Dock, las llamadas no cualificadas resuelven la traducción de Qt, mientras que las funciones auxiliares libres usan la de OBS.

Corrección propuesta: renombrar el traductor de OBS o cualificarlo consistentemente y comprobar toda la interfaz en español e inglés, especialmente avisos de seguridad.

### 8. Media: guardar una configuración inválida puede apagar una API que funcionaba — detectado en código

Ubicación: `src/api.cpp:18` y `src/runtime.cpp:275`.

`Api::configure()` cierra y elimina el listener actual antes de validar la configuración nueva o comprobar el puerto. Si el nuevo puerto está ocupado o la dirección no es válida, lanza una excepción. `settings.save` no persiste los cambios, pero la API anterior ya se ha detenido.

Corrección propuesta: validar antes de cerrar y restaurar el listener anterior si la nueva vinculación falla. Probar cambio a puerto ocupado, host inválido y conservación de acceso con la configuración anterior. No se habilitó la API del usuario para esta prueba.

## Problemas del entorno que no deben atribuirse a este plugin

OBS mostró un aviso por `obs-ptz` y `source-dock`. Los registros también contienen dispositivos de audio no disponibles y fuentes de otros plugins que no se pudieron crear. Broadcast Scheduler sí aparece cargado y su arranque/parada manual funcionó. Esos avisos requieren una revisión separada del perfil y de sus plugins.

## Orden recomendado de corrección y validación

Primero resolver el formulario y la salud/reanudación del motor. Después asegurar propietarios, paradas y confirmaciones tardías. A continuación cerrar correctamente el historial, definir el orden simultáneo y corregir traducción y reconfiguración de API.

La aceptación debería incluir una grabación programada real con archivo validado, solapamientos A/B, retirada de evento activo, fallo y recuperación del temporizador, confirmación tardía, reinicio con `requested`, pausa/replay y orden de acciones simultáneas. OAuth, emisión y suspensión/reanudación del portátil siguen sin validarse en esta sesión.

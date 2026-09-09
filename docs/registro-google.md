# Registrar Google para Broadcast Scheduler

Esta configuración se hace una sola vez por el responsable del plugin. Quien instale la edición preparada solo tendrá que pulsar **Conectar cuenta de Google**, autorizar el acceso en su navegador y seleccionar calendarios. No necesitará crear un proyecto ni introducir credenciales.

El código está preparado, pero falta tu registro real en Google. Una compilación sin ese registro muestra un aviso y deshabilita el botón. Las conexiones configuradas con versiones anteriores se conservan.

## 1. Crear el proyecto y activar Calendar

Entra en [Google Cloud Console](https://console.cloud.google.com/) con la cuenta que administrará la aplicación. Crea un proyecto, por ejemplo **Broadcast Scheduler**. Con ese proyecto seleccionado, abre **APIs y servicios → Biblioteca**, busca **Google Calendar API** y pulsa **Habilitar**. No necesitas activar Gmail, Drive ni crear una cuenta de servicio. [Preparación de aplicaciones de escritorio](https://developers.google.com/identity/protocols/oauth2/native-app).

## 2. Configurar la presentación y el público

Abre **Google Auth Platform**. Si aparece **Comenzar / Get started**, completa el asistente. En **Branding / Información de la marca**, indica:

- Nombre de aplicación: **Broadcast Scheduler**.
- Correo de soporte: una dirección que atiendas.
- Correo de contacto del desarrollador: una dirección que revises.

En **Audience / Público**, elige **External / Externo** para admitir cuentas personales de Gmail y cuentas de otras organizaciones. **Internal / Interno** sirve para uso limitado a tu propia organización Google Workspace. Para empezar, mantén el estado **Testing / Pruebas** y añade tu dirección de Google como usuario de prueba. Los nombres de los apartados pueden variar con el idioma de la consola. [Configuración oficial de consentimiento](https://developers.google.com/workspace/guides/configure-oauth-consent).

## 3. Declarar los permisos de lectura

En **Data Access / Acceso a los datos → Add or remove scopes / Añadir o quitar permisos**, añade exactamente:

```text
https://www.googleapis.com/auth/calendar.calendarlist.readonly
https://www.googleapis.com/auth/calendar.events.readonly
```

El primero permite enumerar los calendarios de la cuenta; el segundo, leer sus eventos. Son los dos permisos que solicita ahora el plugin. No solicita escribir, eliminar eventos ni leer el correo. La selección dentro del plugin decide qué calendarios se sincronizan; el permiso de Google puede abarcar más calendarios que los seleccionados. [Permisos de Google Calendar](https://developers.google.com/workspace/calendar/api/auth).

## 4. Crear el cliente y descargar el JSON

En **Clients / Clientes → Create client / Crear cliente**:

1. Tipo: **Desktop app / Aplicación de escritorio**.
2. Nombre interno: **Broadcast Scheduler Windows**.
3. Crea el cliente y descarga su archivo JSON.

No elijas Aplicación web ni UWP. No hace falta registrar orígenes JavaScript. El plugin abre temporalmente una dirección local `http://127.0.0.1:PUERTO/oauth/callback`; el puerto lo asigna Windows y no tienes que fijarlo en la consola. [Creación de credenciales](https://developers.google.com/workspace/guides/create-credentials), [retorno local de escritorio](https://developers.google.com/identity/protocols/oauth2/native-app).

## 5. Incorporar el registro al instalador

Guarda el JSON descargado en esta carpeta del proyecto:

```text
.\.deps\google-client.json
```

No hace falta abrirlo ni copiar sus campos a mano. Desde la raíz del proyecto ejecuta:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-installed-obs.ps1 -GoogleClientFile ".\.deps\google-client.json"
```

El script comprueba que el JSON corresponde a un cliente de escritorio, incorpora su identidad al plugin, compila, ejecuta las pruebas y genera:

```text
artifacts\Broadcast-Scheduler-0.1.0-Setup.exe
```

Ese es el instalador que distribuyes. No distribuyas los tokens personales de tu cuenta ni los archivos de configuración de OBS. El JSON queda fuera de Git; los datos del cliente de escritorio quedan incorporados en la DLL y no deben considerarse un secreto que la aplicación pueda ocultar. Los tokens de cada usuario se guardan por separado usando el almacenamiento protegido del sistema.

## 6. Hacer la primera conexión

Cierra OBS, instala la nueva compilación y vuelve a abrirlo. En **Ajustes → Google**, pulsa **Conectar cuenta de Google**. Elige una cuenta incluida entre los usuarios de prueba y concede ambos permisos. Vuelve a OBS: se abrirá la selección de calendarios. Marca los que quieras utilizar y guarda. Para una prueba controlada, utiliza un calendario dedicado con un evento corto próximo y verifica inicio, parada e historial.

Si cancelas en Google, podrás repetir el proceso. El botón **Cambiar cuenta** desconecta la cuenta anterior y desactiva sus calendarios antes de abrir el navegador. **Desconectar** elimina el token local y solicita su revocación a Google.

## 7. Antes de distribuirlo o usarlo de forma permanente

En modo **Pruebas**, las autorizaciones de este tipo caducan a los siete días y solo pueden conectarse los usuarios de prueba registrados (hasta 100). No es el modo adecuado para dejar grabaciones automáticas funcionando indefinidamente. [Límites del modo de pruebas](https://support.google.com/cloud/answer/15549945?hl=en).

Para distribución pública, prepara una página de la aplicación, una política de privacidad que describa el acceso de lectura y el almacenamiento local, y los datos de marca/dominio que solicite Google. Cambia el público a **In production / En producción** y tramita la verificación que corresponda a los permisos. Google puede solicitar una demostración en vídeo y una justificación del uso de cada permiso. Publicar el proyecto no equivale por sí solo a obtener la verificación. [Verificación de permisos sensibles](https://developers.google.com/identity/protocols/oauth2/production-readiness/sensitive-scope-verification).

## Si aparece un error

- **Esta edición aún no tiene configurada la conexión:** falta recompilar pasando el JSON de escritorio.
- **Acceso bloqueado en pruebas:** comprueba que la cuenta esté incluida en Público → Usuarios de prueba; una organización también puede restringir aplicaciones externas.
- **redirect_uri_mismatch:** revisa que el cliente descargado sea de escritorio y no web.
- **Faltan permisos:** repite la conexión y acepta la lectura de calendarios y eventos.
- **La conexión deja de funcionar después de una semana:** revisa si el proyecto sigue en Pruebas.

La compilación y las pruebas locales no sustituyen la prueba con tu cuenta real: esa parte se podrá completar cuando registres el cliente.

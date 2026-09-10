# Registrar Google para Broadcast Scheduler

[English](google-setup.md)

Esta configuración la realiza cada usuario en su propia cuenta de Google Cloud. El plugin no incluye un cliente OAuth del desarrollador ni queda asociado al proyecto de Google del repositorio. Se introducen el **Client ID** y el **client secret** de una aplicación de escritorio. El secreto no se publica ni se guarda en la base de datos: el plugin lo cifra mediante el almacenamiento protegido del sistema operativo.

## 1. Crear el proyecto y activar Calendar

Entra en [Google Cloud Console](https://console.cloud.google.com/) con la cuenta que administrará la aplicación. Crea un proyecto, por ejemplo **Broadcast Scheduler**. Con ese proyecto seleccionado, abre **APIs y servicios → Biblioteca**, busca **Google Calendar API** y pulsa **Habilitar**. No necesitas activar Gmail, Drive ni crear una cuenta de servicio. [Preparación de aplicaciones de escritorio](https://developers.google.com/identity/protocols/oauth2/native-app).

## 2. Configurar la presentación y el público

Abre **Google Auth Platform**. Si aparece **Comenzar / Get started**, completa el asistente. En **Branding / Información de la marca**, indica:

- Nombre de aplicación: **Broadcast Scheduler**.
- Correo de soporte: una dirección que atiendas.
- Correo de contacto del desarrollador: una dirección que revises.

En [**Audience / Público**](https://console.cloud.google.com/auth/audience), elige **External / Externo** para admitir cuentas personales de Gmail y cuentas de otras organizaciones. **Internal / Interno** sirve para uso limitado a tu propia organización Google Workspace. Para empezar, puedes mantener el estado **Testing / Pruebas**, pero entonces es obligatorio abrir **Test users / Usuarios de prueba → Add users / Añadir usuarios**, escribir la dirección exacta de Google con la que iniciarás sesión y guardar. Asegúrate antes de que la consola tenga seleccionado el mismo proyecto en el que crearás el Client ID. Los nombres de los apartados pueden variar con el idioma de la consola. [Gestión oficial del público y los usuarios de prueba](https://support.google.com/cloud/answer/15549945?hl=es).

## 3. Declarar los permisos de lectura

En **Data Access / Acceso a los datos → Add or remove scopes / Añadir o quitar permisos**, añade exactamente:

```text
https://www.googleapis.com/auth/calendar.calendarlist.readonly
https://www.googleapis.com/auth/calendar.events.readonly
```

El primero permite enumerar los calendarios de la cuenta; el segundo, leer sus eventos. Son los dos permisos que solicita ahora el plugin. No solicita escribir, eliminar eventos ni leer el correo. La selección dentro del plugin decide qué calendarios se sincronizan; el permiso de Google puede abarcar más calendarios que los seleccionados. [Permisos de Google Calendar](https://developers.google.com/workspace/calendar/api/auth).

## 4. Crear el cliente y copiar su ID

En **Clients / Clientes → Create client / Crear cliente**:

1. Tipo: **Desktop app / Aplicación de escritorio**.
2. Nombre interno: **Broadcast Scheduler Windows**.
3. Crea el cliente y copia los valores **Client ID**, que termina en `.apps.googleusercontent.com`, y **Client secret**.

No elijas Aplicación web ni UWP. No hace falta registrar orígenes JavaScript. El plugin abre temporalmente una dirección local `http://127.0.0.1:PUERTO/oauth/callback`; el puerto lo asigna Windows y no tienes que fijarlo en la consola. [Creación de credenciales](https://developers.google.com/workspace/guides/create-credentials), [retorno local de escritorio](https://developers.google.com/identity/protocols/oauth2/native-app).

## 5. Configurar Broadcast Scheduler

Abre OBS y entra en **Broadcast Scheduler → Ajustes → Google**. Pega el Client ID y el client secret en sus respectivos campos, guarda y pulsa **Conectar cuenta de Google**.

El ID se guarda en la base de datos local del plugin y no es una contraseña. El client secret y los tokens de la cuenta se guardan por separado mediante el almacenamiento protegido del sistema. El campo del secreto aparecerá vacío al volver a abrir los ajustes: déjalo así para conservar el valor cifrado existente. Cambiar las credenciales desconecta la cuenta anterior y desactiva sus calendarios para impedir que se reutilice un token con otro proyecto.

## 6. Hacer la primera conexión

Elige una cuenta incluida entre los usuarios de prueba y concede ambos permisos. Vuelve a OBS: se abrirá la selección de calendarios. Marca los que quieras utilizar y guarda. Para una prueba controlada, utiliza un calendario dedicado con un evento corto próximo y verifica inicio, parada e historial.

Si cancelas en Google, podrás repetir el proceso. El botón **Cambiar cuenta** desconecta la cuenta anterior y desactiva sus calendarios antes de abrir el navegador. **Desconectar** elimina el token local y solicita su revocación a Google.

## 7. Antes de distribuirlo o usarlo de forma permanente

En modo **Pruebas**, las autorizaciones de este tipo caducan a los siete días y solo pueden conectarse los usuarios de prueba registrados (hasta 100). No es el modo adecuado para dejar grabaciones automáticas funcionando indefinidamente. [Límites del modo de pruebas](https://support.google.com/cloud/answer/15549945?hl=en).

Para evitar que una conexión personal caduque cada siete días, cambia tu proyecto a **In production / En producción**. Si decides compartir el mismo Client ID con terceros, tú pasarías a ser el responsable de esa aplicación y podrías necesitar marca, política de privacidad y verificación. El diseño recomendado es que cada usuario conserve su propio Client ID. [Verificación de permisos sensibles](https://developers.google.com/identity/protocols/oauth2/production-readiness/sensitive-scope-verification).

## Si aparece un error

- **Falta configurar Google:** pega en Ajustes el Client ID y el client secret de un cliente OAuth de tipo Aplicación de escritorio.
- **Error 403 `access_denied` / “la app se está probando”:** selecciona en Google Cloud el proyecto que creó el Client ID pegado en OBS. Abre [Público](https://console.cloud.google.com/auth/audience), entra en **Usuarios de prueba → Añadir usuarios**, añade la cuenta exacta que eliges en la pantalla de acceso y guarda. Después vuelve a pulsar **Conectar cuenta de Google** en OBS. Si ya aparece en la lista, una organización de Google Workspace también puede estar bloqueando aplicaciones externas.
- **Error 403 al cargar calendarios:** habilita [Google Calendar API](https://console.cloud.google.com/apis/library/calendar-json.googleapis.com) después de seleccionar el mismo proyecto que creó el Client ID. Espera unos minutos tras habilitarla y vuelve a sincronizar. El mensaje del plugin incluye el motivo exacto devuelto por Google para distinguir esta situación de un límite de cuota o una política de Workspace.
- **redirect_uri_mismatch:** revisa que el cliente descargado sea de escritorio y no web.
- **Faltan permisos:** repite la conexión y acepta la lectura de calendarios y eventos.
- **La conexión deja de funcionar después de una semana:** revisa si el proyecto sigue en Pruebas.

La compilación y las pruebas locales no sustituyen la prueba con tu cuenta real: esa parte se podrá completar cuando registres el cliente.

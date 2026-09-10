; OBS's per-plugin layout. No files are written to the OBS application directory.
#define ProductName "Broadcast Scheduler"
#ifndef ProductVersion
  #define ProductVersion "0.2.1"
#endif
#ifndef StageDir
  #define StageDir "..\artifacts\windows-0.2.1"
#endif

[Setup]
; Separate identity prevents upgrades from reusing the legacy OBS directory.
#ifdef PackageTest
AppId={{7CFEE71A-235A-460F-9AB2-DAEDBB54805A}
#else
AppId={{46A7B887-BA15-4527-AD73-164C050DCC92}
#endif
AppName={#ProductName}
AppVersion={#ProductVersion}
AppVerName={#ProductName} {#ProductVersion}
AppPublisher=alexdbdb and contributors
AppPublisherURL=https://github.com/alexdbdb/obs-scheduler
DefaultDirName={commonappdata}\obs-studio\plugins\broadcast-scheduler
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#ifdef PackageTest
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif
OutputDir=..\artifacts
#ifdef PackageTest
OutputBaseFilename=Broadcast-Scheduler-{#ProductVersion}-InstallerTest
#else
OutputBaseFilename=Broadcast-Scheduler-{#ProductVersion}-Setup
#endif
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\licenses\GPL-3.0.txt
Uninstallable=yes
UninstallFilesDir={app}\uninstall
CloseApplications=yes
RestartApplications=no
ChangesEnvironment=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Messages]
english.WelcomeLabel2=This wizard installs Broadcast Scheduler for OBS Studio 32.2.2 (64-bit).%n%nClose OBS before continuing. If upgrading from 0.1.0, uninstall the old plugin first. Your database and settings are retained.
spanish.WelcomeLabel2=Este asistente instala Broadcast Scheduler para OBS Studio 32.2.2 (64 bits).%n%nCierra OBS antes de continuar. Si actualizas desde 0.1.0, desinstala primero el plugin anterior. Se conservan la base de datos y los ajustes.

[CustomMessages]
english.LegacyInstall=Broadcast Scheduler 0.1.0 is still installed. Close OBS and uninstall the old plugin from Windows Settings before installing this version. Your scheduler database and settings are retained.
spanish.LegacyInstall=Broadcast Scheduler 0.1.0 sigue instalado. Cierra OBS y desinstala el plugin anterior desde Configuración de Windows antes de instalar esta versión. Se conservan la base de datos y los ajustes.

[Files]
Source: "{#StageDir}\broadcast-scheduler\bin\64bit\*.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#StageDir}\broadcast-scheduler\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
function InitializeSetup(): Boolean;
var
  LegacyKey: string;
begin
#ifdef PackageTest
  Result := True;
#else
  LegacyKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{B7B6C6A8-EC02-4F9C-BAC6-7E50170B5B18}_is1';
  Result := not (RegKeyExists(HKLM64, LegacyKey) or RegKeyExists(HKCU, LegacyKey));
  if not Result then
    MsgBox(ExpandConstant('{cm:LegacyInstall}'), mbError, MB_OK);
#endif
end;

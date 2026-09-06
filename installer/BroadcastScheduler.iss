; Broadcast Scheduler Windows installer.
; The plugin is installed into an existing OBS Studio 32.x installation.

#define ProductName "Broadcast Scheduler"
#define ProductVersion "0.1.0"
#define ProductPublisher "AUTHOR_NAME"
#define StageDir "..\artifacts\windows"

[Setup]
AppId={{B7B6C6A8-EC02-4F9C-BAC6-7E50170B5B18}
AppName={#ProductName}
AppVersion={#ProductVersion}
AppVerName={#ProductName} {#ProductVersion}
AppPublisher={#ProductPublisher}
AppPublisherURL=https://github.com/alexdbdb/obs-scheduler
DefaultDirName={code:GetDefaultOBSPath}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\artifacts
OutputBaseFilename=Broadcast-Scheduler-{#ProductVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
Uninstallable=yes
UninstallFilesDir={app}\uninstall\Broadcast Scheduler
CloseApplications=yes
RestartApplications=no
ChangesEnvironment=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Messages]
WelcomeLabel2=This wizard installs Broadcast Scheduler into an existing OBS Studio installation.%n%nClose OBS Studio before continuing. Your scheduler database and settings are kept when the plugin is uninstalled.

[Files]
Source: "{#StageDir}\obs-plugins\64bit\broadcast-scheduler.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#StageDir}\data\obs-plugins\broadcast-scheduler\*"; DestDir: "{app}\data\obs-plugins\broadcast-scheduler"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\bin\64bit\Qt6HttpServer.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#StageDir}\bin\64bit\Qt6WebSockets.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion

[UninstallDelete]
Type: dirifempty; Name: "{app}\uninstall"

[Code]
function IsOBSInstall(const Path: string): Boolean;
begin
  Result := FileExists(AddBackslash(Path) + 'bin\64bit\obs64.exe') or
            FileExists(AddBackslash(Path) + 'obs64.exe');
end;

function RegistryOBSPath(RootKey: Integer; const KeyName: string): string;
begin
  Result := '';
  RegQueryStringValue(RootKey, 'SOFTWARE\OBS Studio', 'InstallPath', Result);
  if (Result = '') then
    RegQueryStringValue(RootKey, KeyName, 'InstallPath', Result);
  if not IsOBSInstall(Result) then
    Result := '';
end;

function GetDefaultOBSPath(Param: string): string;
var
  Candidate: string;
begin
  Candidate := RegistryOBSPath(HKLM64, 'SOFTWARE\OBS Studio');
  if Candidate <> '' then begin
    Result := Candidate;
    exit;
  end;

  Candidate := RegistryOBSPath(HKCU, 'SOFTWARE\OBS Studio');
  if Candidate <> '' then begin
    Result := Candidate;
    exit;
  end;

  Candidate := ExpandConstant('{autopf}\obs-studio');
  if IsOBSInstall(Candidate) then begin
    Result := Candidate;
    exit;
  end;

  Candidate := ExpandConstant('{localappdata}\Programs\obs-studio');
  if IsOBSInstall(Candidate) then begin
    Result := Candidate;
    exit;
  end;

  Result := ExpandConstant('{autopf}\obs-studio');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    if not IsOBSInstall(ExpandConstant('{app}')) then begin
      MsgBox('The selected folder does not contain a 64-bit OBS Studio installation.' + #13#10 +
             'Select the folder that contains the bin and obs-plugins folders.',
             mbError, MB_OK);
      Result := False;
    end;
  end;
end;

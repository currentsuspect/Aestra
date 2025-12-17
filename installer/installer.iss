; =========================================================
;  NOMAD DAW – Windows Installer
;  Author: Dylan Makori / Nomad Studios
;  Date: 2025-10-30
;  Free distribution installer – no paid features used
; =========================================================

#define MyAppName        "NOMAD DAW"
#define MyAppVersion     "1.1.0"
#define MyAppPublisher   "Nomad Studios"
#define MyAppURL         "https://nomad-daw.com"
#define MyAppExeName     "NomadDAW.exe"

[Setup]
; ---- Identity ----
AppId={{A1B2C3D4-E5F6-4765-8B7C-8B7C8B7C8B7C}}
AppName={#MyAppName}
AppVerName={#MyAppName} {#MyAppVersion}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/support
AppUpdatesURL={#MyAppURL}/updates

; ---- Directories ----
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=dist\installers
OutputBaseFilename=NOMAD-DAW-Setup
DisableProgramGroupPage=yes

; ---- UI / Compression ----
WizardStyle=modern
SetupIconFile=assets\icon.ico
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
PrivilegesRequired=lowest   ; per-user install possible
LicenseFile=LICENSE

; ---- Languages ----
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

; ---- Tasks ----
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

; ---- Files ----
[Files]
Source: "build\Release\NomadDAW.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "assets\*"; DestDir: "{app}\assets"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
; Add optional data folders below
; Source: "samples\*"; DestDir: "{app}\samples"; Flags: recursesubdirs createallsubdirs

; ---- Icons ----
[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

; ---- Uninstall cleanup ----
[UninstallDelete]
Type: filesandordirs; Name: "{app}"

; ---- Run after install ----
[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName,'&','&&')}}"; Flags: nowait postinstall skipifsilent

; =========================================================
;  Optional Pascal code
; =========================================================
[Code]
function InitializeSetup(): Boolean;
begin
  if not IsWin64 then
  begin
    MsgBox('Nomad DAW requires a 64-bit version of Windows.', mbError, MB_OK);
    Result := False;
  end
  else
    Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    MsgBox('Thank you for installing Nomad DAW!'#13#13 +
           'You can start creating right away.', mbInformation, MB_OK);
end;
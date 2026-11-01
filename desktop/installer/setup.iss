; Inno Setup script for the FM Player Analyzer desktop app.
; Built by scripts\package.ps1, which stages the app (exe + Qt DLLs via
; windeployqt) into installer\staging\ before invoking ISCC on this file.

#define MyAppName "FM Player Analyzer"
#ifndef MyAppVersion
  #define MyAppVersion "2.0.1"
#endif
#define MyAppExeName "fmplayeranalyzer.exe"

[Setup]
; AppId is a STABLE identity — never change it, or updates won't replace an
; existing installation.
AppId={{8F2B6E7A-40A1-4C0D-9E3B-5D71C2A94F08}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=ElectroHugin
; Version info of the setup .exe (Properties > Details). Code signing
; (SignPath) checks product name and version, so they must match the program
; file (src/app/app.rc.in).
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoProductTextVersion={#MyAppVersion}
VersionInfoCompany=ElectroHugin
VersionInfoDescription={#MyAppName} Setup
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=FMPlayerAnalyzer-{#MyAppVersion}-Windows-x64-Setup
SetupIconFile=..\src\app\resources\favicon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; User data (Datenbanken, config.ini, definitions.json) liegt in
; %LOCALAPPDATA%\FM24PlayerAnalyzer und wird vom Installer nie angefasst.

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "fm27views"; Description: "Ansichten (Views) für den FM27-Export in den Ordner von Football Manager 27 kopieren"; GroupDescription: "Football Manager 27:"; Check: Fm27Installed
Name: "fm27tactic"; Description: "Standardtaktik (4-2-3-1 - Bavarian Blitz) in den Taktik-Ordner von Football Manager 27 kopieren"; GroupDescription: "Football Manager 27:"; Check: Fm27Installed

[Files]
Source: "staging\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; FM27 view filters: always next to the app, into the game's views folder on request.
Source: "..\src\app\resources\views\*.fmf"; DestDir: "{app}\views"; Flags: ignoreversion
Source: "..\src\app\resources\views\*.fmf"; DestDir: "{userdocs}\Sports Interactive\Football Manager 27\views"; Flags: ignoreversion; Tasks: fm27views
; FM27 standard tactic (game file): next to the app, into the game's tactics folder on request.
Source: "..\src\app\resources\tactics\*.fmf"; DestDir: "{app}\tactics"; Flags: ignoreversion
Source: "..\src\app\resources\tactics\*.fmf"; DestDir: "{userdocs}\Sports Interactive\Football Manager 27\tactics"; Flags: ignoreversion; Tasks: fm27tactic

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// Offer the views and the tactic only when Football Manager 27 has created its documents folder.
function Fm27Installed: Boolean;
begin
  Result := DirExists(ExpandConstant('{userdocs}\Sports Interactive\Football Manager 27'));
end;

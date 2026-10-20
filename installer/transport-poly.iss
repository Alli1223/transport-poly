; Windows installer for transport-poly (Inno Setup 6). CI builds it from the installed release build:
;   iscc /DAppVersion=0.2.0 /DSourceDir=<staged install> /DOutputDir=<packages> installer\transport-poly.iss
; It installs for the current user (no administrator rights needed) or, if chosen, for everyone, to a
; folder the player picks; adds Start menu and optional desktop shortcuts, and an uninstaller that
; preserves saved games and settings. Run with /SILENT it upgrades an existing
; installation in place and starts the game again (the in-game updater does this).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppIdValue
  #define AppIdValue "{{8F6A2C3E-5B1D-4E7A-9C2F-7D3B1A6E4F21}"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\windows-msvc\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\build\windows-msvc\packages"
#endif

[Setup]
AppId={#AppIdValue}
AppName=transport-poly
AppVersion={#AppVersion}
AppVerName=transport-poly {#AppVersion}
AppPublisher=transport-poly
AppPublisherURL=https://github.com/Alli1223/transport-poly
AppSupportURL=https://github.com/Alli1223/transport-poly/issues
AppUpdatesURL=https://github.com/Alli1223/transport-poly/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\transport-poly
DefaultGroupName=transport-poly
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UsePreviousAppDir=yes
DisableDirPage=no
OutputDir={#OutputDir}
OutputBaseFilename=transport-poly-{#AppVersion}-windows-x64-setup
SetupIconFile=..\src\client\transport-poly.ico
UninstallDisplayIcon={app}\transport-poly.exe
UninstallDisplayName=transport-poly
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\transport-poly"; Filename: "{app}\transport-poly.exe"; WorkingDir: "{app}"
Name: "{group}\transport-poly dedicated server"; Filename: "{app}\transport-poly-server.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall transport-poly"; Filename: "{uninstallexe}"
Name: "{autodesktop}\transport-poly"; Filename: "{app}\transport-poly.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\transport-poly.exe"; Description: "{cm:LaunchProgram,transport-poly}"; Flags: nowait postinstall skipifsilent
; After a silent upgrade (from the game's updater) start the new version straight away.
Filename: "{app}\transport-poly.exe"; Flags: nowait runasoriginaluser; Check: RestartAfterUpdate

[INI]
Filename: "{app}\install-mode.ini"; Section: "Install"; Key: "Scope"; String: "{code:InstallationScope}"

[UninstallDelete]
Type: files; Name: "{app}\install-mode.ini"

[Code]
function RestartAfterUpdate: Boolean;
begin
  Result := WizardSilent and (ExpandConstant('{param:RESTARTGAME|0}') = '1');
end;

function InstallationScope(Param: String): String;
begin
  if IsAdminInstallMode then Result := 'allusers'
  else Result := 'currentuser';
end;

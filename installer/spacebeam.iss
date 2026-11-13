; Inno Setup script for the SpaceBeam OBS plugin.
;
; Build it with installer.ps1, which fills in the version and the staging
; folder:
;   ISCC.exe /DVersion=1.2.0 /DStageDir=..\dist\stage\spacebeam /DHasDock=1 spacebeam.iss
;
; Installs into ProgramData so it applies to every user, and needs admin for
; the same reason. Uninstalling removes the plugin folder entirely.

#ifndef Version
  #define Version "0.0.0"
#endif

#ifndef StageDir
  #define StageDir "..\dist\stage\spacebeam"
#endif

; The dock is a separate DLL; a build without Qt simply omits it.
#ifndef HasDock
  #define HasDock "1"
#endif

#ifndef PluginDir
  #define PluginDir "{commonappdata}\obs-studio\plugins\spacebeam"
#endif

#ifndef Privileges
  #define Privileges "admin"
#endif

#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#ifndef OutputName
  ; Concatenated here: a nested "{#Version}" inside a #define stays literal.
  #define OutputName "SpaceBeam-" + Version + "-windows-x64-setup"
#endif

[Setup]
AppId={{3F0B1E52-9C74-4A6B-8F21-7D2C5E90B4A1}
AppName=SpaceBeam for OBS
AppVersion={#Version}
AppVerName=SpaceBeam for OBS {#Version}
AppPublisher=SpaceBeam for OBS contributors
AppPublisherURL=https://github.com/KoBadger/spacebeam-obs
AppSupportURL=https://github.com/KoBadger/spacebeam-obs/issues
AppUpdatesURL=https://github.com/KoBadger/spacebeam-obs/releases
DefaultDirName={#PluginDir}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired={#Privileges}
OutputBaseFilename={#OutputName}
OutputDir={#OutputDir}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
; Nothing to run at the end; the user starts OBS.
UninstallDisplayName=SpaceBeam for OBS {#Version}
UninstallDisplayIcon={sys}\unins000.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[InstallDelete]
; Versions before 1.2.0 kept the DLLs in the plugin folder itself. Removing
; them here prevents OBS finding the plugin twice after an upgrade.
Type: files; Name: "{app}\spacebeam.dll"
Type: files; Name: "{app}\spacebeam_dock.dll"

[Files]
Source: "{#StageDir}\bin\64bit\spacebeam.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
#if HasDock == "1"
Source: "{#StageDir}\bin\64bit\spacebeam_dock.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion skipifsourcedoesntexist
#endif
Source: "{#StageDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; The plugin has no per-user state in this folder, so clean it out completely.
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
var
  ObsPath: String;
begin
  Result := True;
  ObsPath := ExpandConstant('{commonpf}\obs-studio\bin\64bit\obs64.exe');
  if not FileExists(ObsPath) then begin
    if MsgBox('OBS Studio was not found in the default location.' + #13#10 +
              'This plugin only works inside OBS Studio. Install it anyway?',
              mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if FileExists(ExpandConstant('{commonpf}\obs-studio\bin\64bit\obs64.exe')) then
      MsgBox('SpaceBeam is installed.' + #13#10#13#10 +
             'Start OBS and add a SpaceBeam filter to a source, or open the ' +
             'dock from Docks > SpaceBeam.',
             mbInformation, MB_OK);
  end;
end;

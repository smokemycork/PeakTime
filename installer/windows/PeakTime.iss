; PeakTime Windows installer (Inno Setup 6)
; Built by GitHub Actions:  iscc /DMyAppVersion=1.0.0 /DArtefactsDir=<...>\Release /O<out> PeakTime.iss
; AppId never changes, so a newer version installs straight over an older one.

#define MyAppName "PeakTime"
#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef ArtefactsDir
  #define ArtefactsDir "..\..\build\PeakTime_artefacts\Release"
#endif

[Setup]
AppId={{B6E2F4A1-3C5D-4E8F-9A7B-1D2C3E4F5A60}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=PeakTime Audio
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputBaseFilename=PeakTime-{#MyAppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
CloseApplications=yes
UninstallDisplayName={#MyAppName}

[Types]
Name: "full"; Description: "VST3 plugin + standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (Ableton, FL Studio, Cubase, Bitwig...)"; Types: full custom
Name: "app";  Description: "Standalone app (drop in a bounce, no DAW needed)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone app"; Components: app; Flags: unchecked

[InstallDelete]
; remove any previous version of the plugin bundle first
Type: filesandordirs; Name: "{commoncf64}\VST3\PeakTime.vst3"

[Files]
Source: "{#ArtefactsDir}\VST3\PeakTime.vst3\*"; DestDir: "{commoncf64}\VST3\PeakTime.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#ArtefactsDir}\Standalone\PeakTime.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\PeakTime.exe"; Components: app
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\PeakTime.exe"; Tasks: desktopicon

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\PeakTime.vst3"

[Run]
Filename: "{app}\PeakTime.exe"; Description: "Open PeakTime now"; Flags: nowait postinstall skipifsilent; Components: app

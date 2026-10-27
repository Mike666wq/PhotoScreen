#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must be supplied
#endif
#ifndef OutputDir
  #error OutputDir must be supplied
#endif

[Setup]
AppId={{588AD21E-B81D-46C6-A6D5-54B25CBA9D21}
AppName=PhotoScreen
AppVersion={#MyAppVersion}
AppPublisher=PhotoScreen
DefaultDirName={localappdata}\Programs\PhotoScreen
DefaultGroupName=PhotoScreen
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
UsePreviousAppDir=yes
Uninstallable=yes
AppMutex=PhotoScreen.App
CloseApplications=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#OutputDir}
OutputBaseFilename=PhotoScreen-{#MyAppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\PhotoScreen"; Filename: "{app}\PhotoScreen.exe"
Name: "{group}\Uninstall PhotoScreen"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\PhotoScreen.exe"; Description: "Launch PhotoScreen"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\PhotoScreen.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "PhotoScreen"; Flags: uninsdeletevalue

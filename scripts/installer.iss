#ifndef PackageDir
  #error PackageDir must identify the deployed application directory
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
[Setup]
AppId={{D172A1C9-51C3-4B6F-8750-25E49C099C64}
AppName=ImageSifter
AppVersion=2.0.0
AppPublisher=ImageSifter contributors
AppPublisherURL=https://github.com/Stylle/ImageSifter
DefaultDirName={localappdata}\Programs\ImageSifter
DefaultGroupName=ImageSifter
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=ImageSifter-2.0.0-windows-x64-setup
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\ImageSifter.exe
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "fileassociation"; Description: "Associate .imagesifter project files"; Flags: unchecked
[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\ImageSifter"; Filename: "{app}\ImageSifter.exe"
Name: "{autodesktop}\ImageSifter"; Filename: "{app}\ImageSifter.exe"; Tasks: desktopicon
[Registry]
Root: HKCU; Subkey: "Software\Classes\.imagesifter\OpenWithProgids"; ValueType: string; ValueName: "ImageSifter.Project"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassociation
Root: HKCU; Subkey: "Software\Classes\ImageSifter.Project"; ValueType: string; ValueData: "ImageSifter Project"; Flags: uninsdeletekey; Tasks: fileassociation
Root: HKCU; Subkey: "Software\Classes\ImageSifter.Project\DefaultIcon"; ValueType: string; ValueData: "{app}\ImageSifter.exe,0"; Tasks: fileassociation
Root: HKCU; Subkey: "Software\Classes\ImageSifter.Project\shell\open\command"; ValueType: string; ValueData: """{app}\ImageSifter.exe"" ""%1"""; Tasks: fileassociation
[Run]
Filename: "{app}\ImageSifter.exe"; Description: "Launch ImageSifter"; Flags: nowait postinstall skipifsilent

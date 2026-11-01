; Instalador de Windows para Driftbox (Inno Setup 6)
; Compilar:  ISCC.exe /DAppVersion=1.0.0 /DSourceDir=C:\ruta\build\Driftbox_artefacts\Release Driftbox.iss

#define AppName "Driftbox"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\Driftbox_artefacts\Release"
#endif

[Setup]
AppId={{8C1E8C7A-4B7E-4C1D-9F7B-2D5E6A1B3C4D}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Driftbox Audio
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputBaseFilename=Driftbox-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName={#AppName} {#AppVersion}

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Instalación completa"
Name: "custom"; Description: "Personalizada"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plugin VST3 (Ableton, FL Studio, Cubase, Reaper, Bitwig, Studio One...)"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Aplicación independiente (Standalone)"; Types: full

[Files]
Source: "{#SourceDir}\VST3\Driftbox.vst3\*"; DestDir: "{commoncf64}\VST3\Driftbox.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Standalone\Driftbox.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Driftbox"; Filename: "{app}\Driftbox.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Driftbox.vst3"

; Copyright (c) 2026 Alesson Queiroz. All rights reserved.
; Caecilia is proprietary and confidential; see LICENSE.
;
; Inno Setup script for the Caecilia Windows installer (VST3 + Standalone).
; Build:  iscc /DMyAppVersion=0.1.0 installer\caecilia.iss
; The artefacts directory can be overridden with /DArtefactsDir=<path>.

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif

#ifndef ArtefactsDir
  ; Default: the CMake plugin output, relative to this .iss file (installer\).
  #define ArtefactsDir "..\build\src\caecilia\plugin\Caecilia_artefacts\Release"
#endif

#define MyAppName "Caecilia"
#define MyAppPublisher "Alesson Queiroz"
#define MyAppURL "https://github.com/alessonqueirozdev-hub/Caecilia"
#define MyAppExeName "Caecilia.exe"

[Setup]
; A stable, product-unique GUID so upgrades replace in place.
AppId={{7C1E9F2A-3B84-4C7D-9A1E-CAEC11A00001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} pipe organ instrument installer
VersionInfoCopyright=Copyright (c) 2026 {#MyAppPublisher}. All rights reserved.

DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
LicenseFile=..\LICENSE
OutputDir=..\dist\installer
OutputBaseFilename=Caecilia-Setup-{#MyAppVersion}-x64
Compression=lzma2/ultra64
SolidCompression=yes
; 64-bit only — the plugin ships x64 exclusively.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
PrivilegesRequired=admin
WizardStyle=modern
SetupIconFile=caecilia.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
WizardImageFile=wizard-large.bmp
WizardSmallImageFile=wizard-small.bmp
WizardImageStretch=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation (VST3 + Standalone)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (for your DAW)"; Types: full custom; Flags: checkablealone
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: standalone; Flags: unchecked

[Files]
; --- VST3 plug-in bundle -> the shared 64-bit VST3 folder --------------------
Source: "{#ArtefactsDir}\VST3\{#MyAppName}.vst3\*"; \
    DestDir: "{commoncf64}\VST3\{#MyAppName}.vst3"; \
    Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion

; --- Standalone application -> Program Files ---------------------------------
Source: "{#ArtefactsDir}\Standalone\{#MyAppExeName}"; \
    DestDir: "{app}"; Components: standalone; Flags: ignoreversion

; --- Documentation -----------------------------------------------------------
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Components: standalone
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; Components: standalone

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent; Components: standalone

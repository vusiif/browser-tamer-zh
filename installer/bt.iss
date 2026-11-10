; Inno Setup script for Browser Tamer 中文版 (Windows x64, per-user install).
;
; Build it with scripts/package-win.ps1, which compiles the Release build first and passes:
;   /DAppVersion=<x.y.z>   version from CMakeLists.txt
;   /DBuildDir=<path>      directory containing bt.exe and clearurls_rules.json
;
; Registry entries mirror resources/wix/patch.xml (the upstream WiX installer), so Windows
; sees the same browser registration whichever installer was used.

#ifndef AppVersion
  #error AppVersion is not defined. Build with scripts/package-win.ps1 or pass /DAppVersion=x.y.z
#endif
#ifndef BuildDir
  #define BuildDir "..\out\build\x64-release"
#endif

#define AppName "Browser Tamer"
#define AppExe "bt.exe"
; must match ProtoNameRelease / CustomProtoName in globals.h.in
#define HtmClass "BrowserTamerHTM"
#define PdfClass "BrowserTamerPDF"
#define EpubClass "BrowserTamerEPUB"
#define CustomProto "x-bt"
#define AppDescription "Redirects open URLs to a browser of your choice."

[Setup]
; Inno's own product id. Do not change it, or upgrades will install side by side.
AppId={{1F2668D9-5A99-4125-BB8E-F89185115611}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion} 中文版
AppPublisher=aloneguid（原作者）
AppPublisherURL=https://github.com/vusiif/browser-tamer-zh
AppSupportURL=https://github.com/vusiif/browser-tamer-zh/issues
AppUpdatesURL=https://github.com/vusiif/browser-tamer-zh/releases
AppCopyright=Copyright © aloneguid. Apache License 2.0.
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} 中文版安装程序

; per-user install, no administrator rights needed (same as the upstream MSI)
PrivilegesRequired=lowest
DefaultDirName={localappdata}\BrowserTamer
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

LicenseFile=..\LICENSE
SetupIconFile=..\icon.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} 中文版
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\out\installer
OutputBaseFilename=BrowserTamer-zh-{#AppVersion}-win64-setup

; tell Explorer that file/URL associations changed
ChangesAssociations=yes
; close running bt.exe (picker/config) before replacing it
CloseApplications=yes

[Languages]
Name: "chs"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\clearurls_rules.json"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; --- custom protocol (x-bt)
Root: HKCU; Subkey: "Software\Classes\{#CustomProto}"; ValueType: string; ValueName: ""; ValueData: "URL:BrowserTamer Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#CustomProto}"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\{#CustomProto}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

; --- browser registration (Default Apps)
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer"; ValueType: string; ValueName: ""; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\shell\open\command"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{#AppDescription}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: "{app}\{#AppExe},0"

Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\URLAssociations"; ValueType: string; ValueName: "{#CustomProto}"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\URLAssociations"; ValueType: string; ValueName: "http"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\URLAssociations"; ValueType: string; ValueName: "https"; ValueData: "{#HtmClass}"

Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".htm"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".html"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mht"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mhtml"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".shtml"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".svg"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".webp"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xht"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xhtml"; ValueData: "{#HtmClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "{#PdfClass}"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".epub"; ValueData: "{#EpubClass}"

; --- document classes referenced by the associations above
Root: HKCU; Subkey: "Software\Classes\{#HtmClass}"; ValueType: string; ValueName: ""; ValueData: "Browser Tamer Document"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#HtmClass}\Application"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"
Root: HKCU; Subkey: "Software\Classes\{#HtmClass}\Application"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{#AppDescription}"
Root: HKCU; Subkey: "Software\Classes\{#HtmClass}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKCU; Subkey: "Software\Classes\{#HtmClass}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

Root: HKCU; Subkey: "Software\Classes\{#PdfClass}"; ValueType: string; ValueName: ""; ValueData: "Browser Tamer Document"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#PdfClass}\Application"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"
Root: HKCU; Subkey: "Software\Classes\{#PdfClass}\Application"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{#AppDescription}"
Root: HKCU; Subkey: "Software\Classes\{#PdfClass}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},1"
Root: HKCU; Subkey: "Software\Classes\{#PdfClass}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

Root: HKCU; Subkey: "Software\Classes\{#EpubClass}"; ValueType: string; ValueName: ""; ValueData: "Browser Tamer Document"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#EpubClass}\Application"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"
Root: HKCU; Subkey: "Software\Classes\{#EpubClass}\Application"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{#AppDescription}"
Root: HKCU; Subkey: "Software\Classes\{#EpubClass}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},2"
Root: HKCU; Subkey: "Software\Classes\{#EpubClass}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

; --- make Windows list Browser Tamer under Default Apps
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "{#AppName}"; ValueData: "Software\Clients\StartMenuInternet\BrowserTamer\Capabilities"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

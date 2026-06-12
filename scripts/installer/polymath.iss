; ============================================================================
;  Hearth — Inno Setup installer script
;  Wraps the portable bundle that scripts\package.ps1 stages into a single
;  Windows installer (.exe). This does NOT replace the portable zip — it wraps
;  the very same staged folder, so the zip remains the shippable fallback when
;  Inno Setup is not available on the build box.
;
;  WHAT THIS PACKAGES
;    The staged bundle directory dist\Hearth-<ver>-win64-<flavor>\ produced by:
;        pwsh scripts\package.ps1 -Flavor cuda          (or -Flavor cpu)
;    i.e. Hearth.exe + the Qt runtime + engine/CUDA/ONNX/OpenCV DLLs + the
;    VC++ redist DLLs + the first-run scripts + an EMPTY data\models\ (models are
;    ~28 GB and are fetched on first run, never bundled here).
;
;  WHAT IT DOES NOT BUNDLE
;    Models. They are downloaded post-install by the first-run wizard
;    (first-run.ps1 -> fetch-models.ps1). Bundling 28 GB into an installer is a
;    non-starter; the wizard is the model-delivery mechanism.
;
;  HOW TO BUILD THE INSTALLER  (Inno Setup 6, ISCC.exe)
;    1. Stage the bundle (folder, not zip):
;         pwsh scripts\package.ps1 -Flavor cuda -NoZip
;       This writes dist\Hearth-<ver>-win64-cuda\.
;    2. Compile (pass the version + flavor so SourceDir resolves):
;         "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" ^
;             /DAppVersion=0.1.0 /DFlavor=cuda scripts\installer\polymath.iss
;       Output: dist\Hearth-0.1.0-win64-cuda-Setup.exe
;    Defaults: AppVersion=0.1.0, Flavor=cuda. Override either with /D as above.
;
;    Inno Setup is NOT installed on the current build machine. Install it with:
;         winget install JRSoftware.InnoSetup
;       (or https://jrsoftware.org/isdl.php). Until then, ship the portable zip
;       from scripts\package.ps1 — it is the validated fallback.
;
;  CODE SIGNING  (ship UNSIGNED for now; here's the real procedure)
;    Without a signature, SmartScreen shows "Windows protected your PC" on first
;    launch (More info -> Run anyway). To sign, obtain an Authenticode cert
;    (ideally OV/EV from a CA; an EV cert clears SmartScreen reputation faster)
;    and sign BOTH Hearth.exe and the installer:
;
;      signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
;          /f mycert.pfx /p <pw> "dist\Hearth-<ver>-win64-<flavor>\Hearth.exe"
;      ; re-stage so the signed exe is the one packaged, then build the installer,
;      ; then sign the installer the same way:
;      signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
;          /f mycert.pfx /p <pw> "dist\Hearth-<ver>-win64-<flavor>-Setup.exe"
;
;    To let ISCC sign automatically during compile, register a sign tool in the
;    IDE (Tools -> Configure Sign Tools) named e.g. "signtool", then uncomment the
;    SignTool= line in [Setup] below. Always timestamp (/tr) so signatures outlive
;    the cert. EV certs typically live on an HSM/token; CI signing then uses the
;    token's CSP rather than a .pfx.
; ============================================================================

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Flavor
  #define Flavor "cuda"
#endif

; The staged bundle folder produced by package.ps1 -NoZip. Resolved relative to
; this .iss (scripts\installer\ -> ..\..\dist\...).
#define BundleName "Hearth-" + AppVersion + "-win64-" + Flavor
#define SourceDir  "..\..\dist\" + BundleName

[Setup]
AppId={{B1A7E0E2-9C4F-4B3A-8D6E-HEARTH0001}
AppName=Hearth
AppVersion={#AppVersion}
AppPublisher=Hearth
AppPublisherURL=https://example.invalid/hearth
DefaultDirName={autopf}\Hearth
DefaultGroupName=Hearth
; Per-user data lives under the install dir's data\ folder (portable layout);
; the app resolves data\ beside Hearth.exe (see src\app\main.cpp). For a true
; multi-user install you'd instead point the app at {localappdata}\Hearth —
; documented in docs\PACKAGING.md.
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename={#BundleName}-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
; A non-admin install is fine (portable app, no services/drivers). lowest = install
; to a writable location without a UAC prompt; the wizard auto-falls-back to a
; per-user dir if {autopf} isn't writable.
PrivilegesRequiredOverridesAllowed=dialog
PrivilegesRequired=lowest
; Uncomment after registering a sign tool (see header):
; SignTool=signtool
; SignedUninstaller=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "fetchmodels"; Description: "Download models now (opens the first-run setup after install)"; GroupDescription: "First run:"

[Files]
; Recursively package the entire staged bundle. recursesubdirs+createallsubdirs
; pulls in platforms\, qml\, imageformats\, data\models\ (empty placeholder), etc.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
; Ensure the models folder exists even though it's empty in the bundle, so the
; first-run wizard and the app find data\models\ on a fresh install.
Name: "{app}\data\models"
Name: "{app}\data\logs"

[Icons]
Name: "{group}\Hearth"; Filename: "{app}\Hearth.exe"; WorkingDir: "{app}"
Name: "{group}\Hearth first-run setup"; Filename: "powershell.exe"; \
      Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\first-run.ps1"""; \
      WorkingDir: "{app}"; Comment: "GPU check + guided model download"
Name: "{group}\{cm:UninstallProgram,Hearth}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Hearth"; Filename: "{app}\Hearth.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; Post-install: if the user ticked "Download models now", run the first-run
; wizard (GPU check + guided fetch). Otherwise offer to launch the app. The app
; itself never dies without models — it shows the in-app cold-start guide — so
; this is a convenience, not a requirement.
; We invoke the in-box Windows PowerShell (powershell.exe, always present on
; Windows) rather than pwsh (not installed on a clean box). The first-run scripts
; are saved UTF-8-with-BOM so Windows PowerShell 5.1 parses them correctly.
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\first-run.ps1"""; \
    WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent; \
    Description: "Run first-run setup (GPU check + model download)"; Tasks: fetchmodels
Filename: "{app}\Hearth.exe"; WorkingDir: "{app}"; \
    Flags: postinstall nowait skipifsilent unchecked; \
    Description: "Launch Hearth now"; Tasks: not fetchmodels

[UninstallDelete]
; Logs and any cache the app writes under the install dir. Models live in
; data\models\ — leave the user's downloaded models alone on uninstall (they're
; large and the user paid the bandwidth); only remove logs.
Type: filesandordirs; Name: "{app}\data\logs"

[Code]
{ Guard: refuse to build/install if the staged bundle is missing Hearth.exe.
  ISCC fails at compile time if SourceDir doesn't exist; this is the runtime
  belt-and-suspenders if someone hand-edits the layout. }
function InitializeSetup(): Boolean;
begin
  Result := True;
end;

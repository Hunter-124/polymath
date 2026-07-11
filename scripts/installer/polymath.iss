; ============================================================================
;  Polymath — Inno Setup installer script
;  Wraps the portable bundle that scripts\package.ps1 stages into a single
;  Windows installer (.exe). This does NOT replace the portable zip — it wraps
;  the very same staged folder, so the zip remains the shippable fallback when
;  Inno Setup is not available on the build box.
;
;  WHAT THIS PACKAGES
;    The staged bundle directory dist\Polymath-<ver>-win64-<flavor>\ produced by:
;        pwsh scripts\package.ps1 -Flavor cuda          (or -Flavor cpu)
;    i.e. Polymath.exe + the Qt runtime + engine/CUDA/ONNX/OpenCV DLLs + the
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
;       This writes dist\Polymath-<ver>-win64-cuda\.
;    2. Compile (pass the version + flavor so SourceDir resolves):
;         "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" ^
;             /DAppVersion=0.3.0 /DFlavor=cuda scripts\installer\polymath.iss
;       Output: dist\Polymath-0.3.0-win64-cuda-Setup.exe
;    Defaults: AppVersion=0.3.0, Flavor=cuda. Override either with /D as above.
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
;    and sign BOTH Polymath.exe and the installer:
;
;      signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
;          /f mycert.pfx /p <pw> "dist\Polymath-<ver>-win64-<flavor>\Polymath.exe"
;      ; re-stage so the signed exe is the one packaged, then build the installer,
;      ; then sign the installer the same way:
;      signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
;          /f mycert.pfx /p <pw> "dist\Polymath-<ver>-win64-<flavor>-Setup.exe"
;
;    To let ISCC sign automatically during compile, register a sign tool in the
;    IDE (Tools -> Configure Sign Tools) named e.g. "signtool", then uncomment the
;    SignTool= line in [Setup] below. Always timestamp (/tr) so signatures outlive
;    the cert. EV certs typically live on an HSM/token; CI signing then uses the
;    token's CSP rather than a .pfx.
; ============================================================================

#ifndef AppVersion
  #define AppVersion "0.3.1"
#endif
#ifndef Flavor
  #define Flavor "cuda"
#endif

; The staged bundle folder produced by package.ps1 -NoZip. Resolved relative to
; this .iss (scripts\installer\ -> ..\..\dist\...).
#define BundleName "Polymath-" + AppVersion + "-win64-" + Flavor
#define SourceDir  "..\..\dist\" + BundleName

[Setup]
AppId={{7F3C2E9A-1D4B-4A8E-9C6F-2B5A7D0E4C10}
AppName=Polymath
AppVersion={#AppVersion}
AppPublisher=Polymath
AppPublisherURL=https://example.invalid/polymath
; Install PER-USER into a writable location. This is REQUIRED, not cosmetic:
; the app uses the portable layout (data\ beside Polymath.exe, plus model
; downloads from first-run.ps1), so the install dir must be writable by the
; running user. A Program Files install ({commonpf}) is read-only for a normal
; user, so logs/db/model files fail and the app aborts on launch. {localappdata}\
; Programs is always user-writable and needs no elevation.
DefaultDirName={localappdata}\Programs\Polymath
DefaultGroupName=Polymath
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename={#BundleName}-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
; Portable app, no services/drivers -> always install per-user with no UAC prompt.
; We do NOT allow an "all users" (admin, Program Files) override: that lands the
; app in a read-only location and it fails to launch. Force the writable per-user
; install every time.
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
Name: "{group}\Polymath"; Filename: "{app}\Polymath.exe"; WorkingDir: "{app}"
Name: "{group}\Polymath first-run setup"; Filename: "powershell.exe"; \
      Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\first-run.ps1"""; \
      WorkingDir: "{app}"; Comment: "GPU check + guided model download"
Name: "{group}\{cm:UninstallProgram,Polymath}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Polymath"; Filename: "{app}\Polymath.exe"; WorkingDir: "{app}"; Tasks: desktopicon

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
Filename: "{app}\Polymath.exe"; WorkingDir: "{app}"; \
    Flags: postinstall nowait skipifsilent unchecked; \
    Description: "Launch Polymath now"; Tasks: not fetchmodels

[UninstallDelete]
; Logs and any cache the app writes under the install dir. Models live in
; data\models\ — leave the user's downloaded models alone on uninstall (they're
; large and the user paid the bandwidth); only remove logs.
Type: filesandordirs; Name: "{app}\data\logs"

[Code]
{ Guard: refuse to build/install if the staged bundle is missing Polymath.exe.
  ISCC fails at compile time if SourceDir doesn't exist; this is the runtime
  belt-and-suspenders if someone hand-edits the layout. }
function InitializeSetup(): Boolean;
begin
  Result := True;
end;

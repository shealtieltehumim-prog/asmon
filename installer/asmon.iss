; Asmon — Windows installer (Inno Setup 6)
; Built by CI: iscc /DAppVersion=x.y.z /DDist=..\dist /DReTools=..\retools asmon.iss
; Asmon is free software under the GNU GPL v3 (fork of SASM by Dmitriy Manushin).

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Dist
  #define Dist "..\dist"
#endif
#ifndef ReTools
  #define ReTools "..\retools"
#endif

[Setup]
AppId={{6F4B7A52-2C1D-4E0B-9A3E-41534D4F4E01}
AppName=Asmon
AppVersion={#AppVersion}
AppVerName=Asmon {#AppVersion}
AppPublisher=Asmon contributors
AppPublisherURL=https://github.com/shealtieltehumim-prog/asmon
AppSupportURL=https://github.com/shealtieltehumim-prog/asmon/issues
DefaultDirName={autopf}\Asmon
DefaultGroupName=Asmon
DisableProgramGroupPage=yes
; per-user install without admin rights is allowed (choose in the first page)
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=output
OutputBaseFilename=Asmon-{#AppVersion}-Setup
SetupIconFile=..\icon.ico
UninstallDisplayIcon={app}\asmon.exe
LicenseFile=..\COPYING
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=4
ChangesEnvironment=yes
ChangesAssociations=yes

[Languages]
Name: "he"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
he.CompCore=אסמון + NASM + MinGW-w64 (GCC, ld, binutils, gdb)
en.CompCore=Asmon + NASM + MinGW-w64 (GCC, ld, binutils, gdb)
he.CompX86=כלי 32 ביט (MinGW-w64 i686: GCC, ld, gdb) לתוכניות x86
en.CompX86=32-bit tools (MinGW-w64 i686: GCC, ld, gdb) for x86 programs
he.CompLlvm=LLVM: lld, llvm-objdump, lldb (בניית תוכניות לינוקס בלי WSL)
en.CompLlvm=LLVM: lld, llvm-objdump, lldb (build Linux programs without WSL)
he.CompGhidra=Ghidra (כולל Java)
en.CompGhidra=Ghidra (with Java)
he.CompCutter=Cutter (Rizin)
en.CompCutter=Cutter (Rizin)
he.CompX64dbg=x64dbg
en.CompX64dbg=x64dbg
he.CompImhex=ImHex (עורך hex)
en.CompImhex=ImHex (hex editor)
he.TypeFull=התקנה מלאה (כל כלי ה-RE)
en.TypeFull=Full installation (all RE tools)
he.TypeCore=התקנה בסיסית (כתיבה, הרצה ודיבוג)
en.TypeCore=Basic installation (write, run and debug)
he.TypeCustom=התקנה מותאמת אישית
en.TypeCustom=Custom installation
he.TaskPath=הוסף את NASM וכלי MinGW ל-PATH (לשימוש מהטרמינל)
en.TaskPath=Add NASM and the MinGW tools to PATH (for use in a terminal)
he.TaskAssoc=פתח קבצי ‎.asm באסמון
en.TaskAssoc=Open .asm files with Asmon
he.GroupOther=אפשרויות נוספות:
en.GroupOther=Other options:

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "core"; Description: "{cm:TypeCore}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "core"; Description: "{cm:CompCore}"; Types: full core custom; Flags: fixed
Name: "llvm"; Description: "{cm:CompLlvm}"; Types: full core
Name: "x86"; Description: "{cm:CompX86}"; Types: full
Name: "ghidra"; Description: "{cm:CompGhidra}"; Types: full
Name: "cutter"; Description: "{cm:CompCutter}"; Types: full
Name: "x64dbg"; Description: "{cm:CompX64dbg}"; Types: full
Name: "imhex"; Description: "{cm:CompImhex}"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "addpath"; Description: "{cm:TaskPath}"; GroupDescription: "{cm:GroupOther}"; Flags: unchecked
Name: "assoc"; Description: "{cm:TaskAssoc}"; GroupDescription: "{cm:GroupOther}"; Flags: unchecked

[Files]
Source: "{#Dist}\*"; DestDir: "{app}"; Excludes: "\LLVM\*,\MinGW\*,\portable"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: core
Source: "{#Dist}\LLVM\*"; DestDir: "{app}\LLVM"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: llvm
Source: "{#Dist}\MinGW\*"; DestDir: "{app}\MinGW"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: x86
Source: "{#ReTools}\ghidra\*"; DestDir: "{app}\tools\ghidra"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: ghidra
Source: "{#ReTools}\jdk\*"; DestDir: "{app}\tools\jdk"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: ghidra
Source: "{#ReTools}\cutter\*"; DestDir: "{app}\tools\cutter"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: cutter
Source: "{#ReTools}\x64dbg\*"; DestDir: "{app}\tools\x64dbg"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: x64dbg
Source: "{#ReTools}\imhex\*"; DestDir: "{app}\tools\imhex"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: imhex
Source: "..\THIRD_PARTY.md"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Asmon"; Filename: "{app}\asmon.exe"
Name: "{autodesktop}\Asmon"; Filename: "{app}\asmon.exe"; Tasks: desktopicon

[Registry]
; .asm association (removed on uninstall)
Root: HKA; Subkey: "Software\Classes\.asm\OpenWithProgids"; ValueType: string; ValueName: "Asmon.asm"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Asmon.asm"; ValueType: string; ValueName: ""; ValueData: "Assembly source"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Asmon.asm\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\asmon.exe,0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Asmon.asm\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\asmon.exe"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\asmon.exe"; Description: "{cm:LaunchProgram,Asmon}"; Flags: nowait postinstall skipifsilent

[Code]
// PATH handling: entries are added only with the "addpath" task and removed
// exactly (no other PATH entry is touched) on uninstall.
function EnvKey(): String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

function EnvRoot(): Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function PathHas(Path, Dir: String): Boolean;
begin
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';') > 0;
end;

procedure AddToPath(Dir: String);
var Path: String;
begin
  if not RegQueryStringValue(EnvRoot(), EnvKey(), 'Path', Path) then Path := '';
  if PathHas(Path, Dir) then exit;
  if (Path <> '') and (Copy(Path, Length(Path), 1) <> ';') then Path := Path + ';';
  RegWriteExpandStringValue(EnvRoot(), EnvKey(), 'Path', Path + Dir);
end;

procedure RemoveFromPath(Dir: String);
var Path, Rest, Part, NewPath: String; P: Integer;
begin
  if not RegQueryStringValue(EnvRoot(), EnvKey(), 'Path', Path) then exit;
  Rest := Path; NewPath := '';
  while Rest <> '' do begin
    P := Pos(';', Rest);
    if P = 0 then begin Part := Rest; Rest := ''; end
    else begin Part := Copy(Rest, 1, P - 1); Rest := Copy(Rest, P + 1, Length(Rest)); end;
    if (Part <> '') and (Uppercase(Part) <> Uppercase(Dir)) then begin
      if NewPath <> '' then NewPath := NewPath + ';';
      NewPath := NewPath + Part;
    end;
  end;
  if NewPath <> Path then RegWriteExpandStringValue(EnvRoot(), EnvKey(), 'Path', NewPath);
end;

// The GPL text is English: in the Hebrew (right-to-left) wizard the license box
// would align it to the right and flip its punctuation. Make that box LTR.
const
  GWL_EXSTYLE = -20;
  WS_EX_RIGHT = $1000;
  WS_EX_RTLREADING = $2000;
  WS_EX_LEFTSCROLLBAR = $4000;
  WS_EX_LAYOUTRTL = $400000;

function GetWindowLong(Wnd: HWND; Index: Integer): LongInt;
  external 'GetWindowLongW@user32.dll stdcall';
function SetWindowLong(Wnd: HWND; Index: Integer; NewLong: LongInt): LongInt;
  external 'SetWindowLongW@user32.dll stdcall';

procedure InitializeWizard();
var
  H: HWND;
  Text: String;
begin
  H := WizardForm.LicenseMemo.Handle;
  SetWindowLong(H, GWL_EXSTYLE, GetWindowLong(H, GWL_EXSTYLE)
    and not (WS_EX_RIGHT or WS_EX_RTLREADING or WS_EX_LEFTSCROLLBAR or WS_EX_LAYOUTRTL));
  Text := WizardForm.LicenseMemo.Text;
  WizardForm.LicenseMemo.Text := Text;
end;

// Used only by the automated screenshot test (/AUTOACCEPT=1).
procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpLicense) and (ExpandConstant('{param:AUTOACCEPT|0}') = '1') then
    WizardForm.LicenseAcceptedRadio.Checked := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addpath') then begin
    AddToPath(ExpandConstant('{app}\NASM'));
    AddToPath(ExpandConstant('{app}\MinGW64\bin'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then begin
    RemoveFromPath(ExpandConstant('{app}\NASM'));
    RemoveFromPath(ExpandConstant('{app}\MinGW64\bin'));
  end;
end;

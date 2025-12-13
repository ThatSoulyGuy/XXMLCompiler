; XXML Compiler Installer Script for Inno Setup
; This creates a standard Windows installer (.exe)
;
; Requirements:
;   - Inno Setup 6.x (https://jrsoftware.org/isinfo.php)
;   - Pre-built XXML compiler in build/release/bin
;   - MinGW-w64 extracted to installer/temp/mingw64 (optional)

#define MyAppName "XXML Compiler"
#define MyAppVersion GetEnv('XXML_VERSION')
#if MyAppVersion == ""
  #define MyAppVersion "3.0.0"
#endif
#define MyAppPublisher "XXML Project"
#define MyAppURL "https://github.com/yourusername/xxml"
#define MyAppExeName "xxml.exe"

#define ProjectRoot GetEnv('XXML_PROJECT_ROOT')
#if ProjectRoot == ""
  #define ProjectRoot "..\.."
#endif

#define BuildDir ProjectRoot + "\build\release"
#define MinGWDir GetEnv('MINGW_PATH')
#if MinGWDir == ""
  #define MinGWDir "..\temp\mingw64"
#endif

[Setup]
; Application identity
AppId={{8E5F4A2B-1C3D-4E5F-6A7B-8C9D0E1F2A3C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}

; Installation settings
DefaultDirName={autopf}\XXML
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile={#ProjectRoot}\LICENSE
OutputDir=..\out
OutputBaseFilename=xxml-{#MyAppVersion}-windows-x64-setup
SetupIconFile=..\resources\xxml.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

; Require 64-bit Windows
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Privileges
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog

; Uninstall info
UninstallDisplayIcon={app}\bin\xxml.exe
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation (XXML + MinGW GCC)"
Name: "compact"; Description: "Compact installation (XXML only)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "main"; Description: "XXML Compiler"; Types: full compact custom; Flags: fixed
Name: "stdlib"; Description: "Standard Library"; Types: full compact custom; Flags: fixed
Name: "lsp"; Description: "Language Server (for IDE integration)"; Types: full custom
Name: "mingw"; Description: "MinGW-w64 GCC Toolchain"; Types: full custom
Name: "docs"; Description: "Documentation"; Types: full custom

[Tasks]
Name: "addtopath"; Description: "Add XXML to system PATH"; GroupDescription: "Environment:"; Flags: checkedonce
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; Main compiler
Source: "{#BuildDir}\bin\xxml.exe"; DestDir: "{app}\bin"; Flags: ignoreversion; Components: main

; LSP Server
Source: "{#BuildDir}\bin\xxml-lsp.exe"; DestDir: "{app}\bin"; Flags: ignoreversion skipifsourcedoesntexist; Components: lsp

; Runtime libraries
Source: "{#BuildDir}\lib\XXMLLLVMRuntime.lib"; DestDir: "{app}\lib"; Flags: ignoreversion; Components: main
Source: "{#BuildDir}\lib\XXMLLib.lib"; DestDir: "{app}\lib"; Flags: ignoreversion skipifsourcedoesntexist; Components: main

; Standard Library (recursively copy entire Language directory)
Source: "{#ProjectRoot}\Language\*"; DestDir: "{app}\Language"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: stdlib

; Runtime C source (for reference)
Source: "{#ProjectRoot}\runtime\*.c"; DestDir: "{app}\runtime"; Flags: ignoreversion; Components: main
Source: "{#ProjectRoot}\runtime\*.h"; DestDir: "{app}\runtime"; Flags: ignoreversion skipifsourcedoesntexist; Components: main

; Documentation
Source: "{#ProjectRoot}\docs\*.md"; DestDir: "{app}\docs"; Flags: ignoreversion skipifsourcedoesntexist; Components: docs
Source: "{#ProjectRoot}\docs\language\*.md"; DestDir: "{app}\docs\language"; Flags: ignoreversion skipifsourcedoesntexist; Components: docs
Source: "{#ProjectRoot}\docs\stdlib\*.md"; DestDir: "{app}\docs\stdlib"; Flags: ignoreversion skipifsourcedoesntexist; Components: docs
Source: "{#ProjectRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist; Components: docs
Source: "{#ProjectRoot}\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist; Components: docs

; MinGW-w64 (if available)
Source: "{#MinGWDir}\bin\*.exe"; DestDir: "{app}\mingw64\bin"; Flags: ignoreversion skipifsourcedoesntexist; Components: mingw
Source: "{#MinGWDir}\bin\*.dll"; DestDir: "{app}\mingw64\bin"; Flags: ignoreversion skipifsourcedoesntexist; Components: mingw
Source: "{#MinGWDir}\lib\gcc\*"; DestDir: "{app}\mingw64\lib\gcc"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist; Components: mingw
Source: "{#MinGWDir}\lib\*.a"; DestDir: "{app}\mingw64\lib"; Flags: ignoreversion skipifsourcedoesntexist; Components: mingw
Source: "{#MinGWDir}\lib\*.o"; DestDir: "{app}\mingw64\lib"; Flags: ignoreversion skipifsourcedoesntexist; Components: mingw
Source: "{#MinGWDir}\x86_64-w64-mingw32\lib\*.a"; DestDir: "{app}\mingw64\x86_64-w64-mingw32\lib"; Flags: ignoreversion skipifsourcedoesntexist; Components: mingw
Source: "{#MinGWDir}\x86_64-w64-mingw32\lib\*.o"; DestDir: "{app}\mingw64\x86_64-w64-mingw32\lib"; Flags: ignoreversion skipifsourcedoesntexist; Components: mingw
Source: "{#MinGWDir}\x86_64-w64-mingw32\include\*"; DestDir: "{app}\mingw64\x86_64-w64-mingw32\include"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist; Components: mingw

[Icons]
Name: "{group}\XXML Command Prompt"; Filename: "{cmd}"; Parameters: "/k ""set PATH={app}\bin;{app}\mingw64\bin;%PATH% && echo XXML Compiler Ready && cd /d %USERPROFILE%"""; WorkingDir: "{userdocs}"
Name: "{group}\XXML Documentation"; Filename: "{app}\docs"; Components: docs
Name: "{group}\Uninstall XXML"; Filename: "{uninstallexe}"
Name: "{autodesktop}\XXML Command Prompt"; Filename: "{cmd}"; Parameters: "/k ""set PATH={app}\bin;{app}\mingw64\bin;%PATH% && echo XXML Compiler Ready"""; Tasks: desktopicon

[Registry]
; Add to PATH
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}\bin"; Tasks: addtopath; Check: NeedsAddPath('{app}\bin')
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}\mingw64\bin"; Tasks: addtopath; Components: mingw; Check: NeedsAddPath('{app}\mingw64\bin')

; XXML_HOME environment variable
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: string; ValueName: "XXML_HOME"; ValueData: "{app}"

[Run]
Filename: "{app}\bin\xxml.exe"; Parameters: "--version"; Description: "Verify installation"; Flags: postinstall nowait skipifsilent runhidden

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{8E5F4A2B-1C3D-4E5F-6A7B-8C9D0E1F2A3C}_is1';

// Get the uninstall string for a previous installation
function GetUninstallString(): string;
var
  UninstallString: string;
begin
  Result := '';
  if RegQueryStringValue(HKLM, UninstallKey, 'UninstallString', UninstallString) then
    Result := UninstallString
  else if RegQueryStringValue(HKCU, UninstallKey, 'UninstallString', UninstallString) then
    Result := UninstallString;
end;

// Check if a previous version is installed
function IsUpgrade(): Boolean;
begin
  Result := (GetUninstallString() <> '');
end;

// Uninstall previous version before installing new one
function UninstallPreviousVersion(): Integer;
var
  UninstallString: string;
  ResultCode: Integer;
begin
  Result := 0;
  UninstallString := GetUninstallString();

  if UninstallString <> '' then
  begin
    // Remove quotes if present
    if (Length(UninstallString) > 0) and (UninstallString[1] = '"') then
    begin
      Delete(UninstallString, 1, 1);
      Delete(UninstallString, Pos('"', UninstallString), 1);
    end;

    // Run the uninstaller silently
    if Exec(UninstallString, '/SILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      Result := ResultCode
    else
      Result := -1;
  end;
end;

// Called before installation begins
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;

  // Check if XXML is already installed
  if IsUpgrade() then
  begin
    if MsgBox('A previous version of XXML Compiler is installed. It will be uninstalled first.' + #13#10 + #13#10 +
              'Do you want to continue?', mbConfirmation, MB_YESNO) = IDYES then
    begin
      ResultCode := UninstallPreviousVersion();
      if ResultCode <> 0 then
      begin
        MsgBox('Failed to uninstall previous version (error code: ' + IntToStr(ResultCode) + ').' + #13#10 +
               'Please uninstall it manually and try again.', mbError, MB_OK);
        Result := False;
      end;
    end
    else
    begin
      Result := False;
    end;
  end;
end;

// Check if path needs to be added
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath)
  then begin
    Result := True;
    exit;
  end;
  // Look for the path with leading and trailing semicolon
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;

// Check for GCC after installation
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    // Notify environment change
    RegWriteStringValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'XXML_INSTALLED', '1');
  end;
end;

// Custom message on finish
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
  begin
    WizardForm.FinishedLabel.Caption :=
      'XXML Compiler has been installed on your computer.' + #13#10 + #13#10 +
      'Open a new command prompt and run:' + #13#10 +
      '    xxml --version' + #13#10 + #13#10 +
      'To compile your first program:' + #13#10 +
      '    xxml hello.XXML -o hello.exe';
  end;
end;

// Remove from PATH on uninstall
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path: string;
  AppPath: string;
  P: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if RegQueryStringValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'Path', Path) then
    begin
      AppPath := ExpandConstant('{app}\bin');
      P := Pos(';' + AppPath, Path);
      if P > 0 then
      begin
        Delete(Path, P, Length(AppPath) + 1);
        RegWriteStringValue(HKEY_LOCAL_MACHINE,
          'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
          'Path', Path);
      end;

      AppPath := ExpandConstant('{app}\mingw64\bin');
      P := Pos(';' + AppPath, Path);
      if P > 0 then
      begin
        Delete(Path, P, Length(AppPath) + 1);
        RegWriteStringValue(HKEY_LOCAL_MACHINE,
          'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
          'Path', Path);
      end;
    end;

    // Remove XXML_HOME
    RegDeleteValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'XXML_HOME');
    RegDeleteValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'XXML_INSTALLED');
  end;
end;

#ifndef AppVersion
  #error AppVersion must be supplied by build_installer_windows.ps1
#endif
#ifndef StageBin
  #error StageBin must be supplied by build_installer_windows.ps1
#endif
#ifndef SourceRoot
  #error SourceRoot must be supplied by build_installer_windows.ps1
#endif
#ifndef InstallerOutput
  #error InstallerOutput must be supplied by build_installer_windows.ps1
#endif

#define SuchName "Such"
#define SuchPublisher "Heritage Inc."

[Setup]
AppId={{6646D39A-AB12-4FA1-A889-CB2E51B3178C}
AppName={#SuchName}
AppVersion={#AppVersion}
AppVerName={#SuchName} v{#AppVersion}
AppPublisher={#SuchPublisher}
DefaultDirName={localappdata}\Programs\Such
DefaultGroupName=Heritage Inc
PrivilegesRequired=lowest
; Inno Setup 7: explicitly emit a native x64 setup EXE.
; SetupArchitecture also selects x64compatible install mode by default.
SetupArchitecture=x64
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Such.exe
WizardStyle=modern
SetupIconFile={#SourceRoot}\assets\icon\Such.ico
LicenseFile={#SourceRoot}\LICENSE
OutputDir={#InstallerOutput}
OutputBaseFilename=Such_v{#AppVersion}_Windows_x64_Setup
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "addpath"; Description: "Add Such CLI to the user PATH"; GroupDescription: "Command line:"; Flags: checkedonce

[Files]
Source: "{#StageBin}\Such.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageBin}\SuchCLI.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageBin}\SuchCLI.exe"; DestDir: "{app}\cli"; DestName: "such.exe"; Flags: ignoreversion
Source: "{#StageBin}\SuchRuntimePrivate.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageBin}\SuchRuntimePrivate.dll"; DestDir: "{app}\cli"; Flags: ignoreversion
Source: "{#SourceRoot}\LICENSE"; DestDir: "{app}\legal"; Flags: ignoreversion
Source: "{#SourceRoot}\NOTICE"; DestDir: "{app}\legal"; Flags: ignoreversion
Source: "{#SourceRoot}\runtime\linux-x64\licenses\TopoDB-Apache-2.0-LICENSE.txt"; DestDir: "{app}\legal"; Flags: ignoreversion
Source: "{#SourceRoot}\runtime\linux-x64\licenses\TopoDB-NOTICE.txt"; DestDir: "{app}\legal"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Heritage Inc\Such"; Filename: "{app}\Such.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Such"; Filename: "{app}\Such.exe"; Tasks: desktopicon; WorkingDir: "{app}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\Such.exe"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe"; ValueType: string; ValueName: "Path"; ValueData: "{app}"; Flags: uninsdeletevalue

[Code]
const
  PathRegistryKey = 'Environment';
  OwnershipKey = 'Software\Heritage Inc.\Such\Installer';

function NormalizeDir(const V: String): String;
begin
  Result := Lowercase(Trim(V));
  while (Length(Result) > 0) and (Result[Length(Result)] = '\') do
    Delete(Result, Length(Result), 1);
end;

function PathContains(const Value, Directory: String): Boolean;
var
  Rest, Entry: String;
  K: Integer;
begin
  Result := False;
  Rest := Value;
  repeat
    K := Pos(';', Rest);
    if K = 0 then begin Entry := Rest; Rest := ''; end
    else begin Entry := Copy(Rest, 1, K - 1); Delete(Rest, 1, K); end;
    if NormalizeDir(Entry) = NormalizeDir(Directory) then begin Result := True; Exit; end;
  until Rest = '';
end;

function RemovePathEntry(const Value, Directory: String): String;
var
  Rest, Entry: String;
  K: Integer;
begin
  Result := '';
  Rest := Value;
  repeat
    K := Pos(';', Rest);
    if K = 0 then begin Entry := Rest; Rest := ''; end
    else begin Entry := Copy(Rest, 1, K - 1); Delete(Rest, 1, K); end;
    if (Trim(Entry) <> '') and (NormalizeDir(Entry) <> NormalizeDir(Directory)) then begin
      if Result <> '' then Result := Result + ';';
      Result := Result + Entry;
    end;
  until Rest = '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  OldPath, CliDir: String;
begin
  if CurStep <> ssPostInstall then Exit;
  if not WizardIsTaskSelected('addpath') then Exit;
  CliDir := ExpandConstant('{app}\cli');
  if not RegQueryStringValue(HKCU, PathRegistryKey, 'Path', OldPath) then OldPath := '';
  if PathContains(OldPath, CliDir) then Exit;
  if OldPath <> '' then OldPath := OldPath + ';';
  if not RegWriteExpandStringValue(HKCU, PathRegistryKey, 'Path', OldPath + CliDir) then
    RaiseException('Could not update the user PATH.');
  RegWriteDWordValue(HKCU, OwnershipKey, 'OwnsCliPath', 1);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  OldPath, CliDir: String;
  Owns: Cardinal;
begin
  if CurUninstallStep <> usUninstall then Exit;
  if not RegQueryDWordValue(HKCU, OwnershipKey, 'OwnsCliPath', Owns) or (Owns <> 1) then Exit;
  CliDir := ExpandConstant('{app}\cli');
  if RegQueryStringValue(HKCU, PathRegistryKey, 'Path', OldPath) then
    RegWriteExpandStringValue(HKCU, PathRegistryKey, 'Path', RemovePathEntry(OldPath, CliDir));
  RegDeleteKeyIncludingSubkeys(HKCU, OwnershipKey);
end;

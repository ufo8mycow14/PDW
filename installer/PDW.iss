#ifndef Win32Application
  #error Win32Application must point to the staged Win32 application directory.
#endif
#ifndef X64Application
  #error X64Application must point to the staged x64 application directory.
#endif
#ifndef InstallerOutput
  #error InstallerOutput must point to the installer output directory.
#endif

#define AppIdValue "{{15A49948-1DBB-4F13-95C1-CB5CEB390E8B}"
#define AppName "PDW v5 2026 Release"
#define AppVersion "5.0.0"
#define AppExeName "PDW v5 2026 Release.exe"
#define SetupBaseName "PDW-v5-2026-Release-Setup"

[Setup]
AppId={#AppIdValue}
AppName={#AppName}
AppVerName={#AppName}
AppVersion={#AppVersion}
AppPublisher=PDW Open Source Project
AppPublisherURL=https://github.com/ufo8mycow14/PDW
AppSupportURL=https://github.com/ufo8mycow14/PDW/issues
AppUpdatesURL=https://github.com/ufo8mycow14/PDW/releases
DefaultDirName={localappdata}\Programs\PDW
DefaultGroupName=PDW
DisableProgramGroupPage=yes
LicenseFile={#SourcePath}\..\License
InfoBeforeFile={#SourcePath}\INSTALL_NOTICE.txt
OutputDir={#InstallerOutput}
OutputBaseFilename={#SetupBaseName}
SetupIconFile={#SourcePath}\..\GFX\pdw.ico
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern dynamic
PrivilegesRequired=lowest
ArchitecturesAllowed=x86compatible
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=yes
VersionInfoVersion=5.0.0.0
VersionInfoProductVersion=5.0.0.0
VersionInfoDescription=PDW v5 2026 Release Setup
VersionInfoProductName=PDW v5 2026 Release
VersionInfoCompany=PDW Open Source Project
#ifdef SignToolName
SignTool={#SignToolName}
SignedUninstaller=yes
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a &Desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "autostart"; Description: "Start PDW when I sign in to Windows"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
; Static application files are refreshed on upgrade. Mutable operator files,
; receivers and sounds are handled separately and never overwritten.
Source: "{#Win32Application}\*"; DestDir: "{app}"; Excludes: "PDW.INI,filters.ini,Receivers\*,Wavfiles\*"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: not InstallX64
Source: "{#X64Application}\*"; DestDir: "{app}"; Excludes: "PDW.INI,filters.ini,Receivers\*,Wavfiles\*"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: InstallX64

Source: "{#Win32Application}\PDW.INI"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall; Check: not InstallX64
Source: "{#X64Application}\PDW.INI"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall; Check: InstallX64
Source: "{#Win32Application}\filters.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall; Check: not InstallX64
Source: "{#X64Application}\filters.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall; Check: InstallX64

Source: "{#Win32Application}\Receivers\*"; DestDir: "{app}\Receivers"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs; Check: not InstallX64
Source: "{#X64Application}\Receivers\*"; DestDir: "{app}\Receivers"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs; Check: InstallX64
Source: "{#Win32Application}\Wavfiles\*"; DestDir: "{app}\Wavfiles"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs; Check: not InstallX64
Source: "{#X64Application}\Wavfiles\*"; DestDir: "{app}\Wavfiles"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs; Check: InstallX64

[Dirs]
Name: "{app}\Logfiles"; Flags: uninsneveruninstall

[Icons]
Name: "{autoprograms}\PDW"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\PDW"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userstartup}\PDW"; Filename: "{app}\{#AppExeName}"; Parameters: "/startup"; WorkingDir: "{app}"; Tasks: autostart

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch PDW v5 2026 Release"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
var
  ArchitecturePage: TWizardPage;
  X64Radio: TNewRadioButton;
  Win32Radio: TNewRadioButton;
  StorageLabel: TNewStaticText;
  RequestedArchitecture: String;

function InitializeSetup: Boolean;
begin
  RequestedArchitecture := Lowercase(Trim(ExpandConstant('{param:ARCH|}')));
  Result := (RequestedArchitecture = '') or
    (RequestedArchitecture = 'x64') or
    (RequestedArchitecture = 'win32') or
    (RequestedArchitecture = 'x86');
  if not Result then
    MsgBox('The /ARCH value must be x64 or Win32.', mbError, MB_OK)
  else if (RequestedArchitecture = 'x64') and (not IsWin64) then
  begin
    MsgBox('The x64 application cannot be installed on 32-bit Windows.',
      mbError, MB_OK);
    Result := False;
  end;
end;

function InstallX64: Boolean;
begin
  Result := IsWin64 and Assigned(X64Radio) and X64Radio.Checked;
end;

procedure InitializeWizard;
begin
  ArchitecturePage := CreateCustomPage(wpSelectDir,
    'Choose the PDW architecture',
    'Both choices are the same PDW product and use the same settings.');

  X64Radio := TNewRadioButton.Create(ArchitecturePage);
  X64Radio.Parent := ArchitecturePage.Surface;
  X64Radio.Left := 0;
  X64Radio.Top := 8;
  X64Radio.Width := ArchitecturePage.SurfaceWidth;
  X64Radio.Caption := '64-bit PDW (x64) - modern receivers, Windows audio and rtl_tcp';
  X64Radio.Checked := IsWin64 and
    ((RequestedArchitecture = '') or (RequestedArchitecture = 'x64'));
  X64Radio.Enabled := IsWin64;

  Win32Radio := TNewRadioButton.Create(ArchitecturePage);
  Win32Radio.Parent := ArchitecturePage.Surface;
  Win32Radio.Left := 0;
  Win32Radio.Top := X64Radio.Top + 34;
  Win32Radio.Width := ArchitecturePage.SurfaceWidth;
  Win32Radio.Caption := 'Win32 compatibility - legacy x86 receiver DLLs and older hardware';
  Win32Radio.Checked := (not IsWin64) or (RequestedArchitecture = 'win32') or
    (RequestedArchitecture = 'x86');

  StorageLabel := TNewStaticText.Create(ArchitecturePage);
  StorageLabel.Parent := ArchitecturePage.Surface;
  StorageLabel.Left := 0;
  StorageLabel.Top := Win32Radio.Top + 42;
  StorageLabel.Width := ArchitecturePage.SurfaceWidth;
  StorageLabel.Height := 46;
  StorageLabel.AutoSize := False;
  StorageLabel.WordWrap := True;
  StorageLabel.Caption :=
    'PDW.INI, filters, receivers, WAV files, logs and the application remain ' +
    'together in the selected PDW installation folder. Use PDW Backup / Restore ' +
    'after installation to move settings from another copy.';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = ArchitecturePage.ID) and (not IsWin64);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;

  if InstallX64 then
    SaveStringToFile(ExpandConstant('{app}\installation-architecture.txt'),
      'x64'#13#10, False)
  else
    SaveStringToFile(ExpandConstant('{app}\installation-architecture.txt'),
      'Win32'#13#10, False);
end;

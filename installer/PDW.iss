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
#define AppName "PDW v5.5.2 2026 Release"
#define AppVersion "5.5.2"
#define AppExeName "PDW v5.5.2 2026 Release.exe"
#define SetupBaseName "PDW-v5.5.2-2026-Release-Setup"

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
MinVersion=10.0.10586
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=yes
VersionInfoVersion=5.5.2.0
VersionInfoProductVersion=5.5.2.0
VersionInfoDescription=PDW v5.5.2 2026 Release Setup
VersionInfoProductName=PDW v5.5.2 2026 Release
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
Source: "{#Win32Application}\*"; DestDir: "{app}"; Excludes: "PDW.INI,PDW-Adelaide-FLEX.INI,PDW_INSTALLER_INPUT_SHA256SUMS.txt,filters.ini,Receivers\*,Wavfiles\*"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: not InstallX64
Source: "{#X64Application}\*"; DestDir: "{app}"; Excludes: "PDW.INI,PDW-Adelaide-FLEX.INI,PDW_INSTALLER_INPUT_SHA256SUMS.txt,filters.ini,Receivers\*,Wavfiles\*"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: InstallX64

Source: "{#Win32Application}\PDW.INI"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall; Check: (not InstallX64) and InstallStandardProfile
Source: "{#X64Application}\PDW.INI"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall; Check: InstallX64 and InstallStandardProfile
Source: "{#Win32Application}\PDW-Adelaide-FLEX.INI"; DestDir: "{app}"; DestName: "PDW.INI"; Flags: onlyifdoesntexist uninsneveruninstall; Check: (not InstallX64) and InstallAdelaideFlexProfile
Source: "{#X64Application}\PDW-Adelaide-FLEX.INI"; DestDir: "{app}"; DestName: "PDW.INI"; Flags: onlyifdoesntexist uninsneveruninstall; Check: InstallX64 and InstallAdelaideFlexProfile

Source: "{#Win32Application}\Receivers\*"; DestDir: "{app}\Receivers"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs; Check: not InstallX64
Source: "{#X64Application}\Receivers\*"; DestDir: "{app}\Receivers"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs; Check: InstallX64
Source: "{#Win32Application}\Wavfiles\*"; DestDir: "{app}\Wavfiles"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs; Check: not InstallX64
Source: "{#X64Application}\Wavfiles\*"; DestDir: "{app}\Wavfiles"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs; Check: InstallX64

[InstallDelete]
; The stable AppId upgrades prior 2026 releases in place. Remove only exact
; renamed predecessors; never wildcard operator files or other executables.
Type: files; Name: "{app}\PDW v5 2026 Release.exe"
Type: files; Name: "{app}\PDW v5.1 2026 Release.exe"
Type: files; Name: "{app}\PDW v5.2 2026 Release.exe"
Type: files; Name: "{app}\PDW v5.3 2026 Release.exe"
Type: files; Name: "{app}\PDW v5.4 2026 Release.exe"
Type: files; Name: "{app}\PDW v5.5 2026 Release.exe"
Type: files; Name: "{app}\PDW v5.5.1 2026 Release.exe"

; Runtime filenames are architecture-neutral. Remove the complete reviewed
; allowlist before copying the selected architecture so an x64-only runtime
; cannot survive a switch to Win32 (or vice versa).
Type: files; Name: "{app}\concrt140.dll"
Type: files; Name: "{app}\msvcp140.dll"
Type: files; Name: "{app}\msvcp140_1.dll"
Type: files; Name: "{app}\msvcp140_2.dll"
Type: files; Name: "{app}\msvcp140_atomic_wait.dll"
Type: files; Name: "{app}\msvcp140_codecvt_ids.dll"
Type: files; Name: "{app}\vcruntime140.dll"
Type: files; Name: "{app}\vcruntime140_1.dll"

[Dirs]
Name: "{app}\Logfiles"; Flags: uninsneveruninstall

[Icons]
Name: "{autoprograms}\PDW"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\PDW"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userstartup}\PDW"; Filename: "{app}\{#AppExeName}"; Parameters: "/startup"; WorkingDir: "{app}"; Tasks: autostart

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch PDW v5.5.2 2026 Release"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
var
  ArchitecturePage: TWizardPage;
  X64Radio: TNewRadioButton;
  Win32Radio: TNewRadioButton;
  StorageLabel: TNewStaticText;
  ProfilePage: TInputOptionWizardPage;
  RequestedArchitecture: String;
  RequestedProfile: String;
  PriorArchitecture: String;
  ArchitectureSelectionDirectory: String;
  ReceiverArchitectureBackup: String;
  ReceiverArchitectureOriginal: String;
  ReceiverPreservationPending: Boolean;
  ArchitectureMarkerDirectory: String;
  ArchitectureMarkerPath: String;
  ArchitectureMarkerBackup: String;
  ArchitectureMarkerHadPriorFile: Boolean;
  ArchitectureMarkerDirectoryCreated: Boolean;
  ArchitectureMarkerRollbackPending: Boolean;
  InstallationCompleted: Boolean;
  ForceReceiverPreservationFailure: Boolean;
  ForceArchitectureMarkerWriteFailure: Boolean;
  ForceFinalDirectoryRefreshTest: Boolean;

procedure ReportInitializationError(const MessageText: String);
begin
  Log(MessageText);
  if not WizardSilent then
    MsgBox(MessageText, mbError, MB_OK);
end;

function InitializeSetup: Boolean;
begin
  RequestedArchitecture := Lowercase(Trim(ExpandConstant('{param:ARCH|}')));
  RequestedProfile := Lowercase(Trim(ExpandConstant('{param:PROFILE|}')));
  ForceReceiverPreservationFailure :=
    Trim(ExpandConstant('{param:TESTFAILAFTERRECEIVERPRESERVATION|0}')) = '1';
  ForceArchitectureMarkerWriteFailure :=
    Trim(ExpandConstant('{param:TESTFAILARCHITECTUREMARKERWRITE|0}')) = '1';
  ForceFinalDirectoryRefreshTest :=
    Trim(ExpandConstant('{param:TESTFINALDIRARCHITECTUREREFRESH|0}')) = '1';
  if (RequestedArchitecture <> '') and
    (RequestedArchitecture <> 'x64') and
    (RequestedArchitecture <> 'win32') and
    (RequestedArchitecture <> 'x86') then
  begin
    ReportInitializationError('The /ARCH value must be x64 or Win32.');
    Result := False;
    Exit;
  end;
  if (RequestedProfile <> '') and
    (RequestedProfile <> 'standard') and
    (RequestedProfile <> 'adelaide-flex') then
  begin
    ReportInitializationError(
      'The /PROFILE value must be standard or adelaide-flex.');
    Result := False;
    Exit;
  end;
  if (RequestedArchitecture = 'x64') and (not IsWin64) then
  begin
    ReportInitializationError(
      'The x64 application cannot be installed on 32-bit Windows.');
    Result := False;
    Exit;
  end;
  Result := True;
end;

function FilesAreByteExact(const FirstPath, SecondPath: String): Boolean;
var
  FirstHash: String;
  SecondHash: String;
begin
  Result := False;
  if (not FileExists(FirstPath)) or (not FileExists(SecondPath)) then
    Exit;
  FirstHash := GetSHA256OfFile(FirstPath);
  SecondHash := GetSHA256OfFile(SecondPath);
  Result := (FirstHash <> '') and
    (CompareText(FirstHash, SecondHash) = 0);
end;

function RestoreReceiverAfterIncompleteInstall: Boolean;
begin
  Result := True;
  if not ReceiverPreservationPending then
    Exit;

  Result := FileExists(ReceiverArchitectureBackup) and
    CopyFile(ReceiverArchitectureBackup, ReceiverArchitectureOriginal, False) and
    FilesAreByteExact(ReceiverArchitectureBackup, ReceiverArchitectureOriginal);
  if not Result then
  begin
    Log('ERROR: Could not restore the preserved receiver DLL after incomplete installation. ' +
      'Recovery copy retained at: ' + ReceiverArchitectureBackup);
    Exit;
  end;

  Log('Restored cross-architecture receiver DLL after incomplete installation: ' +
    ReceiverArchitectureOriginal);
  if not DeleteFile(ReceiverArchitectureBackup) then
    Log('The temporary receiver recovery copy could not be removed after verified restore and was retained: ' +
      ReceiverArchitectureBackup);
  ReceiverArchitectureBackup := '';
  ReceiverArchitectureOriginal := '';
  ReceiverPreservationPending := False;
end;

function RestoreArchitectureMarkerAfterIncompleteInstall: Boolean;
begin
  Result := True;
  if not ArchitectureMarkerRollbackPending then
    Exit;

  if ArchitectureMarkerHadPriorFile then
  begin
    Result := FileExists(ArchitectureMarkerBackup) and
      CopyFile(ArchitectureMarkerBackup, ArchitectureMarkerPath, False) and
      FilesAreByteExact(ArchitectureMarkerBackup, ArchitectureMarkerPath);
  end
  else if FileExists(ArchitectureMarkerPath) then
    Result := DeleteFile(ArchitectureMarkerPath);

  if not Result then
  begin
    Log('ERROR: Could not restore the prior installation-architecture marker. ' +
      'Recovery copy retained at: ' + ArchitectureMarkerBackup);
    Exit;
  end;

  if ArchitectureMarkerHadPriorFile then
    Log('Restored installation-architecture marker after incomplete installation: ' +
      ArchitectureMarkerPath)
  else
    Log('Removed installation-architecture marker created by incomplete installation: ' +
      ArchitectureMarkerPath);

  if ArchitectureMarkerDirectoryCreated then
  begin
    if RemoveDir(ArchitectureMarkerDirectory) then
      Log('Removed empty installation directory created for the incomplete Setup: ' +
        ArchitectureMarkerDirectory)
    else
      Log('The installation directory was retained because it is not empty: ' +
        ArchitectureMarkerDirectory);
  end;

  if (ArchitectureMarkerBackup <> '') and FileExists(ArchitectureMarkerBackup) and
    (not DeleteFile(ArchitectureMarkerBackup)) then
    Log('The temporary architecture-marker recovery copy could not be removed after verified restore: ' +
      ArchitectureMarkerBackup);
  ArchitectureMarkerBackup := '';
  ArchitectureMarkerDirectory := '';
  ArchitectureMarkerDirectoryCreated := False;
  ArchitectureMarkerRollbackPending := False;
end;

function PrepareArchitectureMarkerRollback: Boolean;
var
  BackupCandidate: String;
  Suffix: Integer;
begin
  Result := False;
  ArchitectureMarkerDirectory := WizardDirValue;
  ArchitectureMarkerPath := AddBackslash(ArchitectureMarkerDirectory) +
    'installation-architecture.txt';
  ArchitectureMarkerBackup := '';
  ArchitectureMarkerDirectoryCreated := False;
  if not DirExists(ArchitectureMarkerDirectory) then
  begin
    if not ForceDirectories(ArchitectureMarkerDirectory) then
      Exit;
    ArchitectureMarkerDirectoryCreated := True;
  end;
  ArchitectureMarkerHadPriorFile := FileExists(ArchitectureMarkerPath);
  ArchitectureMarkerRollbackPending := True;
  if not ArchitectureMarkerHadPriorFile then
  begin
    Result := True;
    Exit;
  end;

  BackupCandidate := ArchitectureMarkerPath + '.pdw-setup-rollback.bak';
  Suffix := 2;
  while FileExists(BackupCandidate) do
  begin
    BackupCandidate := ArchitectureMarkerPath + '.pdw-setup-rollback-' +
      IntToStr(Suffix) + '.bak';
    Suffix := Suffix + 1;
  end;
  if not CopyFile(ArchitectureMarkerPath, BackupCandidate, False) then
  begin
    ArchitectureMarkerRollbackPending := False;
    Exit;
  end;
  if not FilesAreByteExact(ArchitectureMarkerPath, BackupCandidate) then
  begin
    DeleteFile(BackupCandidate);
    ArchitectureMarkerRollbackPending := False;
    Exit;
  end;
  ArchitectureMarkerBackup := BackupCandidate;
  Result := True;
end;

function SaveAndVerifyArchitectureMarker(
  const SelectedArchitecture: String): Boolean;
var
  MarkerContents: AnsiString;
begin
  Result := not ForceArchitectureMarkerWriteFailure;
  if Result then
    Result := SaveStringToFile(ArchitectureMarkerPath,
      SelectedArchitecture + #13#10, False);
  if Result then
    Result := LoadStringFromFile(ArchitectureMarkerPath, MarkerContents) and
      (CompareText(Trim(String(MarkerContents)), SelectedArchitecture) = 0);
  if not Result then
    Log('ERROR: PDW could not save and verify installation-architecture.txt.');
end;

procedure CompleteArchitectureMarkerTransaction;
begin
  if (ArchitectureMarkerBackup <> '') and FileExists(ArchitectureMarkerBackup) and
    (not DeleteFile(ArchitectureMarkerBackup)) then
    Log('The completed Setup retained a temporary architecture-marker recovery copy: ' +
      ArchitectureMarkerBackup);
  ArchitectureMarkerBackup := '';
  ArchitectureMarkerDirectory := '';
  ArchitectureMarkerDirectoryCreated := False;
  ArchitectureMarkerRollbackPending := False;
end;

function InstallX64: Boolean;
begin
  Result := IsWin64 and Assigned(X64Radio) and X64Radio.Checked;
end;

function InstallAdelaideFlexProfile: Boolean;
begin
  Result := Assigned(ProfilePage) and
    (ProfilePage.SelectedValueIndex = 1);
end;

function InstallStandardProfile: Boolean;
begin
  Result := not InstallAdelaideFlexProfile;
end;

function InstalledArchitecture: String;
var
  MarkerContents: AnsiString;
  MarkerPath: String;
begin
  Result := '';
  MarkerPath := AddBackslash(WizardDirValue) + 'installation-architecture.txt';
  if not LoadStringFromFile(MarkerPath, MarkerContents) then
    Exit;
  Result := Lowercase(Trim(String(MarkerContents)));
  if (Result <> 'x64') and (Result <> 'win32') then
  begin
    Log('Ignoring invalid prior architecture marker: ' + Result);
    Result := '';
  end;
end;

procedure RefreshArchitectureSelectionFromDirectory;
begin
  PriorArchitecture := InstalledArchitecture;
  X64Radio.Checked := IsWin64 and
    ((RequestedArchitecture = 'x64') or
      ((RequestedArchitecture = '') and (PriorArchitecture <> 'win32')));
  Win32Radio.Checked := not X64Radio.Checked;
  ArchitectureSelectionDirectory := WizardDirValue;
  Log('Architecture selection refreshed from final installation folder "' +
    WizardDirValue + '": ' + PriorArchitecture);
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
  X64Radio.Enabled := IsWin64;

  Win32Radio := TNewRadioButton.Create(ArchitecturePage);
  Win32Radio.Parent := ArchitecturePage.Surface;
  Win32Radio.Left := 0;
  Win32Radio.Top := X64Radio.Top + 34;
  Win32Radio.Width := ArchitecturePage.SurfaceWidth;
  Win32Radio.Caption := 'Win32 compatibility - legacy x86 receiver DLLs and older hardware';
  RefreshArchitectureSelectionFromDirectory;

  StorageLabel := TNewStaticText.Create(ArchitecturePage);
  StorageLabel.Parent := ArchitecturePage.Surface;
  StorageLabel.Left := 0;
  StorageLabel.Top := Win32Radio.Top + 42;
  StorageLabel.Width := ArchitecturePage.SurfaceWidth;
  StorageLabel.Height := 46;
  StorageLabel.AutoSize := False;
  StorageLabel.WordWrap := True;
  StorageLabel.Caption :=
    'PDW.INI, the Capcode Directory, receivers, WAV files, logs and the application remain ' +
    'together in the selected PDW installation folder. Use PDW Backup / Restore ' +
    'after installation to move settings from another copy.';

  ProfilePage := CreateInputOptionPage(ArchitecturePage.ID,
    'Choose the initial PDW settings profile',
    'This choice applies only when PDW.INI does not already exist.',
    'SDR# and VB-Audio Cable are external products; Setup does not install or configure them.',
    True, False);
  ProfilePage.Add('Standard PDW settings (recommended for most users)');
  ProfilePage.Add('SDR# + VB-Audio Cable (Adelaide FLEX)');
  if RequestedProfile = 'adelaide-flex' then
    ProfilePage.SelectedValueIndex := 1
  else
    ProfilePage.SelectedValueIndex := 0;

  if ForceFinalDirectoryRefreshTest and IsWin64 and
    (RequestedArchitecture = '') then
  begin
    { Deterministic smoke setup: emulate a default x64 selection made before
      the operator browses to the final directory. PrepareToInstall invokes
      the real page-entry handler below after /DIR has supplied the final path. }
    X64Radio.Checked := True;
    Win32Radio.Checked := False;
    ArchitectureSelectionDirectory := WizardDirValue + '.pdw-smoke-initial';
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = ArchitecturePage.ID) and
    (RequestedArchitecture = '') and
    (CompareText(ArchitectureSelectionDirectory, WizardDirValue) <> 0) then
    RefreshArchitectureSelectionFromDirectory;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  BackupCandidate: String;
  ExistingReceiver: String;
  SelectedArchitecture: String;
  Suffix: Integer;
begin
  Result := '';
  ReceiverArchitectureBackup := '';
  ReceiverArchitectureOriginal := '';
  ReceiverPreservationPending := False;
  { WizardDirValue can change after InitializeWizard, so the final selected
    destination is authoritative for architecture-switch preservation. }
  if ForceFinalDirectoryRefreshTest then
    CurPageChanged(ArchitecturePage.ID);
  PriorArchitecture := InstalledArchitecture;
  if ArchitectureMarkerRollbackPending and
    (not RestoreArchitectureMarkerAfterIncompleteInstall) then
  begin
    Result := 'PDW could not restore the prior architecture marker before retrying Setup.';
    Exit;
  end;
  if not PrepareArchitectureMarkerRollback then
  begin
    Result := 'PDW could not create and byte-verify the architecture-marker rollback copy.';
    Exit;
  end;
  if InstallX64 then
    SelectedArchitecture := 'x64'
  else
    SelectedArchitecture := 'win32';
  if not SaveAndVerifyArchitectureMarker(SelectedArchitecture) then
  begin
    if RestoreArchitectureMarkerAfterIncompleteInstall then
      Result := 'PDW could not save and verify installation-architecture.txt. ' +
        'Setup stopped before application files were changed.'
    else
      Result := 'PDW could not save installation-architecture.txt or restore its prior contents. ' +
        'The recovery copy was retained at: ' + ArchitectureMarkerBackup;
    Exit;
  end;
  if (PriorArchitecture = '') or (PriorArchitecture = SelectedArchitecture) then
    Exit;

  ExistingReceiver := AddBackslash(WizardDirValue) + 'Receivers\RTL-SDR\rtlsdr.dll';
  if not FileExists(ExistingReceiver) then
    Exit;
  BackupCandidate := ExistingReceiver + '.pre-' + PriorArchitecture + '-architecture.bak';
  Suffix := 2;
  while FileExists(BackupCandidate) do
  begin
    BackupCandidate := ExistingReceiver + '.pre-' + PriorArchitecture +
      '-architecture-' + IntToStr(Suffix) + '.bak';
    Suffix := Suffix + 1;
  end;
  if not CopyFile(ExistingReceiver, BackupCandidate, False) then
  begin
    if RestoreArchitectureMarkerAfterIncompleteInstall then
      Result := 'PDW could not preserve the existing RTL-SDR receiver DLL before changing architecture. ' +
        'No architecture change was made. Choose the previous architecture or move the DLL manually.'
    else
      Result := 'PDW could not preserve the RTL-SDR receiver DLL or restore the prior architecture marker. ' +
        'The marker recovery copy was retained at: ' + ArchitectureMarkerBackup;
    Exit;
  end;
  if not FilesAreByteExact(ExistingReceiver, BackupCandidate) then
  begin
    DeleteFile(BackupCandidate);
    if RestoreArchitectureMarkerAfterIncompleteInstall then
      Result := 'PDW could not byte-verify the receiver-DLL backup before changing architecture.'
    else
      Result := 'PDW could not verify the receiver-DLL backup or restore the prior architecture marker. ' +
        'The marker recovery copy was retained at: ' + ArchitectureMarkerBackup;
    Exit;
  end;
  if not DeleteFile(ExistingReceiver) then
  begin
    if FilesAreByteExact(ExistingReceiver, BackupCandidate) then
      DeleteFile(BackupCandidate);
    if RestoreArchitectureMarkerAfterIncompleteInstall then
      Result := 'PDW preserved the receiver DLL as ' + BackupCandidate +
        ' but could not remove the incompatible active copy. Close programs using it and try again.'
    else
      Result := 'PDW could not remove the incompatible receiver DLL or restore the prior architecture marker. ' +
        'Recovery copies remain at: ' + BackupCandidate + ' and ' + ArchitectureMarkerBackup;
    Exit;
  end;
  ReceiverArchitectureBackup := BackupCandidate;
  ReceiverArchitectureOriginal := ExistingReceiver;
  ReceiverPreservationPending := True;
  Log('Preserved cross-architecture receiver DLL as: ' + ReceiverArchitectureBackup);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := ((PageID = ArchitecturePage.ID) and (not IsWin64)) or
    ((PageID = ProfilePage.ID) and
      FileExists(AddBackslash(WizardDirValue) + 'PDW.INI'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    if ForceReceiverPreservationFailure and ReceiverPreservationPending then
      RaiseException('PDW installer smoke forced failure after receiver preservation.');
    Exit;
  end;

  if CurStep = ssDone then
  begin
    CompleteArchitectureMarkerTransaction;
    InstallationCompleted := True;
    Exit;
  end;
end;

procedure DeinitializeSetup;
var
  MarkerRestored: Boolean;
  ReceiverRestored: Boolean;
begin
  if ReceiverPreservationPending and (not InstallationCompleted) then
  begin
    ReceiverRestored := RestoreReceiverAfterIncompleteInstall;
    if (not ReceiverRestored) and (not WizardSilent) then
      MsgBox('PDW could not restore the preserved RTL-SDR receiver DLL after Setup stopped.' + #13#10#13#10 +
        'The verified recovery copy remains at:' + #13#10 +
        ReceiverArchitectureBackup, mbError, MB_OK);
  end;
  if ArchitectureMarkerRollbackPending and (not InstallationCompleted) then
  begin
    MarkerRestored := RestoreArchitectureMarkerAfterIncompleteInstall;
    if (not MarkerRestored) and (not WizardSilent) then
      MsgBox('PDW could not restore the prior installation-architecture marker after Setup stopped.' + #13#10#13#10 +
        'The recovery copy remains at:' + #13#10 +
        ArchitectureMarkerBackup, mbError, MB_OK);
  end;
end;

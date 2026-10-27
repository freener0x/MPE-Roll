; MPE Roll's Windows installer, built with Inno Setup 6 by the Windows CI job (see
; .github/workflows/build.yml). To build it by hand from a CI download or a dist folder:
;   iscc /DDistDir=C:\path\to\dist /DBuildNumber=0 installer\MpeRoll.iss
;
; It installs the VST3 where every host looks, the VST2 .dll (with the scanner beside it) in a
; folder the user picks, and the standalone app. Before installing it looks for other copies of
; MPE Roll in the plugin folders and offers to remove them: FL Studio loads whichever copy a project
; was saved with, so an old copy left behind keeps loading an old build.
;
; Silent install, as the CI test runs it:
;   MPE-Roll-Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /COMPONENTS=vst3,vst2,app
;                      /VST2DIR="C:\Program Files\Common Files\VST2"
; A silent install removes every older copy it finds, unless /KEEPOLDCOPIES is given.

#ifndef DistDir
  #define DistDir "..\dist"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef BuildNumber
  #define BuildNumber "0"
#endif
#ifndef BuildName
  #define BuildName "local build"
#endif
#ifndef OutputName
  #define OutputName "MPE-Roll-Setup"
#endif

[Setup]
; Never change the AppId: upgrades and the uninstaller find earlier installs by it.
AppId={{74C456C0-14CD-4836-A865-C9B0FF60BB0B}
AppName=MPE Roll
AppVersion={#AppVersion}
AppVerName=MPE Roll {#AppVersion} ({#BuildName})
AppPublisher=Glowing Pancake
AppPublisherURL=https://freener0x.github.io/glowing-pancake/
VersionInfoVersion={#AppVersion}.{#BuildNumber}
VersionInfoDescription=MPE Roll installer
DefaultDirName={autopf}\MPE Roll
DisableProgramGroupPage=yes
DisableDirPage=yes
LicenseFile=..\LICENSE
OutputDir=output
OutputBaseFilename={#OutputName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The VST3 goes to Common Files, which only an administrator can write to.
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={uninstallexe}
UninstallDisplayName=MPE Roll
; A DAW that has MPE Roll loaded holds its files: Setup names the programs and offers to close them.
; The VST3's own binary ends in .vst3.
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll,*.vst3
RestartApplications=no
SetupLogging=yes

[Types]
Name: "full"; Description: "VST3, VST2 and the standalone app"
Name: "vst3only"; Description: "VST3 only"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (in Common Files\VST3, where FL Studio looks)"; Types: full vst3only
Name: "vst2"; Description: "VST2 plugin, MPE Roll.dll (in a folder you choose)"; Types: full
Name: "app"; Description: "Standalone app"; Types: full

[InstallDelete]
; Files a newer build no longer has would otherwise stay inside the bundle.
Type: filesandordirs; Name: "{commoncf64}\VST3\MPE Roll.vst3"; Components: vst3

[Files]
Source: "{#DistDir}\MPE Roll.vst3\*"; DestDir: "{commoncf64}\VST3\MPE Roll.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DistDir}\MPE Roll.dll"; DestDir: "{code:GetVst2Dir}"; Components: vst2; Flags: ignoreversion
; MPE Roll looks for its scanner next to its own binary (and in the installed VST3).
Source: "{#DistDir}\MPE Roll Scanner.exe"; DestDir: "{code:GetVst2Dir}"; Components: vst2; Flags: ignoreversion
Source: "{#DistDir}\MPE Roll.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
; Lets a crash report name the code involved.
Source: "{#DistDir}\MPE Roll.pdb"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#DistDir}\MPE Roll Scanner.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\MPE Roll.vst3"; Components: vst3

[Icons]
Name: "{autoprograms}\MPE Roll"; Filename: "{app}\MPE Roll.exe"; Components: app

[Run]
Filename: "{app}\MPE Roll.exe"; Description: "Open the MPE Roll app"; Components: app; Flags: postinstall nowait skipifsilent unchecked

[Code]
const
  // FILE_ATTRIBUTE_REPARSE_POINT: a link or junction.
  ReparsePoint = $400;
  // How far below a plugin folder to look for older copies.
  SearchDepth = 4;

var
  Vst2Page: TInputDirWizardPage;
  OldCopiesPage: TInputOptionWizardPage;
  OldCopies: TArrayOfString;
  NotRemoved: String;

// ---------------------------------------------------------------------------------------------
// The VST2 folder

function DefaultVst2Dir: String;
var
  Folder: String;
begin
  // A folder given on the command line, then the one chosen last time, then the folder Steinberg's
  // convention names in the registry, then the common one.
  Result := ExpandConstant('{param:VST2DIR|}');
  if Result <> '' then
    exit;

  Result := GetPreviousData('VST2Folder', '');
  if Result <> '' then
    exit;

  if RegQueryStringValue(HKLM64, 'SOFTWARE\VST', 'VSTPluginsPath', Folder) and (Trim(Folder) <> '') then
    Result := RemoveBackslashUnlessRoot(Trim(Folder))
  else
    Result := ExpandConstant('{commoncf64}\VST2');
end;

function GetVst2Dir(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Trim(Vst2Page.Values[0]));
end;

// ---------------------------------------------------------------------------------------------
// Older copies

procedure AddUnique(var List: TArrayOfString; const Path: String);
var
  I: Integer;
begin
  for I := 0 to GetArrayLength(List) - 1 do
    if CompareText(List[I], Path) = 0 then
      exit;

  SetArrayLength(List, GetArrayLength(List) + 1);
  List[GetArrayLength(List) - 1] := Path;
end;

// Adds every 'MPE Roll.vst3' and 'MPE Roll.dll' in Dir and the folders below it.
procedure FindCopies(const Dir: String; Depth: Integer; var Found: TArrayOfString);
var
  FindRec: TFindRec;
  Path: String;
  IsDir: Boolean;
begin
  if not FindFirst(AddBackslash(Dir) + '*', FindRec) then
    exit;

  try
    repeat
      if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
      begin
        Path := AddBackslash(Dir) + FindRec.Name;
        IsDir := (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0;

        if (CompareText(FindRec.Name, 'MPE Roll.vst3') = 0)
           or ((not IsDir) and (CompareText(FindRec.Name, 'MPE Roll.dll') = 0)) then
          AddUnique(Found, Path)
        // Not into links, which can loop, nor into other plugins' bundles.
        else if IsDir and (Depth > 0)
                and ((FindRec.Attributes and ReparsePoint) = 0)
                and (CompareText(ExtractFileExt(FindRec.Name), '.vst3') <> 0) then
          FindCopies(Path, Depth - 1, Found);
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

// Copies this install doesn't replace, in the folders FL Studio and other hosts search.
procedure FindOldCopies(var Copies: TArrayOfString);
var
  Roots, Found: TArrayOfString;
  Folder, Vst3Path, Vst2Path: String;
  I: Integer;
begin
  SetArrayLength(Copies, 0);
  SetArrayLength(Roots, 0);
  SetArrayLength(Found, 0);
  AddUnique(Roots, ExpandConstant('{commoncf64}\VST3'));
  AddUnique(Roots, ExpandConstant('{commoncf64}\VST2'));
  AddUnique(Roots, ExpandConstant('{commoncf64}\Steinberg\VST2'));
  AddUnique(Roots, ExpandConstant('{commonpf64}\VSTPlugins'));
  AddUnique(Roots, ExpandConstant('{commonpf64}\Steinberg\VSTPlugins'));

  if RegQueryStringValue(HKLM64, 'SOFTWARE\VST', 'VSTPluginsPath', Folder) and (Trim(Folder) <> '') then
    AddUnique(Roots, RemoveBackslashUnlessRoot(Trim(Folder)));

  Folder := GetPreviousData('VST2Folder', '');
  if Folder <> '' then
    AddUnique(Roots, Folder);

  if WizardIsComponentSelected('vst2') then
    AddUnique(Roots, GetVst2Dir(''));

  for I := 0 to GetArrayLength(Roots) - 1 do
    if DirExists(Roots[I]) then
      FindCopies(Roots[I], SearchDepth, Found);

  // What this install puts back isn't an older copy.
  Vst3Path := ExpandConstant('{commoncf64}\VST3\MPE Roll.vst3');
  Vst2Path := AddBackslash(GetVst2Dir('')) + 'MPE Roll.dll';

  for I := 0 to GetArrayLength(Found) - 1 do
    if not ((WizardIsComponentSelected('vst3') and (CompareText(Found[I], Vst3Path) = 0))
            or (WizardIsComponentSelected('vst2') and (CompareText(Found[I], Vst2Path) = 0))) then
      AddUnique(Copies, Found[I]);
end;

function SameCopies(const A, B: TArrayOfString): Boolean;
var
  I: Integer;
begin
  Result := False;

  if GetArrayLength(A) <> GetArrayLength(B) then
    exit;

  for I := 0 to GetArrayLength(A) - 1 do
    if CompareText(A[I], B[I]) <> 0 then
      exit;

  Result := True;
end;

// A switch given on the command line, like /KEEPOLDCOPIES.
function HasSwitch(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;

  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/' + Name) = 0 then
      Result := True;
end;

function RemoveCopy(const Path: String): Boolean;
var
  Scanner: String;
begin
  if DirExists(Path) then
    Result := DelTree(Path, True, True, True)
  else
  begin
    Result := DeleteFile(Path);

    // The scanner beside an old .dll belongs to it.
    Scanner := AddBackslash(ExtractFileDir(Path)) + 'MPE Roll Scanner.exe';
    if Result and FileExists(Scanner) then
      DeleteFile(Scanner);
  end;

  if Result then
    Log('Removed the older copy ' + Path)
  else
    Log('Could not remove the older copy ' + Path);
end;

procedure RemoveOldCopies;
var
  I: Integer;
  Chosen: Boolean;
begin
  if WizardSilent then
  begin
    if HasSwitch('KEEPOLDCOPIES') then
      exit;
    FindOldCopies(OldCopies);
  end;

  for I := 0 to GetArrayLength(OldCopies) - 1 do
  begin
    // A silent install has no page to tick them on.
    if WizardSilent then
      Chosen := True
    else
      Chosen := OldCopiesPage.Values[I];

    if Chosen and not RemoveCopy(OldCopies[I]) then
      NotRemoved := NotRemoved + #13#10 + '    ' + OldCopies[I];
  end;
end;

// ---------------------------------------------------------------------------------------------
// FL Studio running

function IsRunning(const ExeName: String): Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = "' + ExeName + '"');
    Result := Processes.Count > 0;
  except
    Log('Could not tell whether ' + ExeName + ' is running: ' + GetExceptionMessage);
  end;
end;

function FlStudioRunning: Boolean;
begin
  Result := IsRunning('FL64.exe') or IsRunning('FL.exe');
end;

// ---------------------------------------------------------------------------------------------
// Wizard

procedure InitializeWizard;
begin
  Vst2Page := CreateInputDirPage(wpSelectComponents,
    'Where to put the VST2 plugin',
    'MPE Roll.dll goes in a folder FL Studio searches for plugins.',
    'Pick one of the folders listed in FL Studio under Options > Manage plugins (the VST3 needs no ' +
    'folder: it goes to Common Files\VST3). MPE Roll Scanner.exe is put beside MPE Roll.dll.',
    False, '');
  Vst2Page.Add('VST2 plugin folder:');
  Vst2Page.Values[0] := DefaultVst2Dir;

  OldCopiesPage := CreateInputOptionPage(Vst2Page.ID,
    'Older copies of MPE Roll',
    'These copies are in folders that FL Studio searches for plugins.',
    'FL Studio loads whichever copy a project was saved with, so an older copy keeps loading an ' +
    'older build. Ticked copies are removed. A project saved with a removed copy will say the ' +
    'plugin is missing: install the same kind (VST3 or VST2) to keep such projects working.',
    False, False);
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if WizardIsComponentSelected('vst2') then
    SetPreviousData(PreviousDataKey, 'VST2Folder', GetVst2Dir(''));
end;

function ShouldSkipPage(PageID: Integer): Boolean;
var
  Copies: TArrayOfString;
  I: Integer;
begin
  Result := False;

  if PageID = Vst2Page.ID then
    Result := not WizardIsComponentSelected('vst2')
  else if PageID = OldCopiesPage.ID then
  begin
    // Looked for again whenever the page comes up, since the choices before it change what this
    // install replaces. The same list keeps the user's ticks.
    FindOldCopies(Copies);

    if not SameCopies(Copies, OldCopies) then
    begin
      OldCopies := Copies;
      OldCopiesPage.CheckListBox.Items.Clear;

      for I := 0 to GetArrayLength(OldCopies) - 1 do
        OldCopiesPage.Add(OldCopies[I]);

      for I := 0 to GetArrayLength(OldCopies) - 1 do
        OldCopiesPage.Values[I] := True;
    end;

    Result := GetArrayLength(OldCopies) = 0;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if (CurPageID = Vst2Page.ID) and (GetVst2Dir('') = '') then
  begin
    MsgBox('Choose a folder for the VST2 plugin.', mbError, MB_OK);
    Result := False;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  Removed: String;
  I: Integer;
begin
  Result := MemoComponentsInfo;

  if WizardIsComponentSelected('vst2') then
    Result := Result + NewLine + NewLine + 'VST2 plugin folder:' + NewLine + Space + GetVst2Dir('');

  for I := 0 to GetArrayLength(OldCopies) - 1 do
    if OldCopiesPage.Values[I] then
      Removed := Removed + NewLine + Space + OldCopies[I];

  if Removed <> '' then
    Result := Result + NewLine + NewLine + 'Older copies to remove:' + Removed;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';

  // Windows can't replace or remove a plugin file FL Studio has loaded.
  while FlStudioRunning do
  begin
    Result := 'FL Studio is running. Close it, then run Setup again.';

    if WizardSilent then
      exit;

    if MsgBox('FL Studio is running, and Windows can''t replace or remove a plugin it has loaded.' + #13#10#13#10 +
              'Save your project, close FL Studio, then click Retry.', mbInformation, MB_RETRYCANCEL) = IDCANCEL then
      exit;

    Result := '';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    RemoveOldCopies
  else if (CurStep = ssPostInstall) and (NotRemoved <> '') then
  begin
    Log('Older copies left in place:' + NotRemoved);
    if not WizardSilent then
      MsgBox('These older copies couldn''t be removed (a program may have them open):' + NotRemoved + #13#10#13#10 +
             'Close FL Studio and delete them by hand.', mbInformation, MB_OK);
  end;
end;

; MPE Roll's Windows installer, built with Inno Setup 6 by the Windows CI job (see
; .github/workflows/build.yml). To build it by hand from a CI download or a dist folder:
;   iscc /DDistDir=C:\path\to\dist /DBuildNumber=0 installer\MpeRoll.iss
;
; It installs the VST3 where every host looks, the VST2 .dll (with the scanner beside it) in a
; folder the user picks, and the standalone app. Before installing it looks for other copies of
; MPE Roll in the plugin folders and in FL Studio's plugin database, and offers to remove them: FL
; loads whichever copy a project was saved with, so an old copy left behind keeps loading an old
; build.
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
; Always installed, and first: older copies are removed just before it, once programs holding them
; have been closed (see RemoveOldCopies).
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion; BeforeInstall: RemoveOldCopies
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
  // How far below a plugin folder to look for older copies: the standard folders, then folders the
  // user or the registry named (which may be a whole drive's root, looked at only at its top).
  SearchDepth = 4;
  ChosenFolderDepth = 2;

var
  Vst2Page: TInputDirWizardPage;
  OldCopiesPage: TInputOptionWizardPage;
  OldCopies: TArrayOfString;            // listed on OldCopiesPage
  RescanCopies: Boolean;                // look for them again when the page next comes up
  ChosenCopies: TArrayOfString;         // the ones to remove, settled once the wizard is done
  CopiesChosen, CopiesRemoved: Boolean;
  NotRemoved: String;

// ---------------------------------------------------------------------------------------------
// The VST2 folder

function DefaultVst2Dir: String;
begin
  // A folder given on the command line, then the one chosen last time, then the folder FL Studio's
  // manual names for 64-bit VST2 plugins (which Steinberg recommends too).
  Result := ExpandConstant('{param:VST2DIR|}');
  if Result <> '' then
    exit;

  Result := GetPreviousData('VST2Folder', '');
  if Result <> '' then
    exit;

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

// FL Studio's user data folder, which holds its plugin database. Image-Line doesn't document the
// registry value; other tools read it the same way.
function FlUserDataDir: String;
begin
  Result := '';

  if not RegQueryStringValue(HKCU, 'SOFTWARE\Image-Line\Shared\Paths', 'Shared data', Result) then
    if not RegQueryStringValue(HKLM, 'SOFTWARE\Image-Line\Shared\Paths', 'Shared data', Result) then
      Result := '';

  Result := Trim(Result);

  if Result = '' then
    Result := ExpandConstant('{userdocs}\Image-Line');

  Result := RemoveBackslashUnlessRoot(Result);
end;

// The copies of MPE Roll that FL Studio's plugin database lists, wherever they are: these are the
// ones FL can load, from its own search folders as well as the user's.
procedure FindCopiesFlKnows(var Found: TArrayOfString);
var
  Database, Folder, Line, Path: String;
  Lines: TArrayOfString;
  FindRec: TFindRec;
  Kind, I, Cut: Integer;
begin
  // Each plugin's .nfo has a line ps_file_filename_N=<path> for every file FL found it in. For a
  // VST3 bundle that's the binary inside it.
  Database := FlUserDataDir + '\FL Studio\Presets\Plugin database\Installed\Generators\';

  for Kind := 0 to 1 do
  begin
    if Kind = 0 then
      Folder := Database + 'VST'
    else
      Folder := Database + 'VST3';

    if FindFirst(Folder + '\*.nfo', FindRec) then
    begin
      try
        repeat
          if LoadStringsFromFile(Folder + '\' + FindRec.Name, Lines) then
            for I := 0 to GetArrayLength(Lines) - 1 do
            begin
              Line := Lines[I];

              if (Pos('ps_file_filename_', Line) = 1) and (Pos('=', Line) > 0) then
              begin
                Path := Trim(Copy(Line, Pos('=', Line) + 1, Length(Line)));
                Cut := Pos('\MPE ROLL.VST3\', Uppercase(Path) + '\');

                if Cut > 0 then
                  Path := Copy(Path, 1, Cut + Length('\MPE Roll.vst3') - 1)
                else if CompareText(ExtractFileName(Path), 'MPE Roll.dll') <> 0 then
                  Path := '';

                // Paths under FL's own folder start with %FLPath%, which isn't documented: skipped.
                if (Path <> '') and (Pos('%', Path) = 0) then
                  if FileExists(Path) or DirExists(Path) then
                    AddUnique(Found, Path);
              end;
            end;
        until not FindNext(FindRec);
      finally
        FindClose(FindRec);
      end;
    end;
  end;
end;

// Copies this install doesn't replace, in the folders FL Studio and other hosts search.
procedure FindOldCopies(var Copies: TArrayOfString);
var
  Roots, Chosen, Found: TArrayOfString;
  Folder, Vst3Path, Vst2Path: String;
  I: Integer;
begin
  SetArrayLength(Copies, 0);
  SetArrayLength(Roots, 0);
  SetArrayLength(Chosen, 0);
  SetArrayLength(Found, 0);
  AddUnique(Roots, ExpandConstant('{commoncf64}\VST3'));
  AddUnique(Roots, ExpandConstant('{commonpf64}\VST3'));
  AddUnique(Roots, ExpandConstant('{commoncf64}\VST2'));
  AddUnique(Roots, ExpandConstant('{commoncf64}\Steinberg\VST2'));
  AddUnique(Roots, ExpandConstant('{commonpf64}\VSTPlugins'));
  AddUnique(Roots, ExpandConstant('{commonpf64}\Steinberg\VSTPlugins'));

  if RegQueryStringValue(HKLM64, 'SOFTWARE\VST', 'VSTPluginsPath', Folder) and (Trim(Folder) <> '') then
    AddUnique(Chosen, RemoveBackslashUnlessRoot(Trim(Folder)));

  Folder := GetPreviousData('VST2Folder', '');
  if Folder <> '' then
    AddUnique(Chosen, Folder);

  if WizardIsComponentSelected('vst2') then
    AddUnique(Chosen, GetVst2Dir(''));

  for I := 0 to GetArrayLength(Roots) - 1 do
    if DirExists(Roots[I]) then
      FindCopies(Roots[I], SearchDepth, Found);

  // A drive's root (D:\ as a plugin drive, say) only at its top.
  for I := 0 to GetArrayLength(Chosen) - 1 do
    if DirExists(Chosen[I]) then
    begin
      if Length(RemoveBackslash(Chosen[I])) <= 2 then
        FindCopies(Chosen[I], 0, Found)
      else
        FindCopies(Chosen[I], ChosenFolderDepth, Found);
    end;

  FindCopiesFlKnows(Found);

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

// The scanner that goes with an old MPE Roll.dll, in the same folder.
function ScannerBeside(const Path: String): String;
begin
  Result := '';

  if CompareText(ExtractFileName(Path), 'MPE Roll.dll') = 0 then
    Result := AddBackslash(ExtractFileDir(Path)) + 'MPE Roll Scanner.exe';
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
    Scanner := ScannerBeside(Path);

    if Result and (Scanner <> '') and FileExists(Scanner) then
      DeleteFile(Scanner);
  end;

  if Result then
    Log('Removed the older copy ' + Path)
  else
    Log('Could not remove the older copy ' + Path);
end;

// Settles which copies go, once the wizard is done: those left ticked or, in a silent install,
// every one found (unless /KEEPOLDCOPIES).
procedure ChooseCopies;
var
  I: Integer;
begin
  if CopiesChosen then
    exit;

  CopiesChosen := True;
  SetArrayLength(ChosenCopies, 0);

  if WizardSilent then
  begin
    if not HasSwitch('KEEPOLDCOPIES') then
      FindOldCopies(ChosenCopies);
  end
  else
    for I := 0 to GetArrayLength(OldCopies) - 1 do
      if OldCopiesPage.Values[I] then
        AddUnique(ChosenCopies, OldCopies[I]);
end;

// So that Restart Manager names (and offers to close) a program holding an old copy open, as it
// does for the files this install replaces.
procedure RegisterExtraCloseApplicationsResources;
var
  I: Integer;
  Path: String;
begin
  ChooseCopies;

  for I := 0 to GetArrayLength(ChosenCopies) - 1 do
  begin
    Path := ChosenCopies[I];

    if DirExists(Path) then
    begin
      RegisterExtraCloseApplicationsResource(False, AddBackslash(Path) + 'Contents\x86_64-win\MPE Roll.vst3');
      RegisterExtraCloseApplicationsResource(False, AddBackslash(Path) + 'Contents\x86_64-win\MPE Roll Scanner.exe');
    end
    else
    begin
      RegisterExtraCloseApplicationsResource(False, Path);

      if ScannerBeside(Path) <> '' then
        RegisterExtraCloseApplicationsResource(False, ScannerBeside(Path));
    end;
  end;
end;

// Run as the first file is installed (BeforeInstall in [Files]): after Restart Manager has closed
// the programs the user let it close, which happens later than ssInstall.
procedure RemoveOldCopies;
var
  I: Integer;
begin
  if CopiesRemoved then
    exit;

  CopiesRemoved := True;
  ChooseCopies;

  for I := 0 to GetArrayLength(ChosenCopies) - 1 do
    if not RemoveCopy(ChosenCopies[I]) then
      NotRemoved := NotRemoved + #13#10 + '    ' + ChosenCopies[I];
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

  RescanCopies := True;
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
  // Setup rewrites what it keeps on every install, so a folder not used this time is kept too: the
  // next install still looks there for an older MPE Roll.dll.
  if WizardIsComponentSelected('vst2') then
    SetPreviousData(PreviousDataKey, 'VST2Folder', GetVst2Dir(''))
  else
    SetPreviousData(PreviousDataKey, 'VST2Folder', GetPreviousData('VST2Folder', ''));
end;

// Lists the copies on OldCopiesPage, ticked except those the user unticked before.
procedure ShowCopies(const Copies: TArrayOfString);
var
  Unticked: TArrayOfString;
  I, J: Integer;
  Tick: Boolean;
begin
  SetArrayLength(Unticked, 0);

  for I := 0 to GetArrayLength(OldCopies) - 1 do
    if not OldCopiesPage.Values[I] then
      AddUnique(Unticked, OldCopies[I]);

  OldCopies := Copies;
  OldCopiesPage.CheckListBox.Items.Clear;

  for I := 0 to GetArrayLength(OldCopies) - 1 do
    OldCopiesPage.Add(OldCopies[I]);

  for I := 0 to GetArrayLength(OldCopies) - 1 do
  begin
    Tick := True;

    for J := 0 to GetArrayLength(Unticked) - 1 do
      if CompareText(Unticked[J], OldCopies[I]) = 0 then
        Tick := False;

    OldCopiesPage.Values[I] := Tick;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
var
  Copies: TArrayOfString;
begin
  Result := False;

  if PageID = Vst2Page.ID then
    Result := not WizardIsComponentSelected('vst2')
  else if PageID = OldCopiesPage.ID then
  begin
    // Looked for again only on the way forward from the choices before it, which change what this
    // install replaces: Setup also asks this on the way back from the Ready page.
    if RescanCopies then
    begin
      RescanCopies := False;
      FindOldCopies(Copies);

      if not SameCopies(Copies, OldCopies) then
        ShowCopies(Copies);
    end;

    Result := GetArrayLength(OldCopies) = 0;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if (CurPageID = Vst2Page.ID) and (GetVst2Dir('') = '') then
  begin
    SuppressibleMsgBox('Choose a folder for the VST2 plugin.', mbError, MB_OK, MB_OK);
    Result := False;
  end;

  if (CurPageID = wpSelectComponents) or (CurPageID = Vst2Page.ID) then
    RescanCopies := True;
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

    if SuppressibleMsgBox('FL Studio is running, and Windows can''t replace or remove a plugin it has loaded.' + #13#10#13#10 +
                          'Save your project, close FL Studio, then click Retry.', mbInformation, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
      exit;

    Result := '';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and (NotRemoved <> '') then
  begin
    Log('Older copies left in place:' + NotRemoved);
    if not WizardSilent then
      SuppressibleMsgBox('These older copies couldn''t be removed:' + NotRemoved + #13#10#13#10 +
                         'A program may still have them open (FL Studio, or another one that uses plugins): ' +
                         'close it, then delete them by hand.', mbInformation, MB_OK, MB_OK);
  end;
end;

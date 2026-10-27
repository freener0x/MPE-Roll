# Installs, upgrades and uninstalls MPE Roll with its installer, silently, and checks where every
# file goes and that older copies are found and removed. The Windows CI job runs it on a fresh
# runner: it writes to Program Files and leaves test folders behind.
#
#   pwsh installer/test-installer.ps1 -Setup installer/output/MPE-Roll-Setup.exe -Dist dist

param(
    [Parameter(Mandatory)] [string] $Setup,
    [Parameter(Mandatory)] [string] $Dist
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Setup = (Resolve-Path $Setup).Path
$Dist = (Resolve-Path $Dist).Path
$commonFiles = $env:CommonProgramFiles
$vst3 = Join-Path $commonFiles 'VST3\MPE Roll.vst3'
$app = Join-Path $env:ProgramFiles 'MPE Roll'
$testRoot = 'C:\MPE Roll installer test'
$vst2A = Join-Path $testRoot 'VST2 A'
$vst2B = Join-Path $testRoot 'VST2 B'
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\MPE Roll.lnk'
$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{74C456C0-14CD-4836-A865-C9B0FF60BB0B}_is1'
$logs = Join-Path $testRoot 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$script:run = 0

function Fail([string] $why) {
    Get-ChildItem $logs -Filter *.log | ForEach-Object { Write-Host "---- $($_.Name)"; Get-Content $_.FullName }
    throw $why
}

function Expect([bool] $condition, [string] $what) {
    if (-not $condition) { Fail "Expected: $what" }
    Write-Host "ok: $what"
}

function Install([string] $arguments) {
    $script:run++
    $log = Join-Path $logs "install-$($script:run).log"
    $all = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$log`" $arguments"
    Write-Host "Setup $all"
    $process = Start-Process -FilePath $Setup -ArgumentList $all -Wait -PassThru
    if ($process.ExitCode -ne 0) { Fail "Setup exited with $($process.ExitCode)" }
}

function Uninstall {
    $uninstaller = (Get-ItemProperty $uninstallKey).UninstallString.Trim('"')
    $log = Join-Path $logs 'uninstall.log'
    Start-Process -FilePath $uninstaller -ArgumentList "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$log`"" -Wait

    # The uninstaller runs on from a copy of itself in the temp folder, so the process waited for
    # can end before it has finished: wait for its registry entry to go.
    $deadline = (Get-Date).AddMinutes(2)
    while ((Test-Path $uninstallKey) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    Expect (-not (Test-Path $uninstallKey)) 'the uninstaller finishes and removes its entry'
    Start-Sleep -Seconds 2
}

function Same([string] $installed, [string] $original) {
    if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) { return $false }
    return (Get-FileHash -LiteralPath $installed).Hash -eq (Get-FileHash -LiteralPath $original).Hash
}

function SameFolder([string] $installed, [string] $original) {
    $expected = @(Get-ChildItem -LiteralPath $original -Recurse -File)
    $found = @(Get-ChildItem -LiteralPath $installed -Recurse -File)
    if ($expected.Count -ne $found.Count) { return $false }
    foreach ($file in $expected) {
        $relative = $file.FullName.Substring($original.Length)
        if (-not (Same (Join-Path $installed $relative) $file.FullName)) { return $false }
    }
    return $true
}

function Plant([string] $path) {
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    Set-Content -LiteralPath $path -Value 'an older copy'
}

# Older copies a user could have unzipped by hand, and a file an older build left in the bundle.
$oldVst2 = Join-Path $commonFiles 'VST2\Old download\MPE Roll.dll'
$oldScanner = Join-Path $commonFiles 'VST2\Old download\MPE Roll Scanner.exe'
$oldVst3 = Join-Path $commonFiles 'VST3\Old download\MPE Roll.vst3'
$leftover = Join-Path $vst3 'Contents\x86_64-win\left over by an older build.txt'
$otherPlugin = Join-Path $commonFiles 'VST2\Old download\Some Other Synth.dll'
Plant $oldVst2
Plant $oldScanner
Plant (Join-Path $oldVst3 'Contents\x86_64-win\MPE Roll.vst3')
Plant $leftover
Plant $otherPlugin

Write-Host '== Everything, VST2 in folder A'
Install "/COMPONENTS=vst3,vst2,app /VST2DIR=`"$vst2A`""
Expect (SameFolder $vst3 (Join-Path $Dist 'MPE Roll.vst3')) 'the VST3 bundle is installed as built, with nothing else in it'
Expect (Same (Join-Path $vst2A 'MPE Roll.dll') (Join-Path $Dist 'MPE Roll.dll')) 'the VST2 is in the chosen folder'
Expect (Same (Join-Path $vst2A 'MPE Roll Scanner.exe') (Join-Path $Dist 'MPE Roll Scanner.exe')) 'the scanner is beside the VST2'
Expect (Same (Join-Path $app 'MPE Roll.exe') (Join-Path $Dist 'MPE Roll.exe')) 'the app is installed'
Expect (Same (Join-Path $app 'MPE Roll.pdb') (Join-Path $Dist 'MPE Roll.pdb')) 'the app''s .pdb is beside it'
Expect (Same (Join-Path $app 'MPE Roll Scanner.exe') (Join-Path $Dist 'MPE Roll Scanner.exe')) 'the scanner is beside the app'
Expect (Test-Path $shortcut) 'the Start menu has the app'
Expect (-not (Test-Path -LiteralPath $oldVst2)) 'an older VST2 copy is removed'
Expect (-not (Test-Path -LiteralPath $oldScanner)) 'the scanner beside it is removed with it'
Expect (-not (Test-Path -LiteralPath $oldVst3)) 'an older VST3 copy in a subfolder is removed'
Expect (Test-Path -LiteralPath $otherPlugin) 'other plugins stay'

Write-Host '== Again, VST2 moved to folder B'
Install "/COMPONENTS=vst3,vst2,app /VST2DIR=`"$vst2B`""
Expect (Same (Join-Path $vst2B 'MPE Roll.dll') (Join-Path $Dist 'MPE Roll.dll')) 'the VST2 is in the new folder'
Expect (-not (Test-Path -LiteralPath (Join-Path $vst2A 'MPE Roll.dll'))) 'the VST2 left in the old folder is removed'
Expect (-not (Test-Path -LiteralPath (Join-Path $vst2A 'MPE Roll Scanner.exe'))) 'so is the scanner beside it'

Write-Host '== Again, VST3 and app only, keeping older copies'
Plant $oldVst2
Install '/COMPONENTS=vst3,app /KEEPOLDCOPIES'
Expect (Test-Path -LiteralPath $oldVst2) '/KEEPOLDCOPIES keeps older copies'
Expect (Test-Path -LiteralPath (Join-Path $vst2B 'MPE Roll.dll')) 'and the VST2 installed before'

Write-Host '== Uninstall'
Uninstall
Expect (-not (Test-Path -LiteralPath $vst3)) 'the VST3 is gone'
Expect (-not (Test-Path -LiteralPath (Join-Path $vst2B 'MPE Roll.dll'))) 'the VST2 an earlier install put in folder B is gone'
Expect (-not (Test-Path -LiteralPath (Join-Path $vst2B 'MPE Roll Scanner.exe'))) 'and its scanner'
Expect (-not (Test-Path -LiteralPath $app)) 'the app''s folder is gone'
Expect (-not (Test-Path $shortcut)) 'the Start menu entry is gone'
Expect (Test-Path -LiteralPath $oldVst2) 'a copy the installer didn''t put there is left alone'
Write-Host 'The installer passed.'

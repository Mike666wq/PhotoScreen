param([Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference = 'Stop'
$stage = Join-Path $env:GITHUB_WORKSPACE 'stage'
$dist = Join-Path $env:GITHUB_WORKSPACE 'dist'
New-Item -ItemType Directory -Force $stage, $dist | Out-Null
cmake --install build --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'cmake install failed' }
& "$env:QT_ROOT_DIR\bin\windeployqt.exe" --release --compiler-runtime --no-translations "$stage\PhotoScreen.exe"
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }
if (-not $env:VCToolsRedistDir) { throw 'VCToolsRedistDir is not set by the MSVC environment' }
$redist = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Sort-Object Name -Descending | Select-Object -First 1
if (-not $redist) { throw 'Official MSVC x64 CRT redist directory not found' }
Copy-Item "$($redist.FullName)\*.dll" $stage -Force
Copy-Item 'THIRD_PARTY_NOTICES.md' $stage
Copy-Item 'third_party\qt' "$stage\Qt-Licenses" -Recurse -Force
if (-not (Test-Path "$stage\platforms\qwindows.dll")) { throw 'Qt Windows platform plugin missing' }
if (-not (Test-Path "$stage\vcruntime140.dll")) { throw 'MSVC runtime missing' }
$headers = & dumpbin /headers "$stage\PhotoScreen.exe" | Out-String
if ($LASTEXITCODE -ne 0 -or $headers -notmatch '8664 machine \(x64\)') { throw 'Application is not x64' }
Compress-Archive -Path "$stage\*" -DestinationPath "$dist\PhotoScreen-$Version-windows-x64.zip" -Force
$iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
if (-not (Test-Path $iscc)) { throw 'Inno Setup 6 is not installed on the runner' }
& $iscc /Qp "/DMyAppVersion=$Version" "/DSourceDir=$stage" "/DOutputDir=$dist" 'packaging\windows.iss'
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed' }
if (-not (Test-Path "$dist\PhotoScreen-$Version-windows-x64-setup.exe")) { throw 'Installer missing' }

param(
    [Parameter(Mandatory)][string]$QtRoot,
    [string]$BuildDir='build',
    [string]$DistDir='dist',
    [string]$ISCCPath='C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$build=(Resolve-Path -LiteralPath $BuildDir).Path
$dist=[IO.Path]::GetFullPath($DistDir)
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$package=Join-Path $dist 'ImageSifter-2.0.0-windows-x64'
if(Test-Path -LiteralPath $package){throw 'Choose a fresh distribution directory; packaging never clears an existing folder.'}
New-Item -ItemType Directory -Path $package | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'ImageSifter.exe') -Destination $package
$raw=Get-ChildItem -LiteralPath $build -Filter '*sifter_raw*.dll'
if($raw.Count -ne 1){throw 'Expected exactly one RAW library'}
Copy-Item -LiteralPath $raw.FullName -Destination $package
& (Join-Path $QtRoot 'bin/windeployqt.exe') --release --compiler-runtime --no-translations --no-opengl-sw --dir $package (Join-Path $package 'ImageSifter.exe')
if($LASTEXITCODE){throw 'Qt deployment failed'}
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE'),(Join-Path $repo 'README.md'),(Join-Path $repo 'THIRD-PARTY-NOTICES.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $repo 'licenses') -Destination $package -Recurse
[IO.File]::WriteAllText((Join-Path $package 'qt.conf'),"[Paths]`nPrefix=.`nPlugins=.`n",[Text.UTF8Encoding]::new($false))
$previousPath=$env:Path
try {
    $env:Path="$env:SystemRoot\System32;$env:SystemRoot"
    $probe=Start-Process -FilePath (Join-Path $package 'ImageSifter.exe') -ArgumentList '--check-runtime' -WindowStyle Hidden -Wait -PassThru
    if($probe.ExitCode -ne 0){throw ('Deployed plugin/runtime probe failed: '+$probe.ExitCode)}
} finally {$env:Path=$previousPath}
Compress-Archive -Path (Join-Path $package '*') -DestinationPath ($package+'-portable.zip') -CompressionLevel Optimal
& $ISCCPath "/DPackageDir=$package" "/DOutputDir=$dist" (Join-Path $PSScriptRoot 'installer.iss')
if($LASTEXITCODE){throw 'Installer compilation failed'}
$artifacts=Get-ChildItem -LiteralPath $dist -File | Where-Object Extension -in '.zip','.exe'
$lines=foreach($artifact in $artifacts){$hash=Get-FileHash -LiteralPath $artifact.FullName -Algorithm SHA256; $hash.Hash.ToLowerInvariant()+'  '+$artifact.Name}
[IO.File]::WriteAllLines((Join-Path $dist 'SHA256SUMS.txt'),$lines,[Text.UTF8Encoding]::new($false))
Write-Output ('Packages and checksums ready: '+$dist)

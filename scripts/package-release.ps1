param(
  [string]$BuildDirectory = "build-native\Release",
  [string]$OutputDirectory = "release"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$outputRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if (-not $outputRoot.StartsWith($projectRoot + [System.IO.Path]::DirectorySeparatorChar,
                               [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Output directory must stay inside the lw.PDF project."
}

$executable = Join-Path $buildRoot "lw.PDF.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
  throw "Native executable not found: $executable"
}

$version = (Get-Content -LiteralPath (Join-Path $projectRoot "package.json") -Raw |
  ConvertFrom-Json).version
if ($version -notmatch '^\d+\.\d+\.\d+$') {
  throw "Invalid package version: $version"
}
$executableVersion = (Get-Item -LiteralPath $executable).VersionInfo.ProductVersion
if ($executableVersion -ne $version) {
  throw "Executable version '$executableVersion' does not match package version '$version'. Reconfigure and rebuild the native project."
}

$packageName = "lw.PDF-v$version-windows-x64"
$packageRoot = Join-Path $outputRoot $packageName
$archivePath = Join-Path $outputRoot "$packageName.zip"
$checksumPath = "$archivePath.sha256"

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
if (Test-Path -LiteralPath $packageRoot) {
  Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
foreach ($path in @($archivePath, $checksumPath)) {
  if (Test-Path -LiteralPath $path) {
    Remove-Item -LiteralPath $path -Force
  }
}
New-Item -ItemType Directory -Path $packageRoot | Out-Null

Copy-Item -LiteralPath $executable -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $projectRoot "README.md") -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $projectRoot "README.en.md") -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $projectRoot "LICENSE") -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $projectRoot "THIRD_PARTY_NOTICES.md") -Destination $packageRoot
$assetDirectory = Join-Path $packageRoot "docs\assets"
New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\assets\sponsor.jpg") -Destination $assetDirectory

Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash *$([System.IO.Path]::GetFileName($archivePath))" |
  Set-Content -LiteralPath $checksumPath -Encoding ascii -NoNewline

Write-Host "Release package: $archivePath"
Write-Host "SHA256: $checksumPath"

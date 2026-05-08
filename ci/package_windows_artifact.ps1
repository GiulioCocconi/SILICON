param(
    [Parameter(Mandatory = $true)]
    [string]$PackageName,

    [Parameter(Mandatory = $true)]
    [string]$PackageVersion
)

$ErrorActionPreference = "Stop"

$outputDir = "out"
$packageFile = Join-Path $outputDir "$PackageName-$PackageVersion.zip"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
if (Test-Path $packageFile) {
    Remove-Item $packageFile -Force
}

# Replace the packaged directory below if the Windows deployment layout changes.
Compress-Archive -Path "bin\*" -DestinationPath $packageFile

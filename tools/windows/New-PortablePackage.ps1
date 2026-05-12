[CmdletBinding()]
param(
  [string]$RepoRoot = ".",
  [string]$ManifestPath = "packaging/windows/runtime-manifest.txt",
  [string]$OutDir = "artifact/windows-portable",
  [string]$ZipPath = "artifact/uxplay-windows-portable.zip"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$Base, [string]$Path) {
  if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
  return [System.IO.Path]::GetFullPath((Join-Path $Base $Path))
}

$repo = [System.IO.Path]::GetFullPath($RepoRoot)
$manifest = Resolve-RepoPath $repo $ManifestPath
if (!(Test-Path $manifest)) { throw "Manifest not found: $manifest" }

$stageRoot = [System.IO.Path]::GetFullPath($OutDir)
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

$lines = Get-Content $manifest | Where-Object { $_ -and -not $_.Trim().StartsWith("#") }
foreach ($line in $lines) {
  $parts = $line.Split("|", 2)
  if ($parts.Count -ne 2) { throw "Invalid manifest line: $line" }
  $destPattern = $parts[0].Trim()
  $sourcePattern = Resolve-RepoPath $repo $parts[1].Trim()

  $matches = Get-ChildItem -Path $sourcePattern -File -Recurse -ErrorAction SilentlyContinue
  if (!$matches) { throw "No files matched '$sourcePattern' for '$destPattern'" }

  foreach ($file in $matches) {
    $destDir = Join-Path $stageRoot ([System.IO.Path]::GetDirectoryName($destPattern))
    if ([string]::IsNullOrWhiteSpace($destDir)) { $destDir = $stageRoot }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null

    $destLeaf = [System.IO.Path]::GetFileName($destPattern)
    if ($destLeaf.Contains("*")) {
      $destPath = Join-Path $destDir $file.Name
    } else {
      $destPath = Join-Path $destDir $destLeaf
    }

    Copy-Item -Path $file.FullName -Destination $destPath -Force
  }
}

$readme = Resolve-RepoPath $repo "README-Windows-Portable.md"
if (Test-Path $readme) {
  Copy-Item -Path $readme -Destination (Join-Path $stageRoot "README-Windows-Portable.md") -Force
} else {
  throw "README-Windows-Portable.md not found at repo root"
}

if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($ZipPath))) -Force | Out-Null
Compress-Archive -Path (Join-Path $stageRoot "*") -DestinationPath $ZipPath -Force
Write-Host "Portable package created at $ZipPath"

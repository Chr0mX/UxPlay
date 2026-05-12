[CmdletBinding()]
param(
  [string]$UxPlayPath = "",
  [string]$LogPath = ""
)

$ErrorActionPreference = 'Stop'

function Get-GstInspectPath {
  if ($env:GST_INSPECT_1_0 -and (Test-Path $env:GST_INSPECT_1_0)) {
    return (Resolve-Path $env:GST_INSPECT_1_0).Path
  }

  $cmd = Get-Command gst-inspect-1.0 -ErrorAction SilentlyContinue
  if ($cmd) {
    return $cmd.Source
  }

  $candidates = @(
    "C:\msys64\ucrt64\bin\gst-inspect-1.0.exe",
    "C:\gstreamer\1.0\msvc_x86_64\bin\gst-inspect-1.0.exe"
  )
  foreach ($candidate in $candidates) {
    if (Test-Path $candidate) { return $candidate }
  }

  throw "Unable to locate gst-inspect-1.0. Set GST_INSPECT_1_0 or add it to PATH."
}

$gstInspect = Get-GstInspectPath

$checks = @(
  @{ Name = 'DXVA2'; Elements = @('d3d11h264dec','d3d11h265dec','d3d11av1dec','d3d11vp9dec'); RequiredAny = $true },
  @{ Name = 'D3D11VA'; Elements = @('d3d11videosink','d3d11convert'); RequiredAny = $true },
  @{ Name = 'NVDEC'; Elements = @('nvh264dec','nvh265dec','nvav1dec','nvvp9dec'); RequiredAny = $false },
  @{ Name = 'Software fallback'; Elements = @('decodebin','videoconvert','autovideosink'); RequiredAny = $false }
)

$results = @()
foreach ($check in $checks) {
  $available = @()
  foreach ($element in $check.Elements) {
    & $gstInspect $element *> $null
    if ($LASTEXITCODE -eq 0) {
      $available += $element
    }
  }

  $ok = if ($check.RequiredAny) { $available.Count -gt 0 } else { $true }

  $results += [PSCustomObject]@{
    Probe = $check.Name
    Required = [bool]$check.RequiredAny
    Available = if ($available.Count -gt 0) { $available -join ',' } else { '' }
    Passed = [bool]$ok
  }
}

$fallback = $results | Where-Object { $_.Probe -eq 'Software fallback' }
if ([string]::IsNullOrWhiteSpace($fallback.Available)) {
  $fallback.Passed = $false
}

$hardwareAvailable = ($results | Where-Object { $_.Probe -in @('DXVA2','D3D11VA','NVDEC') -and -not [string]::IsNullOrWhiteSpace($_.Available) }).Count -gt 0
$summary = [PSCustomObject]@{
  TimestampUtc = (Get-Date).ToUniversalTime().ToString('o')
  GstInspectPath = $gstInspect
  HardwareDecodeDetected = $hardwareAvailable
  FallbackVerified = [bool]$fallback.Passed
  UxPlayPath = $UxPlayPath
  Checks = $results
}

if ($LogPath) {
  $dir = Split-Path -Parent $LogPath
  if ($dir) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
  $summary | ConvertTo-Json -Depth 5 | Out-File -Encoding utf8 -FilePath $LogPath
}

$results | Format-Table -AutoSize | Out-String | Write-Host
if (-not $fallback.Passed) {
  throw "Software fallback path validation failed: decodebin/videoconvert/autovideosink are unavailable."
}

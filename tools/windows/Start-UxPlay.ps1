[CmdletBinding()]
param(
  [string]$UxPlayPath = ".\bin\uxplay.exe",
  [string]$VideoSink = "d3d11videosink",
  [switch]$Windowed,
  [string]$AdditionalArgs = ""
)

$ErrorActionPreference = 'Stop'

if (!(Test-Path $UxPlayPath)) {
  throw "UxPlay binary not found at '$UxPlayPath'"
}

$syncMode = if ($env:UXPLAY_VIDEO_SYNC) { $env:UXPLAY_VIDEO_SYNC } else { "false" }
$fullscreen = if ($Windowed -or ($env:UXPLAY_FULLSCREEN -eq 'false')) { "false" } else { "true" }
$monitorIndex = if ($env:UXPLAY_FULLSCREEN_MONITOR) { $env:UXPLAY_FULLSCREEN_MONITOR } else { "0" }

$videoSinkExpr = "$VideoSink fullscreen=$fullscreen sync=$syncMode"
if ($fullscreen -eq "true") {
  $videoSinkExpr += " fullscreen-monitor=$monitorIndex"
}

$uxplayArgs = @(
  "-vs", $videoSinkExpr,
  "-as", "autoaudiosink sync=false",
  "-al", "0.0"
)

if ($AdditionalArgs) {
  $uxplayArgs += $AdditionalArgs
}

Write-Host "Launching UxPlay with args: $($uxplayArgs -join ' ')"
& $UxPlayPath @uxplayArgs

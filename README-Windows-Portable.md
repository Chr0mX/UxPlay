# UxPlay Windows Portable Package

This archive is intended to run UxPlay without a separate installer.

## Layout

- `bin/` : `uxplay.exe` plus required runtime DLLs.
- `lib/` : GStreamer plugin/runtime module chain.
- `licenses/` : Third-party license files for redistributed dependencies.

## Validation

CI runs import validation for `bin/uxplay.exe` and every `bin/*.dll` using
`dumpbin /dependents`, then checks each dependent DLL resolves in `bin/` or
Windows system directories.

CI also runs `tools/windows/validate-rendering.ps1` to probe decoder/sink
availability and write `artifact/logs/rendering-validation.json` with:

- DXVA2-related decoder element detection (`d3d11*dec`).
- D3D11VA rendering path checks (`d3d11videosink`, `d3d11convert`).
- NVDEC decoder detection (`nv*dec`) when present on the runner.
- Software fallback checks (`decodebin`, `videoconvert`, `autovideosink`) to
  ensure runtime fallback when hardware decode is unavailable.

## Runtime defaults and launch overrides

Use `tools/windows/Start-UxPlay.ps1` for low-latency defaults and stable
fullscreen behavior:

```powershell
./tools/windows/Start-UxPlay.ps1 -UxPlayPath .\artifact\windows-portable\bin\uxplay.exe
```

Default behavior:

- Video sink: `d3d11videosink`.
- Fullscreen enabled with a fixed monitor index (`fullscreen-monitor=0`).
- Audio/video sync disabled (`sync=false`) and `-al 0.0` for low latency.

Override options:

- `-VideoSink <sink>` to change video sink.
- `-Windowed` to disable fullscreen.
- `-AdditionalArgs "..."` to append raw UxPlay arguments.
- Environment variable `UXPLAY_VIDEO_SYNC=true|false`.
- Environment variable `UXPLAY_FULLSCREEN=true|false`.
- Environment variable `UXPLAY_FULLSCREEN_MONITOR=<index>`.

## Manual QA checklist / release sign-off

Before tagging a Windows release, verify:

1. Run `tools/windows/validate-rendering.ps1` and confirm fallback checks pass.
2. Launch with `Start-UxPlay.ps1` defaults and confirm smooth low-latency playback.
3. Verify fullscreen on monitor `0`, then relaunch with
   `UXPLAY_FULLSCREEN_MONITOR=1` (or next available monitor) and confirm
   fullscreen attaches to the selected monitor.
4. Toggle fullscreen (Alt+Enter) and confirm no unstable resize/flicker loops.

## Notes

If you customize dependencies, update `packaging/windows/runtime-manifest.txt`
so runtime contents remain explicit and auditable.

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

## Notes

If you customize dependencies, update `packaging/windows/runtime-manifest.txt`
so runtime contents remain explicit and auditable.

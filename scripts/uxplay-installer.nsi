; UxPlay Windows Installer
; Saved as UTF-8 with a BOM so makensis reads the non-ASCII text (em dashes) correctly.
; Built with NSIS (Nullsoft Scriptable Install System)
; Invoke: makensis /DVERSION=x.y.z scripts\uxplay-installer.nsi
;         (run from repo root so File paths resolve correctly)

; NSIS resolves File/OutFile relative to the script's directory by default.
; !cd ".." moves the compile-time working directory up to the repo root so
; "uxplay-windows-portable\*.*" and the OutFile path both resolve correctly.
!cd ".."

!ifndef VERSION
  !define VERSION "dev"
!endif

!define PRODUCT_NAME "UxPlay"
!define PRODUCT_VER  "${VERSION}"
!define PRODUCT_PUBL "UxPlay Contributors"
!define PRODUCT_URL  "https://github.com/Chr0mX/UxPlay"
!define INST_REG     "Software\UxPlay"
!define UNINST_REG   "Software\Microsoft\Windows\CurrentVersion\Uninstall\UxPlay"

Unicode True
; Sharp text on high-DPI displays (otherwise Windows bitmap-scales the wizard).
ManifestDPIAware true
ManifestSupportedOS all
SetCompressor /SOLID lzma

Name           "${PRODUCT_NAME} ${PRODUCT_VER}"
OutFile        "uxplay-windows-installer.exe"
InstallDir     "$PROGRAMFILES64\UxPlay"
InstallDirRegKey HKLM "${INST_REG}" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show

; ── Pages ─────────────────────────────────────────────────────────────────────
!include "MUI2.nsh"
!include "WinVer.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
; Same artwork as the app (embedded in uxplay-gui.exe and uxplay.exe)
!define MUI_ICON   "gui\uxplay.ico"
!define MUI_UNICON "gui\uxplay.ico"
; Branded artwork, drawn at 2x so it stays crisp when scaled for high DPI
!define MUI_WELCOMEFINISHPAGE_BITMAP   "gui\installer\welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "gui\installer\welcome.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP         "gui\installer\header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP       "gui\installer\header.bmp"
!define MUI_WELCOMEPAGE_TEXT \
  "This wizard will install UxPlay ${PRODUCT_VER} on your computer.$\r$\n$\r$\n\
UxPlay turns your PC into an AirPlay receiver. Stream your iPhone or iPad \
screen directly to Windows — no Apple TV required.$\r$\n$\r$\n\
All required runtimes (GStreamer, OpenSSL, libplist) are bundled. \
iPhone and iPad find UxPlay on their own — no Apple Bonjour or iTunes \
needed.$\r$\n$\r$\nClick Next to continue."

Var SettingsDir
Var LogDir

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY

; Where the launcher keeps settings and logs (changeable later in Advanced > Storage)
!define MUI_PAGE_HEADER_TEXT "Settings Folder"
!define MUI_PAGE_HEADER_SUBTEXT "Choose where UxPlay saves its settings."
!define MUI_DIRECTORYPAGE_TEXT_TOP "UxPlay saves its receiver settings and window preferences in this folder. \
You can change it later in UxPlay under Advanced > Storage."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "Settings folder"
!define MUI_DIRECTORYPAGE_VARIABLE $SettingsDir
!insertmacro MUI_PAGE_DIRECTORY

!define MUI_PAGE_HEADER_TEXT "Logs Folder"
!define MUI_PAGE_HEADER_SUBTEXT "Choose where UxPlay saves its diagnostic logs."
!define MUI_DIRECTORYPAGE_TEXT_TOP "UxPlay writes diagnostic logs (receiver output and connection events) to this folder. \
You can change it later in UxPlay under Advanced > Storage."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "Logs folder"
!define MUI_DIRECTORYPAGE_VARIABLE $LogDir
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN          "$INSTDIR\uxplay-gui.exe"
!define MUI_FINISHPAGE_RUN_TEXT     "Launch UxPlay now"
!define MUI_FINISHPAGE_SHOWREADME   "$INSTDIR\README.md"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "View README"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Main install section ──────────────────────────────────────────────────────
Section "UxPlay (required)" SEC_MAIN
  SectionIn RO

  ; Close a running UxPlay so its files can be replaced
  nsExec::Exec 'taskkill /F /IM uxplay-gui.exe'
  nsExec::Exec 'taskkill /F /IM uxplay.exe'

  SetOutPath "$INSTDIR"
  File /r "uxplay-windows-portable\*.*"
  Delete "$INSTDIR\README-Windows.txt"   ; renamed to README.md

  CreateDirectory "$SettingsDir"
  CreateDirectory "$LogDir"

  ; Firewall: allow inbound on all profiles
  ExecWait 'netsh advfirewall firewall delete rule name="UxPlay"'
  ExecWait 'netsh advfirewall firewall add rule name="UxPlay" \
    dir=in action=allow program="$INSTDIR\uxplay.exe" enable=yes profile=any \
    description="UxPlay receiver engine (AirPlay)"'

  ; Registry entries
  WriteRegStr  HKLM "${INST_REG}" "InstallDir" "$INSTDIR"
  WriteRegStr  HKLM "${INST_REG}" "SettingsDir" "$SettingsDir"
  WriteRegStr  HKLM "${INST_REG}" "LogDir"      "$LogDir"
  WriteRegStr  HKLM "${INST_REG}" "Version"    "${PRODUCT_VER}"

  WriteRegStr  HKLM "${UNINST_REG}" "DisplayName"     "${PRODUCT_NAME} ${PRODUCT_VER}"
  WriteRegStr  HKLM "${UNINST_REG}" "DisplayVersion"  "${PRODUCT_VER}"
  WriteRegStr  HKLM "${UNINST_REG}" "Publisher"       "${PRODUCT_PUBL}"
  WriteRegStr  HKLM "${UNINST_REG}" "URLInfoAbout"    "${PRODUCT_URL}"
  WriteRegStr  HKLM "${UNINST_REG}" "InstallLocation" "$INSTDIR"
  WriteRegStr  HKLM "${UNINST_REG}" "DisplayIcon"     "$INSTDIR\uxplay-gui.exe,0"
  WriteRegStr  HKLM "${UNINST_REG}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "${UNINST_REG}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_REG}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Start Menu
  CreateDirectory "$SMPROGRAMS\UxPlay"
  CreateShortcut "$SMPROGRAMS\UxPlay\UxPlay.lnk" \
    "$INSTDIR\uxplay-gui.exe" "" "$INSTDIR\uxplay-gui.exe" 0
  Delete "$SMPROGRAMS\UxPlay\UxPlay (command line).lnk"   ; renamed below
  CreateShortcut "$SMPROGRAMS\UxPlay\UxPlay (command line, advanced).lnk" \
    "$INSTDIR\uxplay.bat"
  CreateShortcut "$SMPROGRAMS\UxPlay\Uninstall UxPlay.lnk" \
    "$INSTDIR\uninstall.exe"

  ; Desktop shortcut
  CreateShortcut "$DESKTOP\UxPlay.lnk" \
    "$INSTDIR\uxplay-gui.exe" "" "$INSTDIR\uxplay-gui.exe" 0

SectionEnd

; ── Minimum OS check ──────────────────────────────────────────────────────────
Function .onInit
  ; Defaults for the folder pages (per-user locations; same as the launcher's defaults)
  ReadRegStr $SettingsDir HKLM "${INST_REG}" "SettingsDir"
  StrCmp $SettingsDir "" 0 +2
    StrCpy $SettingsDir "$APPDATA\UxPlay"
  ReadRegStr $LogDir HKLM "${INST_REG}" "LogDir"
  StrCmp $LogDir "" 0 +2
    StrCpy $LogDir "$LOCALAPPDATA\UxPlay\logs"
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "UxPlay requires Windows 10 or later."
    Abort
  ${EndIf}
FunctionEnd

; ── Uninstaller ───────────────────────────────────────────────────────────────
Section "Uninstall"
  nsExec::Exec 'taskkill /F /IM uxplay-gui.exe'
  nsExec::Exec 'taskkill /F /IM uxplay.exe'

  ; Firewall
  ExecWait 'netsh advfirewall firewall delete rule name="UxPlay"'

  ; Shortcuts
  Delete "$SMPROGRAMS\UxPlay\*.lnk"
  RMDir  "$SMPROGRAMS\UxPlay"
  Delete "$DESKTOP\UxPlay.lnk"

  ; Files — remove all bundled content; keep user files (settings.ini, registry)
  RMDir /r "$INSTDIR\gst-plugins"
  Delete   "$INSTDIR\*.dll"
  Delete   "$INSTDIR\*.exe"
  Delete   "$INSTDIR\*.bat"
  Delete   "$INSTDIR\*.txt"
  Delete   "$INSTDIR\README.md"
  Delete   "$INSTDIR\*.py"
  Delete   "$INSTDIR\*.bin"
  Delete   "$INSTDIR\uninstall.exe"
  RMDir    "$INSTDIR"

  ; Registry
  DeleteRegKey HKLM "${UNINST_REG}"
  DeleteRegKey HKLM "${INST_REG}"
SectionEnd

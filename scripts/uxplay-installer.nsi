; UxPlay Windows Installer
; Built with NSIS (Nullsoft Scriptable Install System)
; Invoke: makensis /DVERSION=x.y.z scripts\uxplay-installer.nsi
;         (run from repo root so File paths resolve correctly)

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

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT \
  "This wizard will install UxPlay ${PRODUCT_VER} on your computer.$\r$\n$\r$\n\
UxPlay turns your PC into an AirPlay receiver. Stream your iPhone or iPad \
screen directly to Windows — no Apple TV required.$\r$\n$\r$\n\
All required runtimes (GStreamer, OpenSSL, libplist) are bundled. \
Apple Bonjour is required for device discovery — the installer will check \
for it and guide you if it is missing.$\r$\n$\r$\nClick Next to continue."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN          "$INSTDIR\uxplay-gui.exe"
!define MUI_FINISHPAGE_RUN_TEXT     "Launch UxPlay now"
!define MUI_FINISHPAGE_SHOWREADME   "$INSTDIR\README-Windows.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "View README"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Main install section ──────────────────────────────────────────────────────
Section "UxPlay (required)" SEC_MAIN
  SectionIn RO

  SetOutPath "$INSTDIR"
  File /r "uxplay-windows-portable\*.*"

  ; Firewall: allow inbound on all profiles
  ExecWait 'netsh advfirewall firewall delete rule name="UxPlay"'
  ExecWait 'netsh advfirewall firewall add rule name="UxPlay" \
    dir=in action=allow program="$INSTDIR\uxplay.exe" enable=yes profile=any'

  ; Registry entries
  WriteRegStr  HKLM "${INST_REG}" "InstallDir" "$INSTDIR"
  WriteRegStr  HKLM "${INST_REG}" "Version"    "${PRODUCT_VER}"

  WriteRegStr  HKLM "${UNINST_REG}" "DisplayName"     "${PRODUCT_NAME} ${PRODUCT_VER}"
  WriteRegStr  HKLM "${UNINST_REG}" "DisplayVersion"  "${PRODUCT_VER}"
  WriteRegStr  HKLM "${UNINST_REG}" "Publisher"       "${PRODUCT_PUBL}"
  WriteRegStr  HKLM "${UNINST_REG}" "URLInfoAbout"    "${PRODUCT_URL}"
  WriteRegStr  HKLM "${UNINST_REG}" "InstallLocation" "$INSTDIR"
  WriteRegStr  HKLM "${UNINST_REG}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "${UNINST_REG}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_REG}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Start Menu
  CreateDirectory "$SMPROGRAMS\UxPlay"
  CreateShortcut "$SMPROGRAMS\UxPlay\UxPlay.lnk" \
    "$INSTDIR\uxplay-gui.exe" "" "$INSTDIR\uxplay-gui.exe" 0
  CreateShortcut "$SMPROGRAMS\UxPlay\UxPlay (command line).lnk" \
    "$INSTDIR\uxplay.bat"
  CreateShortcut "$SMPROGRAMS\UxPlay\Uninstall UxPlay.lnk" \
    "$INSTDIR\uninstall.exe"

  ; Desktop shortcut
  CreateShortcut "$DESKTOP\UxPlay.lnk" \
    "$INSTDIR\uxplay-gui.exe" "" "$INSTDIR\uxplay-gui.exe" 0

SectionEnd

; ── Minimum OS check ──────────────────────────────────────────────────────────
Function .onInit
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "UxPlay requires Windows 10 or later."
    Abort
  ${EndIf}
FunctionEnd

; ── Post-install: warn if Bonjour is absent ───────────────────────────────────
Function .onInstSuccess
  ; dnssd.dll in System32 or Program Files means Bonjour is installed
  IfFileExists "$WINDIR\System32\dnssd.dll"      bonjour_ok
  IfFileExists "$INSTDIR\dnssd.dll"              bonjour_ok
  IfFileExists "$PROGRAMFILES\Bonjour\dnssd.dll" bonjour_ok
    MessageBox MB_ICONINFORMATION|MB_OK \
      "Apple Bonjour was not detected on this system.$\r$\n$\r$\n\
Bonjour is required for UxPlay to appear in your iPhone/iPad AirPlay list. \
It is included with iTunes, iCloud for Windows, and the Apple TV app.$\r$\n$\r$\n\
You can also install the free standalone $\"Bonjour Print Services for Windows$\" \
from Apple.$\r$\n$\r$\nUxPlay will start but will NOT be discoverable until \
Bonjour is installed and running."
  bonjour_ok:
FunctionEnd

; ── Uninstaller ───────────────────────────────────────────────────────────────
Section "Uninstall"
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
  Delete   "$INSTDIR\*.py"
  Delete   "$INSTDIR\*.bin"
  Delete   "$INSTDIR\uninstall.exe"
  RMDir    "$INSTDIR"

  ; Registry
  DeleteRegKey HKLM "${UNINST_REG}"
  DeleteRegKey HKLM "${INST_REG}"
SectionEnd

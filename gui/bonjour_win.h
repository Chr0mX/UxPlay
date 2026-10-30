// UxPlay GUI Launcher — Apple Bonjour detection and automatic installation (Windows).
//
// UxPlay needs the Bonjour (mDNSResponder) service so Apple devices can discover
// the receiver. This module:
//   * detects an installed Bonjour service (never downloads when it is present),
//   * downloads Apple's "Bonjour Print Services" installer, checks its
//     Authenticode signature, and installs the Bonjour service silently by
//     extracting Bonjour64.msi / Bonjour.msi from the installer's CABINET
//     resource and running `msiexec /qn` (the Apple wrapper itself has no silent
//     switch), elevating with UAC when needed,
//   * falls back to running Apple's installer interactively, and
//   * verifies the service exists before reporting success.
// All work runs on a background thread; callers poll Snapshot().

#pragma once

#include <string>

namespace bonjour {

extern const char* kInstallerUrl;   // Apple's BonjourPSSetup.exe
extern const char* kSupportUrl;     // Apple Support article

enum class State { Unknown, Installed, Missing, Downloading, Installing, Failed };

struct Status {
    State       state   = State::Unknown;
    int         percent = -1;      // download progress, -1 = indeterminate
    std::string message;           // current step, user-facing
    std::string error;             // when Failed: what went wrong, user-facing
    bool        canRunInteractive = false;   // installer downloaded, can be run by hand
};

bool   IsInstalled();                 // service present (quick, synchronous)
void   Refresh();                     // re-detect (Installed / Missing) unless busy
void   StartInstall(bool interactive);// background: download if needed, install, verify
bool   Busy();
Status Snapshot();

// "--install-bonjour" mode used by the setup program: shows a small progress
// dialog, installs if missing, offers retry / manual links on failure.
// Returns 0 if Bonjour is installed at the end, 1 otherwise.
void   SetOwnerWindow(void* hwnd);   // parent for the Windows permission (UAC) prompt
int    RunSetupDialog(bool quiet);   // quiet: no UI (silent setup)

}  // namespace bonjour

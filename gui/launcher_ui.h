// UxPlay GUI Launcher — platform-independent settings model and UI.
// Everything in here depends only on Dear ImGui and the C++ standard library,
// so the Win32/D3D11 host (uxplay-gui.cpp) owns processes, windows and files.

#pragma once

#include <string>
#include "imgui.h"
#include "prefs.h"
#include "session.h"

// ── Settings model ────────────────────────────────────────────────────────────

struct Settings {
    char  serverName[128] = "";
    int   port            = 7100;
    bool  fullscreen      = false;
    char  windowSize[32]  = "1920x1080";
    bool  pin             = false;
    char  password[128]   = "";
    int   videoSinkIdx    = 0;
    int   videoDecoderIdx = 3;  // default: Auto (decodebin), graceful hardware fallback
    bool  h265            = true;
    int   audioSinkIdx    = 0;
    float volume          = 1.0f;
    int   fps             = 60;
    bool  noFreeze        = false;
    bool  debug           = false;
    char  extraArgs[256]  = "";
    int   receivers       = 1;  // phones mirroring side by side (one uxplay.exe each)
};

static const int kMaxReceivers = 4;

struct Option { const char* label; const char* flag; };

extern const Option kVideoSinks[];
extern const Option kVideoDecoders[];
extern const Option kAudioSinks[];
extern const int    kVideoSinkCount;
extern const int    kVideoDecoderCount;
extern const int    kAudioSinkCount;

// redact=true replaces the password with *** (for display and logs).
// index > 0 builds the arguments for an additional side-by-side receiver: its own
// name ("<name> 2"), ports (base + 10 per receiver) and a fixed device ID (-m), so
// several uxplay.exe instances can run at once. defaultName is the name uxplay
// advertises when none is set ("UxPlay@<PC name>").
std::string BuildArgs(const Settings& s, bool redact = false, int index = 0,
                      const std::string& defaultName = "UxPlay");
std::string ReceiverName(const Settings& s, int index, const std::string& defaultName);
std::string SerializeSettings(const Settings& s);                 // INI text
void        ParseSettings(Settings& s, const std::string& text);  // INI text

// ── UI ────────────────────────────────────────────────────────────────────────

struct UiFonts {
    ImFont* body   = nullptr;   // default text
    ImFont* small  = nullptr;   // descriptions, captions
    ImFont* strong = nullptr;   // labels, buttons
    ImFont* title  = nullptr;   // page titles
    ImFont* mono   = nullptr;   // command line, flags, console
};

// Operations the UI needs from the host platform. Every menu item and button
// calls one of these; the UI never manages processes or files itself.
struct UiHost {
    bool (*isRunning)();
    void (*start)(const Settings& s);      // reports success/failure through Session
    void (*stop)();
    bool (*save)(const Settings& s);
    int  (*getRotation)();                 // 0..3 quarter turns clockwise
    void (*setRotation)(int rot);
    void (*openLogFile)();
    void (*openLogFolder)();
    void (*openSettingsFolder)();
    void (*openDocs)();
    void (*exitApp)();
    void (*themeApplied)(bool dark);       // e.g. update the native title bar
    const char* (*logStatus)();            // nullptr when logging works, else reason
};

struct UiState {
    int         page          = 0;
    std::string savedSnapshot;      // SerializeSettings() at last load/save
    std::string toast;
    double      toastUntil    = 0.0;
    bool        toastError    = false;
    bool        consoleVisible = false;
    bool        consoleFollow  = true;
    char        consoleFilter[96] = "";
    bool        showTechnical  = false;
    bool        openAbout      = false;
    bool        prefsDirty     = false;   // host persists UiPrefs when set
    bool        systemDark     = true;    // host: OS app theme
    bool        reduceMotion   = false;   // host: OS animation preference
    int         appliedTheme   = -1;      // -1 none, 0 light, 1 dark
    int         seenErrorSerial = 0;
    int         consoleReceiver = 0;      // which receiver's output the console shows
    float       scale          = 1.0f;
    std::string launcherVersion = "dev";
    std::string computerName;            // host: this PC's name (uxplay's default is "UxPlay@<name>")
};

// Loads fonts at the given DPI scale. Missing files fall back to ImGui's default.
UiFonts LoadUiFonts(float scale, const char* regularTtf, const char* boldTtf, const char* monoTtf);
void    ApplyUiTheme(float scale, bool dark);
// sessions[0..count-1]: one per running/configured receiver (count >= 1).
void    DrawLauncherUI(Settings& s, UiState& ui, Session* sessions, int count, UiPrefs& prefs,
                       const UiHost& host, const UiFonts& fonts);

// Background color the host should clear to (matches the current theme).
ImVec4  UiClearColor();

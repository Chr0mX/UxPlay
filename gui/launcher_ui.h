// UxPlay GUI Launcher — platform-independent settings model and UI.
// Everything in here depends only on Dear ImGui and the C++ standard library,
// so the Win32/D3D11 host (uxplay-gui.cpp) owns processes, windows and files.

#pragma once

#include <string>
#include "imgui.h"

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
};

struct Option { const char* label; const char* flag; };

extern const Option kVideoSinks[];
extern const Option kVideoDecoders[];
extern const Option kAudioSinks[];
extern const int    kVideoSinkCount;
extern const int    kVideoDecoderCount;
extern const int    kAudioSinkCount;

std::string BuildArgs(const Settings& s);
std::string SerializeSettings(const Settings& s);                 // INI text
void        ParseSettings(Settings& s, const std::string& text);  // INI text

// ── UI ────────────────────────────────────────────────────────────────────────

struct UiFonts {
    ImFont* body   = nullptr;   // default text
    ImFont* small  = nullptr;   // descriptions, captions
    ImFont* strong = nullptr;   // labels, buttons
    ImFont* title  = nullptr;   // page titles
    ImFont* mono   = nullptr;   // command line, flags
};

// Operations the UI needs from the host platform.
struct UiHost {
    bool (*isRunning)();
    bool (*launch)(const Settings& s, std::string* error);
    void (*stop)();
    bool (*save)(const Settings& s);
    int  (*getRotation)();          // 0..3 quarter turns clockwise
    void (*setRotation)(int rot);
};

struct UiState {
    int         page          = 0;
    std::string savedSnapshot;      // SerializeSettings() at last load/save
    std::string error;              // persistent banner, dismissable
    std::string toast;
    double      toastUntil    = 0.0;
    bool        wasRunning    = false;
    bool        exited        = false;  // process ended on its own
    double      runningSince  = 0.0;
};

// Loads fonts at the given DPI scale. Missing files fall back to ImGui's default.
UiFonts LoadUiFonts(float scale, const char* regularTtf, const char* boldTtf, const char* monoTtf);
void    ApplyUiTheme(float scale);
void    DrawLauncherUI(Settings& s, UiState& ui, const UiHost& host, const UiFonts& fonts);

// Background color the host should clear to (matches the sidebar/content base).
ImVec4  UiClearColor();

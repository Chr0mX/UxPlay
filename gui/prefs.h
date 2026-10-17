// UxPlay GUI Launcher — UI preferences (appearance, window geometry, console).
//
// Stored separately from the receiver settings (uxplay-settings.ini) so that
// window/appearance changes never rewrite streaming options and vice versa.
// Text format is "key=value" lines; unknown keys are ignored and kept out,
// invalid values fall back to defaults, so old or hand-edited files are safe.

#pragma once

#include <string>

enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

struct UiPrefs {
    ThemeMode theme             = ThemeMode::System;
    // Normal (restored) window rectangle in virtual-screen pixels; w/h = 0 => unset.
    int       x = 0, y = 0, w = 0, h = 0;
    bool      maximized         = false;
    bool      showConsoleOnStartup = false;   // console hidden by default
    float     consoleHeight     = 240.0f;     // unscaled px
    int       page              = 0;
};

std::string SerializePrefs(const UiPrefs& p);
UiPrefs     ParsePrefs(const std::string& text);   // never throws; validates every value

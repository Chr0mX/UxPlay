// UxPlay GUI Launcher — UI preferences (see prefs.h).

#include "prefs.h"

#include <cstdlib>
#include <sstream>

std::string SerializePrefs(const UiPrefs& p)
{
    std::ostringstream f;
    f << "version=1\n";
    f << "theme="                << static_cast<int>(p.theme) << "\n";
    f << "windowX="              << p.x << "\n";
    f << "windowY="              << p.y << "\n";
    f << "windowW="              << p.w << "\n";
    f << "windowH="              << p.h << "\n";
    f << "maximized="            << (p.maximized ? 1 : 0) << "\n";
    f << "showConsoleOnStartup=" << (p.showConsoleOnStartup ? 1 : 0) << "\n";
    f << "consoleHeight="        << static_cast<int>(p.consoleHeight) << "\n";
    f << "page="                 << p.page << "\n";
    return f.str();
}

static bool ToInt(const std::string& v, long lo, long hi, long* out)
{
    if (v.empty()) return false;
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (!end || *end != '\0' || n < lo || n > hi) return false;
    *out = n;
    return true;
}

UiPrefs ParsePrefs(const std::string& text)
{
    UiPrefs p;
    std::istringstream f(text);
    std::string line;
    long n = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if      (k == "theme"         && ToInt(v, 0, 2, &n))           p.theme = static_cast<ThemeMode>(n);
        else if (k == "windowX"       && ToInt(v, -100000, 100000, &n)) p.x = (int)n;
        else if (k == "windowY"       && ToInt(v, -100000, 100000, &n)) p.y = (int)n;
        else if (k == "windowW"       && ToInt(v, 0, 100000, &n))       p.w = (int)n;
        else if (k == "windowH"       && ToInt(v, 0, 100000, &n))       p.h = (int)n;
        else if (k == "maximized"     && ToInt(v, 0, 1, &n))            p.maximized = n == 1;
        else if (k == "showConsoleOnStartup" && ToInt(v, 0, 1, &n))     p.showConsoleOnStartup = n == 1;
        else if (k == "consoleHeight" && ToInt(v, 120, 2000, &n))       p.consoleHeight = (float)n;
        else if (k == "page"          && ToInt(v, 0, 3, &n))            p.page = (int)n;
    }
    if (p.w <= 0 || p.h <= 0) { p.w = p.h = 0; }   // geometry unset unless both are valid
    return p;
}

// UxPlay GUI Launcher — design system, widgets and screens (Dear ImGui only).

#include "launcher_ui.h"

#include "imgui_internal.h"

#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>
#include <algorithm>

// ── Settings model ────────────────────────────────────────────────────────────

const Option kVideoSinks[] = {
    { "D3D11 (recommended)",  "-vs d3d11videosink"  },
    { "D3D12 (Alt+Enter FS)", "-vs d3d12videosink"  },
    { "Auto",                 ""                    },
};
const Option kVideoDecoders[] = {
    { "Hardware D3D11",    "-vd d3d11videodec" },
    { "NVIDIA NVDEC",      "-vd nvh264dec"     },
    { "Software (FFmpeg)", "-vd avdec_h264"    },
    { "Auto (decodebin)",  ""                  },
};
const Option kAudioSinks[] = {
    { "DirectSound",       "-as directsoundsink" },
    { "WASAPI",            "-as wasapisink"       },
    { "WASAPI Exclusive",  "-as wasapi2sink"      },
    { "Auto",              ""                     },
    { "Disabled",          "-a"                   },
};
const int kVideoSinkCount    = (int)(sizeof(kVideoSinks)    / sizeof(*kVideoSinks));
const int kVideoDecoderCount = (int)(sizeof(kVideoDecoders) / sizeof(*kVideoDecoders));
const int kAudioSinkCount    = (int)(sizeof(kAudioSinks)    / sizeof(*kAudioSinks));
static const int kAudioDisabledIdx = 4;

std::string BuildArgs(const Settings& s, bool redact)
{
    std::string args;
    auto append = [&](const std::string& tok) {
        if (!tok.empty()) { if (!args.empty()) args += ' '; args += tok; }
    };

    if (s.serverName[0]) { append("-n"); append(std::string("\"") + s.serverName + "\""); }
    if (s.port != 7100)  { append("-p"); append(std::to_string(s.port)); }
    if (s.fullscreen)    append("-fs");
    if (s.windowSize[0] && std::string(s.windowSize) != "1920x1080") {
        append("-s"); append(s.windowSize);
    }
    if (s.pin)           append("-pin");
    if (s.password[0])   { append("-pw"); append(redact ? std::string("\"***\"") : std::string("\"") + s.password + "\""); }

    append(kVideoSinks[s.videoSinkIdx].flag);
    append(kVideoDecoders[s.videoDecoderIdx].flag);
    if (s.h265) append("-h265");
    if (s.fps > 0) { append("-fps"); append(std::to_string(s.fps)); }
    append(kAudioSinks[s.audioSinkIdx].flag);
    if (s.volume < 0.995f) {
        char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "%.2f", s.volume);
        append("-vol"); append(vbuf);
    }
    if (s.noFreeze) append("-nofreeze");
    if (s.debug)    append("-d");
    if (s.extraArgs[0]) append(s.extraArgs);

    return args;
}

std::string SerializeSettings(const Settings& s)
{
    std::ostringstream f;
    f << "serverName="      << s.serverName    << "\n";
    f << "port="            << s.port          << "\n";
    f << "fullscreen="      << (s.fullscreen ? 1 : 0) << "\n";
    f << "windowSize="      << s.windowSize    << "\n";
    f << "pin="             << (s.pin ? 1 : 0) << "\n";
    f << "password="        << s.password      << "\n";
    f << "videoSinkIdx="    << s.videoSinkIdx  << "\n";
    f << "videoDecoderIdx=" << s.videoDecoderIdx << "\n";
    f << "h265="            << (s.h265 ? 1 : 0) << "\n";
    f << "audioSinkIdx="    << s.audioSinkIdx  << "\n";
    f << "volume="          << s.volume        << "\n";
    f << "fps="             << s.fps           << "\n";
    f << "noFreeze="        << (s.noFreeze ? 1 : 0) << "\n";
    f << "debug="           << (s.debug ? 1 : 0) << "\n";
    f << "extraArgs="       << s.extraArgs     << "\n";
    return f.str();
}

void ParseSettings(Settings& s, const std::string& text)
{
    std::istringstream f(text);
    std::string line;
    auto toInt = [](const std::string& v, int fallback) {
        try { return std::stoi(v); } catch (...) { return fallback; }
    };
    auto copy = [](char* dst, size_t n, const std::string& v) {
        strncpy(dst, v.c_str(), n - 1); dst[n - 1] = 0;
    };
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if      (key == "serverName")      copy(s.serverName, sizeof(s.serverName), val);
        else if (key == "port")            s.port = toInt(val, s.port);
        else if (key == "fullscreen")      s.fullscreen = (val == "1");
        else if (key == "windowSize")      copy(s.windowSize, sizeof(s.windowSize), val);
        else if (key == "pin")             s.pin = (val == "1");
        else if (key == "password")        copy(s.password, sizeof(s.password), val);
        else if (key == "videoSinkIdx")    s.videoSinkIdx    = toInt(val, s.videoSinkIdx);
        else if (key == "videoDecoderIdx") s.videoDecoderIdx = toInt(val, s.videoDecoderIdx);
        else if (key == "h265")            s.h265 = (val == "1");
        else if (key == "audioSinkIdx")    s.audioSinkIdx    = toInt(val, s.audioSinkIdx);
        else if (key == "volume")          { try { s.volume = std::stof(val); } catch (...) {} }
        else if (key == "fps")             s.fps = toInt(val, s.fps);
        else if (key == "noFreeze")        s.noFreeze = (val == "1");
        else if (key == "debug")           s.debug = (val == "1");
        else if (key == "extraArgs")       copy(s.extraArgs, sizeof(s.extraArgs), val);
    }
    auto clamp = [](int& v, int lo, int hi) { if (v < lo) v = lo; if (v > hi) v = hi; };
    clamp(s.videoSinkIdx,    0, kVideoSinkCount    - 1);
    clamp(s.videoDecoderIdx, 0, kVideoDecoderCount - 1);
    clamp(s.audioSinkIdx,    0, kAudioSinkCount    - 1);
    clamp(s.port,            1, 65535);
    if (!(s.volume >= 0.0f)) s.volume = 0.0f;   // also catches NaN
    if (s.volume > 1.0f) s.volume = 1.0f;
    if (s.fps < 0 || s.fps > 256) s.fps = 0;
}

// ── Design tokens ─────────────────────────────────────────────────────────────
// One palette per theme, one spacing scale, one radius scale. Every widget
// below reads colors from `P`; nothing else in the file contains a literal color.

static ImVec4 Hex(uint32_t rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

struct Palette {
    bool   dark;
    // Surfaces, back to front
    ImVec4 Base, SidebarTop, Sidebar, MenuBar, Footer, Surface, Raised, RaisedHover, RaisedPress, Code;
    ImVec4 Border, BorderStrong, BorderHover, Highlight, Shadow, Overlay, Popup;
    // Text
    ImVec4 Text, TextMuted, TextFaint, OnAccent, CodeText;
    // Accent and semantic
    ImVec4 Accent, AccentHover, AccentPress, AccentSoft, AccentGlyph;
    ImVec4 Success, Warning, Danger, DangerHover, DangerPress, TrackOff, TrackOffHover, Knob;
};

static const Palette kDark = {
    true,
    Hex(0x0C0D10), Hex(0x15161C), Hex(0x111217), Hex(0x111217), Hex(0x101116), Hex(0x16181D),
    Hex(0x1E2027), Hex(0x262932), Hex(0x2D303A), Hex(0x0F1014),
    Hex(0x23262E), Hex(0x323642), Hex(0x404452), Hex(0xFFFFFF, 0.05f), Hex(0x000000, 0.35f),
    Hex(0xFFFFFF, 0.05f), Hex(0x1A1C22, 0.98f),
    Hex(0xE9EAEE), Hex(0x9297A3), Hex(0x6A6F7B), Hex(0xFFFFFF), Hex(0xC5C8F9),
    Hex(0x6D6AF2), Hex(0x8280F5), Hex(0x5A57DE), Hex(0x6D6AF2, 0.16f), Hex(0xA9A7FF),
    Hex(0x3DBE85), Hex(0xE6A23C), Hex(0xE5484D), Hex(0xEC5F63), Hex(0xCC3B40),
    Hex(0x2B2E37), Hex(0x343845), Hex(0xC9CCD4),
};

static const Palette kLight = {
    false,
    Hex(0xF3F4F7), Hex(0xEDEEF3), Hex(0xE9EAF0), Hex(0xE9EAF0), Hex(0xFBFBFD), Hex(0xFFFFFF),
    Hex(0xF5F6F9), Hex(0xEDEFF4), Hex(0xE4E7ED), Hex(0xF0F1F5),
    Hex(0xDCDEE5), Hex(0xC9CCD6), Hex(0xB4B8C4), Hex(0xFFFFFF, 0.7f), Hex(0x1B1D2E, 0.07f),
    Hex(0x1B1D2E, 0.05f), Hex(0xFFFFFF, 0.99f),
    Hex(0x15161B), Hex(0x585D6B), Hex(0x7E8391), Hex(0xFFFFFF), Hex(0x3E3BB0),
    Hex(0x5753E3), Hex(0x6662EE), Hex(0x4744CC), Hex(0x5753E3, 0.12f), Hex(0x4C48D8),
    Hex(0x1E9A60), Hex(0xB9720B), Hex(0xD3343A), Hex(0xDE474C), Hex(0xBA2A30),
    Hex(0xCBCED8), Hex(0xBDC1CC), Hex(0xFFFFFF),
};

static const Palette* P = &kDark;

namespace tok {
    // Spacing (unscaled px; multiplied by g_scale)
    static const float S1 = 4, S2 = 8, S3 = 12, S4 = 16, S5 = 24, S6 = 32;
    // Radii
    static const float RSmall = 5, RMed = 7, RLarge = 10;
    // Fixed layout
    static const float SidebarW = 216, SidebarCompactW = 64, FooterH = 68, StatusH = 64;
    static const float ControlW = 248, ToggleW = 38, ToggleH = 22;
}

static float   g_scale = 1.0f;
static bool    g_reduceMotion = false;
static UiFonts g_fonts;

static float Px(float v) { return std::floor(v * g_scale + 0.5f); }
static ImU32 Col(const ImVec4& c, float alphaMul = 1.0f) {
    ImVec4 v = c; v.w *= alphaMul;
    return ImGui::GetColorU32(v);   // honours style.Alpha (disabled state)
}
static ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
static ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

// Exponential approach animation stored per-widget in ImGui's state storage.
// With reduced motion the value jumps straight to its target.
static float Animate(ImGuiID id, float target, float speed = 16.0f) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float* v = st->GetFloatRef(id, -1.0f);
    if (*v < 0.0f || g_reduceMotion) { *v = target; return target; }
    *v += (target - *v) * ImMin(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (std::fabs(*v - target) < 0.001f) *v = target;
    return *v;
}
static bool NavCursorOn() { return ImGui::GetCurrentContext()->NavCursorVisible; }
static void FocusRing(const ImVec2& a, const ImVec2& b, float r) {
    if (ImGui::IsItemFocused() && NavCursorOn()) {
        float o = Px(2);
        ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - o, a.y - o), ImVec2(b.x + o, b.y + o),
                                            Col(P->AccentHover, 0.9f), r + o, 0, Px(1.5f));
    }
}

// ── Fonts and theme ───────────────────────────────────────────────────────────

static bool FileExists(const char* p) {
    if (!p || !*p) return false;
    FILE* f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

UiFonts LoadUiFonts(float scale, const char* regular, const char* bold, const char* mono)
{
    g_scale = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x25A0, 0x25FF, 0 };
    auto load = [&](const char* path, float px) -> ImFont* {
        ImFontConfig cfg;
        cfg.OversampleH = 2; cfg.OversampleV = 2;
        if (FileExists(path))
            return io.Fonts->AddFontFromFileTTF(path, px * scale, &cfg, ranges);
        cfg.SizePixels = px * scale;
        return io.Fonts->AddFontDefault(&cfg);
    };
    UiFonts f;
    f.body   = load(regular, 15.0f);    // first font = ImGui default font
    f.small  = load(regular, 13.0f);
    f.strong = load(FileExists(bold) ? bold : regular, 15.0f);
    f.title  = load(FileExists(bold) ? bold : regular, 22.0f);
    f.mono   = load(FileExists(mono) ? mono : regular, 13.5f);
    g_fonts = f;
    return f;
}

ImVec4 UiClearColor() { return P->Base; }

void ApplyUiTheme(float scale, bool dark)
{
    g_scale = scale;
    P = dark ? &kDark : &kLight;
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();

    st.WindowPadding     = ImVec2(10, 10);
    st.WindowBorderSize  = 0;
    st.WindowRounding    = tok::RLarge;
    st.ChildBorderSize   = 0;
    st.PopupRounding     = tok::RLarge;
    st.PopupBorderSize   = 1;
    st.FramePadding      = ImVec2(10, 7);
    st.FrameRounding     = tok::RMed;
    st.FrameBorderSize   = 1;
    st.ItemSpacing       = ImVec2(tok::S2, tok::S2);
    st.ItemInnerSpacing  = ImVec2(tok::S2, tok::S1);
    st.ScrollbarSize     = 10;
    st.ScrollbarRounding = 10;
    st.GrabMinSize       = 14;
    st.GrabRounding      = 10;
    st.TabRounding       = tok::RSmall;
    st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    st.HoverDelayNormal  = 0.45f;
    st.HoverFlagsForTooltipMouse = ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_Stationary;
    st.HoverFlagsForTooltipNav   = ImGuiHoveredFlags_NoSharedDelay | ImGuiHoveredFlags_DelayNormal;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = P->Text;
    c[ImGuiCol_TextDisabled]         = P->TextFaint;
    c[ImGuiCol_WindowBg]             = P->Base;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = P->Popup;
    c[ImGuiCol_MenuBarBg]            = P->MenuBar;
    c[ImGuiCol_Border]               = P->Border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = P->Raised;
    c[ImGuiCol_FrameBgHovered]       = P->RaisedHover;
    c[ImGuiCol_FrameBgActive]        = P->RaisedPress;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = P->BorderStrong;
    c[ImGuiCol_ScrollbarGrabHovered] = P->BorderHover;
    c[ImGuiCol_ScrollbarGrabActive]  = P->TextFaint;
    c[ImGuiCol_CheckMark]            = P->Accent;
    c[ImGuiCol_SliderGrab]           = P->Accent;
    c[ImGuiCol_SliderGrabActive]     = P->AccentHover;
    c[ImGuiCol_Button]               = P->Raised;
    c[ImGuiCol_ButtonHovered]        = P->RaisedHover;
    c[ImGuiCol_ButtonActive]         = P->RaisedPress;
    c[ImGuiCol_Header]               = P->AccentSoft;
    c[ImGuiCol_HeaderHovered]        = P->Overlay;
    c[ImGuiCol_HeaderActive]         = WithAlpha(P->Accent, 0.24f);
    c[ImGuiCol_Separator]            = P->Border;
    c[ImGuiCol_TextSelectedBg]       = WithAlpha(P->Accent, 0.35f);
    c[ImGuiCol_NavCursor]            = P->AccentHover;
    c[ImGuiCol_ModalWindowDimBg]     = Hex(0x000000, P->dark ? 0.55f : 0.30f);

    st.ScaleAllSizes(scale);
}

// ── Icons ─────────────────────────────────────────────────────────────────────
// One hand-drawn family: 16-unit grid, 1.5-unit strokes, rounded geometry.

enum class Icon { Logo, Connection, Display, Audio, Advanced, Copy, Play, Stop, Save, Rotate,
                  Alert, Close, Check, Console, Restart, Trash, File, Off, Follow };

static void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 col)
{
    const float u = size / 16.0f;
    const float w = ImMax(1.0f, 1.5f * u);
    auto Pt = [&](float x, float y) { return ImVec2(c.x + (x - 8) * u, c.y + (y - 8) * u); };
    switch (icon) {
    case Icon::Logo:
    case Icon::Connection:   // AirPlay: open screen + triangle
        dl->PathLineTo(Pt(5, 12)); dl->PathLineTo(Pt(2.5f, 12)); dl->PathLineTo(Pt(2.5f, 2.5f));
        dl->PathLineTo(Pt(13.5f, 2.5f)); dl->PathLineTo(Pt(13.5f, 12)); dl->PathLineTo(Pt(11, 12));
        dl->PathStroke(col, 0, w);
        dl->AddTriangleFilled(Pt(8, 9), Pt(12, 14.5f), Pt(4, 14.5f), col);
        break;
    case Icon::Display:
        dl->AddRect(Pt(1.5f, 2.5f), Pt(14.5f, 11.5f), col, 1.5f * u, 0, w);
        dl->AddLine(Pt(8, 11.5f), Pt(8, 14), col, w);
        dl->AddLine(Pt(5, 14), Pt(11, 14), col, w);
        break;
    case Icon::Audio:
        dl->PathLineTo(Pt(2, 6)); dl->PathLineTo(Pt(5, 6)); dl->PathLineTo(Pt(9, 2.5f));
        dl->PathLineTo(Pt(9, 13.5f)); dl->PathLineTo(Pt(5, 10)); dl->PathLineTo(Pt(2, 10));
        dl->PathStroke(col, ImDrawFlags_Closed, w);
        dl->PathArcTo(Pt(9, 8), 3.5f * u, -0.9f, 0.9f, 10); dl->PathStroke(col, 0, w);
        dl->PathArcTo(Pt(9, 8), 6.0f * u, -0.9f, 0.9f, 12); dl->PathStroke(col, 0, w);
        break;
    case Icon::Advanced:
        for (int i = 0; i < 3; ++i) {
            float y = 3.5f + i * 4.5f, k = (i == 1) ? 10.5f : (i == 0 ? 5.5f : 8.0f);
            dl->AddLine(Pt(2, y), Pt(14, y), col, w);
            dl->AddCircleFilled(Pt(k, y), 2.0f * u, col);
        }
        break;
    case Icon::Copy:
        dl->AddRect(Pt(5, 5), Pt(14, 14), col, 1.5f * u, 0, w);
        dl->PathLineTo(Pt(11, 2.5f)); dl->PathLineTo(Pt(2.5f, 2.5f)); dl->PathLineTo(Pt(2.5f, 11));
        dl->PathStroke(col, 0, w);
        break;
    case Icon::Play:
        dl->AddTriangleFilled(Pt(4.5f, 2.5f), Pt(13.5f, 8), Pt(4.5f, 13.5f), col);
        break;
    case Icon::Stop:
        dl->AddRectFilled(Pt(3.5f, 3.5f), Pt(12.5f, 12.5f), col, 1.5f * u);
        break;
    case Icon::Save:
    case Icon::Check:
        dl->PathLineTo(Pt(2.5f, 8.5f)); dl->PathLineTo(Pt(6.5f, 12.5f)); dl->PathLineTo(Pt(13.5f, 3.5f));
        dl->PathStroke(col, 0, w * 1.15f);
        break;
    case Icon::Rotate:
    case Icon::Restart:
        dl->PathArcTo(Pt(8, 8.5f), 5.5f * u, IM_PI * 1.15f, IM_PI * 2.75f, 20);
        dl->PathStroke(col, 0, w);
        dl->AddTriangleFilled(Pt(11.5f, 1.5f), Pt(14.5f, 5.5f), Pt(10, 6), col);
        break;
    case Icon::Alert:
        dl->PathLineTo(Pt(8, 1.8f)); dl->PathLineTo(Pt(14.8f, 13.8f)); dl->PathLineTo(Pt(1.2f, 13.8f));
        dl->PathStroke(col, ImDrawFlags_Closed, w);
        dl->AddLine(Pt(8, 6), Pt(8, 9.5f), col, w * 1.15f);
        dl->AddCircleFilled(Pt(8, 11.7f), 0.95f * u, col);
        break;
    case Icon::Close:
        dl->AddLine(Pt(4, 4), Pt(12, 12), col, w);
        dl->AddLine(Pt(12, 4), Pt(4, 12), col, w);
        break;
    case Icon::Console:
        dl->AddRect(Pt(1.5f, 2.5f), Pt(14.5f, 13.5f), col, 1.5f * u, 0, w);
        dl->PathLineTo(Pt(4.5f, 6)); dl->PathLineTo(Pt(7, 8)); dl->PathLineTo(Pt(4.5f, 10));
        dl->PathStroke(col, 0, w);
        dl->AddLine(Pt(8.5f, 10.5f), Pt(11.5f, 10.5f), col, w);
        break;
    case Icon::Trash:
        dl->AddLine(Pt(2.5f, 4), Pt(13.5f, 4), col, w);
        dl->PathLineTo(Pt(6, 4)); dl->PathLineTo(Pt(6.5f, 2)); dl->PathLineTo(Pt(9.5f, 2)); dl->PathLineTo(Pt(10, 4));
        dl->PathStroke(col, 0, w);
        dl->PathLineTo(Pt(4, 4)); dl->PathLineTo(Pt(4.8f, 14)); dl->PathLineTo(Pt(11.2f, 14)); dl->PathLineTo(Pt(12, 4));
        dl->PathStroke(col, 0, w);
        break;
    case Icon::File:
        dl->PathLineTo(Pt(9.5f, 1.8f)); dl->PathLineTo(Pt(3, 1.8f)); dl->PathLineTo(Pt(3, 14.2f));
        dl->PathLineTo(Pt(13, 14.2f)); dl->PathLineTo(Pt(13, 5.3f)); dl->PathLineTo(Pt(9.5f, 1.8f));
        dl->PathLineTo(Pt(9.5f, 5.3f)); dl->PathLineTo(Pt(13, 5.3f));
        dl->PathStroke(col, 0, w);
        break;
    case Icon::Off:
        dl->AddCircle(c, 6.0f * u, col, 24, w);
        break;
    case Icon::Follow:   // arrow down to a baseline: "stick to the newest line"
        dl->AddLine(Pt(8, 2), Pt(8, 10.5f), col, w);
        dl->PathLineTo(Pt(4.5f, 7.5f)); dl->PathLineTo(Pt(8, 11)); dl->PathLineTo(Pt(11.5f, 7.5f));
        dl->PathStroke(col, 0, w);
        dl->AddLine(Pt(3, 14), Pt(13, 14), col, w);
        break;
    }
}

// Indeterminate progress ring (static arc when reduced motion is on).
static void DrawSpinner(ImDrawList* dl, ImVec2 c, float r, ImU32 track, ImU32 col)
{
    dl->AddCircle(c, r, track, 32, Px(2));
    float t = g_reduceMotion ? 0.0f : (float)ImGui::GetTime() * 5.0f;
    dl->PathArcTo(c, r, t, t + IM_PI * 0.6f, 16);
    dl->PathStroke(col, 0, Px(2));
}

// ── Primitives ────────────────────────────────────────────────────────────────

static void TextColored(ImFont* font, const ImVec4& color, const char* text)
{
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static std::string Ellipsize(ImFont* font, const std::string& s, float maxW)
{
    ImGui::PushFont(font);
    std::string out = s;
    if (ImGui::CalcTextSize(out.c_str()).x > maxW) {
        while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > maxW) {
            out.pop_back();
            while (!out.empty() && (out.back() & 0xC0) == 0x80) out.pop_back();  // keep UTF-8 whole
        }
        out += "...";
    }
    ImGui::PopFont();
    return out;
}

static std::string FormatDuration(double secs)
{
    int s = (int)ImMax(0.0, secs);
    char buf[32];
    if (s >= 3600) snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else           snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// Raised surface with shadow, 1px border and a faint top highlight.
static void DrawSurface(ImDrawList* dl, ImVec2 a, ImVec2 b, const ImVec4& fill, float r, bool shadow = true)
{
    if (shadow) {
        dl->AddRectFilled(ImVec2(a.x, a.y + Px(2)), ImVec2(b.x, b.y + Px(3)), Col(P->Shadow, 0.6f), r);
        dl->AddRectFilled(ImVec2(a.x, a.y + Px(1)), ImVec2(b.x, b.y + Px(1)), Col(P->Shadow), r);
    }
    dl->AddRectFilled(a, b, Col(fill), r);
    dl->AddRect(a, b, Col(P->Border), r, 0, 1.0f);
    dl->AddLine(ImVec2(a.x + r, a.y + 0.5f), ImVec2(b.x - r, a.y + 0.5f), Col(P->Highlight), 1.0f);
}

enum class BtnKind { Primary, Secondary, Danger, Ghost };

static bool ActionButton(const char* label, Icon icon, bool withIcon, ImVec2 size, BtnKind kind,
                         const char* tooltip = nullptr)
{
    ImGui::PushID(label);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##btn", size);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    float h = Animate(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 20.0f);

    ImVec4 base, hov, act, fg = P->Text;
    switch (kind) {
    case BtnKind::Primary:   base = P->Accent; hov = P->AccentHover; act = P->AccentPress; fg = P->OnAccent; break;
    case BtnKind::Danger:    base = P->Danger; hov = P->DangerHover; act = P->DangerPress; fg = P->OnAccent; break;
    case BtnKind::Secondary: base = P->Raised; hov = P->RaisedHover; act = P->RaisedPress; break;
    case BtnKind::Ghost:     base = WithAlpha(P->Overlay, 0.0f); hov = P->Overlay;
                             act = WithAlpha(P->Overlay, P->Overlay.w * 2.0f);
                             fg = Mix(P->TextMuted, P->Text, h); break;
    }
    ImVec4 fill = held ? act : Mix(base, hov, h);
    ImVec2 b(p.x + size.x, p.y + size.y);
    float r = Px(tok::RMed);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (kind != BtnKind::Ghost) {
        dl->AddRectFilled(ImVec2(p.x, p.y + Px(1)), ImVec2(b.x, b.y + Px(1)), Col(P->Shadow), r);
        dl->AddRectFilled(p, b, Col(fill), r);
        if (kind == BtnKind::Secondary) dl->AddRect(p, b, Col(Mix(P->BorderStrong, P->BorderHover, h)), r);
        dl->AddLine(ImVec2(p.x + r, p.y + 0.5f), ImVec2(b.x - r, p.y + 0.5f),
                    Col(kind == BtnKind::Secondary ? P->Highlight : Hex(0xFFFFFF, 0.18f)));
    } else {
        dl->AddRectFilled(p, b, Col(fill), r);
    }

    ImGui::PushFont(g_fonts.strong);
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    float iconSz = Px(14), gap = (withIcon && end != label) ? Px(8) : 0.0f;
    float total = ts.x + (withIcon ? iconSz + gap : 0.0f);
    float x = p.x + (size.x - total) * 0.5f, cy = p.y + size.y * 0.5f;
    if (withIcon) { DrawIcon(dl, icon, ImVec2(x + iconSz * 0.5f, cy), iconSz, Col(fg)); x += iconSz + gap; }
    dl->AddText(ImVec2(x, cy - ts.y * 0.5f), Col(fg), label, end);
    ImGui::PopFont();

    FocusRing(p, b, r);
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    ImGui::PopID();
    return pressed;
}

// Square icon-only button; always has a tooltip (its accessible name).
static bool IconButton(const char* id, Icon icon, const char* tooltip, float size = 28.0f, bool active = false)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float s = Px(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), Col(P->AccentSoft), Px(tok::RMed));
    bool pressed = ActionButton(id, icon, false, ImVec2(s, s), BtnKind::Ghost, tooltip);
    ImU32 col = Col(active ? P->AccentGlyph : (ImGui::IsItemHovered() ? P->Text : P->TextMuted));
    DrawIcon(dl, icon, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), Px(14), col);
    return pressed;
}

static bool Toggle(const char* id, bool* v)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float fh = ImGui::GetFrameHeight();
    float w = Px(tok::ToggleW), h = Px(tok::ToggleH);
    bool changed = ImGui::InvisibleButton(id, ImVec2(w, fh));
    if (changed) *v = !*v;
    bool hovered = ImGui::IsItemHovered();
    float t = Animate(ImGui::GetItemID(), *v ? 1.0f : 0.0f, 18.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(p.x, p.y + (fh - h) * 0.5f), b(a.x + w, a.y + h);
    ImVec4 off = hovered ? P->TrackOffHover : P->TrackOff;
    ImVec4 on  = hovered ? P->AccentHover : P->Accent;
    dl->AddRectFilled(a, b, Col(Mix(off, on, t)), h * 0.5f);
    dl->AddRect(a, b, Col(Hex(0x000000, P->dark ? 0.25f : 0.06f)), h * 0.5f);
    float rad = h * 0.5f - Px(3);
    ImVec2 k(a.x + h * 0.5f + (w - h) * t, a.y + h * 0.5f);
    dl->AddCircleFilled(ImVec2(k.x, k.y + Px(1)), rad, Col(P->Shadow), 20);
    dl->AddCircleFilled(k, rad, Col(Mix(P->Knob, Hex(0xFFFFFF), t)), 20);
    FocusRing(a, b, h * 0.5f);
    return changed;
}

static bool ComboOptions(const char* id, int* current, const Option* opts, int count)
{
    const char* preview = (*current >= 0 && *current < count) ? opts[*current].label : "";
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(6), Px(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, Px(2)));
    if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < count; ++i) {
            bool sel = (*current == i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable(opts[i].label, sel, 0, ImVec2(0, Px(28)))) { *current = i; changed = true; }
            if (opts[i].flag[0]) {   // show the flag each choice maps to, right aligned
                ImGui::PushFont(g_fonts.mono);
                ImVec2 ts = ImGui::CalcTextSize(opts[i].flag);
                float x = p.x + ImGui::GetContentRegionAvail().x - ts.x - Px(8);
                ImGui::GetWindowDrawList()->AddText(ImVec2(x, p.y + (Px(28) - ts.y) * 0.5f),
                                                    Col(P->TextFaint), opts[i].flag);
                ImGui::PopFont();
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar(2);
    return changed;
}

// ── Layout: sections, cards and setting rows ─────────────────────────────────

struct CardCtx {
    ImVec2 start;
    float  width = 0;
    int    rows  = 0;
    ImDrawListSplitter split;
    float  rowX = 0, rowEndY = 0;
};
static CardCtx g_card;
static float   g_colW = 1e9f;   // readable content column width for this frame

static void SectionHeader(const char* title, const char* caption = nullptr)
{
    ImGui::Dummy(ImVec2(0, Px(tok::S2)));
    TextColored(g_fonts.strong, P->Text, title);
    if (caption) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4));
        TextColored(g_fonts.small, P->TextMuted, caption);
    }
    ImGui::Dummy(ImVec2(0, Px(2)));
}

static void BeginCard()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    g_card.start = ImGui::GetCursorScreenPos();
    g_card.width = ImMin(ImGui::GetContentRegionAvail().x, g_colW);
    g_card.rows  = 0;
    g_card.split.Split(dl, 2);
    g_card.split.SetCurrentChannel(dl, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(tok::S2), 0));
    ImGui::BeginGroup();
}

static void EndCard()
{
    ImGui::EndGroup();
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = g_card.start;
    ImVec2 b(a.x + g_card.width, ImGui::GetItemRectMax().y);
    g_card.split.SetCurrentChannel(dl, 0);
    DrawSurface(dl, a, b, P->Surface, Px(tok::RLarge));
    g_card.split.Merge(dl);
    ImGui::Dummy(ImVec2(0, Px(tok::S4)));
}

// Starts a labelled row inside a card and leaves the cursor where the control goes.
// controlW <= 0 means "full width under the label".
static void BeginRow(const char* label, const char* desc, const char* flag = nullptr,
                     float controlW = tok::ControlW)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float padX = Px(tok::S4 + 2), padY = Px(tok::S3 + 1);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float  w = g_card.width;

    if (g_card.rows++ > 0)
        dl->AddLine(ImVec2(p.x + padX, p.y), ImVec2(p.x + w - padX, p.y), Col(P->Border));

    bool stacked = controlW <= 0.0f || w < Px(520);   // narrow: control goes under text
    float ctrlW  = controlW <= 0.0f ? (w - padX * 2) : ImMin(Px(controlW), w - padX * 2);
    bool compact = controlW > 0.0f && controlW <= tok::ToggleW + 1.0f;   // toggles stay inline
    if (compact) stacked = false;

    float textW = stacked ? (w - padX * 2) : (w - padX * 3 - ctrlW);
    ImGui::PushFont(g_fonts.strong);
    float lh = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    ImGui::PushFont(g_fonts.small);
    float dh = desc ? ImGui::CalcTextSize(desc, nullptr, false, textW).y + Px(3) : 0.0f;
    ImGui::PopFont();
    float textH = lh + dh;
    float fh = ImGui::GetFrameHeight();
    float rowH = stacked ? (padY * 2 + textH + Px(tok::S2 + 2) + fh) : (padY * 2 + ImMax(textH, fh));

    float ty = stacked ? p.y + padY : p.y + (rowH - textH) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(p.x + padX, ty));
    TextColored(g_fonts.strong, P->Text, label);
    if (flag) {
        ImGui::PushFont(g_fonts.strong);
        float lx = ImGui::CalcTextSize(label).x;
        ImGui::PopFont();
        ImGui::PushFont(g_fonts.mono);
        ImVec2 fs = ImGui::CalcTextSize(flag);
        ImVec2 fa(p.x + padX + lx + Px(8), ty + (lh - fs.y) * 0.5f - Px(1));
        ImVec2 fb(fa.x + fs.x + Px(10), fa.y + fs.y + Px(2));
        dl->AddRectFilled(fa, fb, Col(P->Overlay), Px(tok::RSmall));
        dl->AddRect(fa, fb, Col(P->Border), Px(tok::RSmall));
        dl->AddText(ImVec2(fa.x + Px(5), fa.y + Px(1)), Col(P->TextFaint), flag);
        ImGui::PopFont();
    }
    if (desc) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + padX, ty + lh + Px(3)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
        TextColored(g_fonts.small, P->TextMuted, desc);
        ImGui::PopTextWrapPos();
    }

    float cx = stacked ? p.x + padX : p.x + w - padX - ctrlW;
    float cy = stacked ? p.y + padY + textH + Px(tok::S2 + 2) : p.y + (rowH - fh) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(cx, cy));
    ImGui::SetNextItemWidth(ctrlW);
    g_card.rowX = p.x;
    g_card.rowEndY = p.y + rowH;
    ImGui::PushID(label);
}

static void EndRow()
{
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(g_card.rowX, g_card.rowEndY));
    ImGui::Dummy(ImVec2(g_card.width, 0));
}

static void ToggleRow(const char* label, const char* desc, const char* flag, bool* v)
{
    BeginRow(label, desc, flag, tok::ToggleW);
    Toggle(label, v);   // label doubles as the accessible name
    EndRow();
}

static void ShowToast(UiState& ui, const char* text, bool isError = false)
{
    ui.toast = text;
    ui.toastError = isError;
    ui.toastUntil = ImGui::GetTime() + (isError ? 4.0 : 2.2);
}

// ── Pages ─────────────────────────────────────────────────────────────────────

struct Page { const char* name; const char* subtitle; Icon icon; };
static const Page kPages[] = {
    { "Connection", "How this PC appears to Apple devices and who can connect.", Icon::Connection },
    { "Display",    "Player window, video decoding and frame pacing.",          Icon::Display    },
    { "Audio",      "Where mirrored audio is played and how loud.",              Icon::Audio      },
    { "Advanced",   "Diagnostics, custom flags and the exact command line.",     Icon::Advanced   },
};
static const int kPageCount = (int)(sizeof(kPages) / sizeof(*kPages));

static void PageConnection(Settings& s)
{
    SectionHeader("Network", "How Apple devices find and reach this receiver.");
    BeginCard();
    BeginRow("Server name", "Name shown in the AirPlay menu on iPhone, iPad and Mac.", "-n");
    ImGui::InputTextWithHint("##v", "Use this PC's hostname", s.serverName, sizeof(s.serverName));
    EndRow();
    BeginRow("Port", "Base TCP port for AirPlay. Change only if 7100 is taken.", "-p", 120);
    ImGui::InputInt("##v", &s.port, 0);
    if (s.port < 1) s.port = 1;
    if (s.port > 65535) s.port = 65535;
    EndRow();
    EndCard();

    SectionHeader("Security", "Control who is allowed to mirror to this PC.");
    BeginCard();
    ToggleRow("Require PIN", "Show a one-time PIN on screen the first time a device connects.", "-pin", &s.pin);
    BeginRow("Password", "Devices must enter this password to mirror. Leave empty for none.", "-pw");
    ImGui::InputTextWithHint("##v", "No password", s.password, sizeof(s.password), ImGuiInputTextFlags_Password);
    EndRow();
    EndCard();
}

static void PageDisplay(Settings& s, const UiHost& host)
{
    SectionHeader("Player window");
    BeginCard();
    ToggleRow("Start in fullscreen", "Open the player window fullscreen when a device connects.", "-fs", &s.fullscreen);
    BeginRow("Window size", "Initial player size as WIDTHxHEIGHT.", "-s", 160);
    ImGui::InputTextWithHint("##v", "1920x1080", s.windowSize, sizeof(s.windowSize));
    EndRow();
    {
        static const char* kRot[] = { "0\xc2\xb0", "90\xc2\xb0", "180\xc2\xb0", "270\xc2\xb0" };
        bool running = host.isRunning();
        int rot = host.getRotation();
        char desc[96];
        snprintf(desc, sizeof(desc), running ? "Rotates the live player view, currently %s clockwise."
                                             : "Available while the receiver is running.", kRot[rot]);
        BeginRow("Rotation", desc, nullptr, rot ? 216 : 120);
        ImGui::BeginDisabled(!running);
        float bh = ImGui::GetFrameHeight();
        if (ActionButton("Rotate 90\xc2\xb0", Icon::Rotate, true, ImVec2(Px(120), bh), BtnKind::Secondary,
                         "Rotate the player window clockwise without reconnecting (Ctrl+R)"))
            host.setRotation((rot + 1) % 4);
        if (rot != 0) {
            ImGui::SameLine(0, Px(tok::S2));
            if (ActionButton("Reset", Icon::Rotate, false, ImVec2(Px(88), bh), BtnKind::Ghost,
                             "Reset rotation (Ctrl+Shift+R)"))
                host.setRotation(0);
        }
        ImGui::EndDisabled();
        EndRow();
    }
    EndCard();

    SectionHeader("Video playback");
    BeginCard();
    BeginRow("Video sink", "Renderer used to present frames.", "-vs");
    ComboOptions("##v", &s.videoSinkIdx, kVideoSinks, kVideoSinkCount);
    EndRow();
    BeginRow("Video decoder", "Auto picks hardware decoding and falls back gracefully.", "-vd");
    ComboOptions("##v", &s.videoDecoderIdx, kVideoDecoders, kVideoDecoderCount);
    EndRow();
    ToggleRow("H.265 / 4K", "Allow HEVC streams for 4K mirroring on supported devices.", "-h265", &s.h265);
    BeginRow("Frame rate limit", "Maximum frames per second requested from the device.", "-fps");
    ImGui::SliderInt("##v", &s.fps, 0, 256, s.fps == 0 ? "Default" : "%d fps");
    EndRow();
    EndCard();
}

static void PageAudio(Settings& s)
{
    SectionHeader("Audio output");
    BeginCard();
    BeginRow("Audio sink", "Output API. Choose Disabled to mirror video only.", "-as");
    ComboOptions("##v", &s.audioSinkIdx, kAudioSinks, kAudioSinkCount);
    EndRow();
    BeginRow("Volume", s.audioSinkIdx == kAudioDisabledIdx ? "Audio output is disabled."
                                                           : "Initial playback volume.", "-vol");
    ImGui::BeginDisabled(s.audioSinkIdx == kAudioDisabledIdx);
    int pct = (int)std::lround(s.volume * 100.0f);
    if (ImGui::SliderInt("##v", &pct, 0, 100, "%d%%")) s.volume = pct / 100.0f;
    ImGui::EndDisabled();
    EndRow();
    EndCard();
}

static void PageAdvanced(Settings& s, UiState& ui, UiPrefs& prefs, const std::string& cmd)
{
    SectionHeader("Session behavior");
    BeginCard();
    ToggleRow("Close window on disconnect", "Close the video window when a device disconnects instead of freezing on the last frame.",
              "-nofreeze", &s.noFreeze);
    ToggleRow("Debug logging", "Have uxplay.exe print detailed GStreamer and protocol messages.", "-d", &s.debug);
    EndCard();

    SectionHeader("Launcher");
    BeginCard();
    {
        bool v = prefs.showConsoleOnStartup;
        ToggleRow("Show console on startup", "Open the console panel when the launcher starts. Output is always "
                  "captured and logged, whether or not the console is visible.", nullptr, &v);
        if (v != prefs.showConsoleOnStartup) { prefs.showConsoleOnStartup = v; ui.prefsDirty = true; }
    }
    EndCard();

    SectionHeader("Custom arguments", "Appended verbatim after the generated options.");
    BeginCard();
    BeginRow("Extra arguments", nullptr, nullptr, 0);
    ImGui::PushFont(g_fonts.mono);
    ImGui::InputTextWithHint("##v", "e.g. -reset 15 -vsync no", s.extraArgs, sizeof(s.extraArgs));
    ImGui::PopFont();
    EndRow();
    EndCard();

    SectionHeader("Command line", "Exactly what Start runs. The password is masked here and in logs.");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetCursorScreenPos();
    float w = ImMin(ImGui::GetContentRegionAvail().x, g_colW), pad = Px(tok::S4);
    ImGui::PushFont(g_fonts.mono);
    float textW = w - pad * 2 - Px(96);
    ImVec2 ts = ImGui::CalcTextSize(cmd.c_str(), nullptr, false, textW);
    ImGui::PopFont();
    float h = ImMax(ts.y, ImGui::GetFrameHeight()) + pad * 2;
    ImVec2 b(a.x + w, a.y + h);
    DrawSurface(dl, a, b, P->Code, Px(tok::RLarge), false);
    ImGui::SetCursorScreenPos(ImVec2(a.x + pad, a.y + pad));
    ImGui::PushFont(g_fonts.mono);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
    ImGui::PushStyleColor(ImGuiCol_Text, P->CodeText);
    ImGui::TextUnformatted(cmd.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(b.x - pad - Px(84), a.y + pad));
    if (ActionButton("Copy", Icon::Copy, true, ImVec2(Px(84), ImGui::GetFrameHeight()), BtnKind::Secondary,
                     "Copy the command line (password masked)")) {
        ImGui::SetClipboardText(cmd.c_str());
        ShowToast(ui, "Command copied to clipboard");
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
    ImGui::Dummy(ImVec2(w, Px(tok::S4)));
}

// ── Status presentation ───────────────────────────────────────────────────────

struct StatusLook { ImVec4 color; const char* label; bool spinner; };

static StatusLook LookFor(const Session& ses)
{
    switch (ses.phase) {
    case Phase::Off:        return { P->TextFaint, "Receiver off", false };
    case Phase::Starting:   return { P->Accent,    "Starting",     true  };
    case Phase::Waiting:    return { P->TextMuted, "Disconnected", false };
    case Phase::Connecting: return { P->Accent,    "Connecting",   true  };
    case Phase::Connected:  return { P->Success,   "Connected",    false };
    case Phase::Error:      return { (ses.error.fatal || ses.error.retry) ? P->Danger : P->Warning, "Error", false };
    }
    return { P->TextFaint, "", false };
}

// Draws the state glyph. Each state has its own shape, so color is never the only cue.
static void DrawStateGlyph(ImDrawList* dl, const Session& ses, ImVec2 c, float r)
{
    StatusLook look = LookFor(ses);
    if (look.spinner) { DrawSpinner(dl, c, r * 0.62f, Col(P->Border), Col(look.color)); return; }
    switch (ses.phase) {
    case Phase::Connected:
        dl->AddCircleFilled(c, r * 0.78f, Col(look.color), 24);
        DrawIcon(dl, Icon::Check, c, r * 0.95f, Col(P->OnAccent));
        break;
    case Phase::Error:   DrawIcon(dl, Icon::Alert, c, r * 1.4f, Col(look.color)); break;
    case Phase::Waiting: DrawIcon(dl, Icon::Connection, c, r * 1.3f, Col(look.color)); break;
    default:             dl->AddCircle(c, r * 0.6f, Col(look.color), 24, Px(1.6f)); break;
    }
}

static std::string StatusDetail(const Session& ses, const Settings& s, double now)
{
    std::string name = s.serverName[0] ? s.serverName : "this PC's hostname";
    std::string dev = ses.deviceName.empty() ? std::string("A device") : ses.deviceName;
    char port[32] = "";
    if (ses.port) snprintf(port, sizeof(port), "  \xc2\xb7  port %u", ses.port);
    switch (ses.phase) {
    case Phase::Off:        return "Start the receiver to make this PC visible to Apple devices.";
    case Phase::Starting:   return "Launching uxplay.exe (PID " + std::to_string(ses.pid) + ")...";
    case Phase::Waiting:    return "Waiting for devices  \xc2\xb7  visible as " + name + port;
    case Phase::Connecting: {
        std::string d = dev;
        if (!ses.deviceModel.empty()) d += " (" + ses.deviceModel + ")";
        if (!ses.deviceAddress.empty()) d += "  \xc2\xb7  " + ses.deviceAddress;
        return d + "  \xc2\xb7  setting up stream";
    }
    case Phase::Connected: {
        std::string d = dev;
        if (!ses.deviceAddress.empty()) d += "  \xc2\xb7  " + ses.deviceAddress;
        if (!ses.streamKinds.empty()) d += "  \xc2\xb7  " + ses.streamKinds;
        return d + "  \xc2\xb7  " + FormatDuration(now - ses.streamingSince);
    }
    case Phase::Error:      return ses.error.title;
    }
    return "";
}

// ── Sidebar ───────────────────────────────────────────────────────────────────

static void SelectPage(UiState& ui, UiPrefs& prefs, int i)
{
    if (ui.page == i) return;
    ui.page = i; prefs.page = i; ui.prefsDirty = true;
}

static void Sidebar(UiState& ui, UiPrefs& prefs, const Session& ses, bool compact, float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    dl->AddRectFilledMultiColor(o, ImVec2(o.x + width, o.y + height),
        Col(P->SidebarTop), Col(P->SidebarTop), Col(P->Sidebar), Col(P->Sidebar));
    dl->AddLine(ImVec2(o.x + width - 0.5f, o.y), ImVec2(o.x + width - 0.5f, o.y + height), Col(P->Border));

    const float pad = compact ? Px(12) : Px(tok::S4);

    // Brand
    float logo = Px(30);
    ImVec2 la(o.x + (compact ? (width - logo) * 0.5f : pad), o.y + Px(18));
    ImVec2 lb(la.x + logo, la.y + logo);
    dl->AddRectFilled(ImVec2(la.x, la.y + Px(2)), ImVec2(lb.x, lb.y + Px(2)), Col(P->Shadow), Px(8));
    dl->AddRectFilled(la, lb, Col(Hex(0x5A56E0)), Px(8));
    dl->AddRectFilled(la, ImVec2(lb.x, la.y + logo * 0.55f), Col(Hex(0x9C99FF, 0.45f)), Px(8),
                      ImDrawFlags_RoundCornersTop);
    dl->AddRect(la, lb, Col(Hex(0xFFFFFF, 0.18f)), Px(8));
    DrawIcon(dl, Icon::Logo, ImVec2(la.x + logo * 0.5f, la.y + logo * 0.5f), Px(17), Col(Hex(0xFFFFFF)));
    if (!compact) {
        ImGui::SetCursorScreenPos(ImVec2(lb.x + Px(10), la.y - Px(1)));
        TextColored(g_fonts.strong, P->Text, "UxPlay");
        ImGui::SetCursorScreenPos(ImVec2(lb.x + Px(10), la.y + Px(16)));
        TextColored(g_fonts.small, P->TextFaint, "AirPlay receiver");
    }

    // Navigation
    float y = lb.y + Px(26);
    if (!compact) {
        ImGui::SetCursorScreenPos(ImVec2(o.x + pad + Px(4), y));
        TextColored(g_fonts.small, P->TextFaint, "SETTINGS");
        y += Px(24);
    }
    const float itemH = Px(36);
    for (int i = 0; i < kPageCount; ++i) {
        ImVec2 a(o.x + Px(8), y), b(o.x + width - Px(8), y + itemH);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton(kPages[i].name, ImVec2(b.x - a.x, itemH))) SelectPage(ui, prefs, i);
        bool hov = ImGui::IsItemHovered(), sel = ui.page == i;
        float th = Animate(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 22.0f);
        float ts = Animate(ImGui::GetItemID() + 1, sel ? 1.0f : 0.0f, 18.0f);
        ImGui::SetItemTooltip("%s (Ctrl+%d)", kPages[i].name, i + 1);
        FocusRing(a, b, Px(tok::RMed));
        ImGui::PopID();

        float r = Px(tok::RMed);
        dl->AddRectFilled(a, b, Col(P->Overlay, th * (1 - ts)), r);
        if (ts > 0.0f) {
            dl->AddRectFilled(a, b, Col(P->AccentSoft, ts), r);
            dl->AddRect(a, b, Col(WithAlpha(P->Accent, 0.28f * ts)), r);
            float bh = itemH * 0.5f * ts;
            dl->AddRectFilled(ImVec2(a.x - Px(8) + 1, a.y + (itemH - bh) * 0.5f),
                              ImVec2(a.x - Px(8) + Px(3) + 1, a.y + (itemH + bh) * 0.5f),
                              Col(P->Accent, ts), Px(2));
        }
        ImVec4 fg = Mix(Mix(P->TextMuted, P->Text, th), P->Text, ts);
        float ix = compact ? (a.x + b.x) * 0.5f : a.x + Px(18);
        DrawIcon(dl, kPages[i].icon, ImVec2(ix, a.y + itemH * 0.5f), Px(16), Col(Mix(fg, P->AccentGlyph, ts)));
        if (!compact) {
            ImGui::PushFont(sel ? g_fonts.strong : g_fonts.body);
            float lh = ImGui::GetTextLineHeight();
            dl->AddText(ImVec2(a.x + Px(36), a.y + (itemH - lh) * 0.5f), Col(fg), kPages[i].name);
            ImGui::PopFont();
        }
        y += itemH + Px(2);
    }

    // Compact status at the bottom (full detail lives in the status strip).
    StatusLook look = LookFor(ses);
    if (compact) {
        ImVec2 c(o.x + width * 0.5f, o.y + height - Px(28));
        DrawStateGlyph(dl, ses, c, Px(9));
        ImGui::SetCursorScreenPos(ImVec2(c.x - Px(14), c.y - Px(14)));
        ImGui::InvisibleButton("##status", ImVec2(Px(28), Px(28)));
        ImGui::SetItemTooltip("Status: %s", look.label);
        return;
    }
    ImVec2 ca(o.x + Px(12), o.y + height - Px(56)), cb(o.x + width - Px(12), o.y + height - Px(12));
    DrawSurface(dl, ca, cb, P->Surface, Px(tok::RLarge));
    DrawStateGlyph(dl, ses, ImVec2(ca.x + Px(20), (ca.y + cb.y) * 0.5f), Px(9));
    ImGui::SetCursorScreenPos(ImVec2(ca.x + Px(36), ca.y + Px(5)));
    TextColored(g_fonts.small, P->TextFaint, "Status");
    ImGui::SetCursorScreenPos(ImVec2(ca.x + Px(36), ca.y + Px(21)));
    TextColored(g_fonts.strong, ses.phase == Phase::Off ? P->TextMuted : P->Text, look.label);
}

// ── Status strip (top of content) ─────────────────────────────────────────────

static void StatusStrip(const Settings& s, UiState& ui, Session& ses, const UiHost& host, ImVec2 a, ImVec2 b)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    double now = ImGui::GetTime();
    StatusLook look = LookFor(ses);
    // Brief fade on every state change (instant with reduced motion).
    static Phase shown = Phase::Off;
    ImGuiID fadeId = ImGui::GetID("##phasefade");
    if (shown != ses.phase) { shown = ses.phase; ImGui::GetStateStorage()->SetFloat(fadeId, 0.0f); }
    float t = Animate(fadeId, 1.0f, 10.0f);

    dl->AddRectFilled(a, b, Col(P->Footer));
    dl->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f), Col(P->Border));
    float wash = (P->dark ? 0.10f : 0.07f) * t;
    dl->AddRectFilledMultiColor(a, ImVec2(a.x + Px(300), b.y),
        Col(WithAlpha(look.color, wash)), Col(WithAlpha(look.color, 0.0f)),
        Col(WithAlpha(look.color, 0.0f)), Col(WithAlpha(look.color, wash)));

    const float pad = Px(tok::S5);
    float cy = (a.y + b.y) * 0.5f;
    ImVec2 gc(a.x + pad + Px(10), cy);
    dl->AddCircleFilled(gc, Px(17), Col(WithAlpha(look.color, P->dark ? 0.14f : 0.10f)), 32);
    DrawStateGlyph(dl, ses, gc, Px(10));

    // Actions (right aligned)
    float bh = Px(32), x = b.x - pad;
    auto place = [&](float w) { x -= w; ImGui::SetCursorScreenPos(ImVec2(x, cy - bh * 0.5f)); x -= Px(tok::S2); };
    (void)host;   // retry lives on the error card, directly under this strip
    place(bh);
    if (IconButton("##console", Icon::Console, ui.consoleVisible ? "Hide console (Ctrl+`)" : "Show console (Ctrl+`)",
                   32.0f, ui.consoleVisible))
        ui.consoleVisible = !ui.consoleVisible;

    // Text
    float tx = gc.x + Px(28);
    float maxW = x - tx - Px(tok::S2);
    float lh = g_fonts.strong->FontSize, sh = g_fonts.small->FontSize;
    float ty = cy - (lh + sh + Px(3)) * 0.5f;
    dl->AddText(g_fonts.strong, lh, ImVec2(tx, ty),
                Col(ses.phase == Phase::Off ? P->TextMuted : P->Text, 0.4f + 0.6f * t), look.label);
    std::string full = StatusDetail(ses, s, now);
    std::string detail = Ellipsize(g_fonts.small, full, ImMax(Px(40), maxW));
    dl->AddText(g_fonts.small, sh, ImVec2(tx, ty + lh + Px(3)), Col(P->TextMuted, 0.4f + 0.6f * t), detail.c_str());
    ImGui::SetCursorScreenPos(ImVec2(tx, ty));
    ImGui::InvisibleButton("Status", ImVec2(ImMax(1.0f, maxW), lh + sh));
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(full.c_str());
        if (ses.pid) ImGui::TextDisabled("Receiver process ID %u", ses.pid);
        ImGui::EndTooltip();
    }
}

// ── Error card ────────────────────────────────────────────────────────────────

static void ErrorCard(UiState& ui, Session& ses, const Settings& s, const UiHost& host, float w)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ErrorInfo e = ses.error;   // copy: buttons below may change the session
    float pad = Px(tok::S4);
    float textX = pad + Px(30);
    float textW = w - textX - pad - Px(28);
    ImVec2 a = ImGui::GetCursorScreenPos();

    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(a.x + textX, a.y + pad));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
    TextColored(g_fonts.strong, P->Text, e.title.c_str());
    TextColored(g_fonts.body, P->TextMuted, e.detail.c_str());
    TextColored(g_fonts.body, P->Text, e.hint.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, Px(2)));

    float bh = Px(30);
    if (e.retry) {
        if (ActionButton("Try again", Icon::Restart, true, ImVec2(Px(112), bh), BtnKind::Primary,
                         e.fatal ? "Start the receiver again (F5)" : "Restart the receiver (Ctrl+Shift+F5)")) {
            if (host.isRunning()) host.stop();
            host.start(s);
        }
        ImGui::SameLine(0, Px(tok::S2));
    }
    if (ActionButton(ui.consoleVisible ? "Hide console" : "Open console", Icon::Console, true,
                     ImVec2(Px(136), bh), BtnKind::Secondary, "Show the receiver's output (Ctrl+`)"))
        ui.consoleVisible = !ui.consoleVisible;
    ImGui::SameLine(0, Px(tok::S2));
    if (ActionButton("Open log", Icon::File, true, ImVec2(Px(108), bh), BtnKind::Secondary, "Open the diagnostic log file (Ctrl+L)"))
        host.openLogFile();
    if (!e.technical.empty() && w > Px(560)) ImGui::SameLine(0, Px(tok::S2));
    if (!e.technical.empty()) {
        if (ActionButton(ui.showTechnical ? "Hide details" : "Details", Icon::Advanced, false,
                         ImVec2(Px(104), bh), BtnKind::Ghost, "Show the technical message"))
            ui.showTechnical = !ui.showTechnical;
    }
    if (ui.showTechnical && !e.technical.empty()) {
        ImGui::Dummy(ImVec2(0, Px(2)));
        ImGui::PushFont(g_fonts.mono);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
        ImGui::PushStyleColor(ImGuiCol_Text, P->TextMuted);
        ImGui::TextUnformatted(e.technical.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    ImGui::EndGroup();
    ImVec2 b(a.x + w, ImGui::GetItemRectMax().y + pad);

    ImGui::SetCursorScreenPos(ImVec2(b.x - pad - Px(24) + Px(6), a.y + pad - Px(4)));
    bool dismiss = IconButton("##dismiss", Icon::Close, "Dismiss", 24.0f);

    dl->ChannelsSetCurrent(0);
    ImVec4 tint = (e.fatal || e.retry) ? P->Danger : P->Warning;
    dl->AddRectFilled(a, b, Col(P->Surface), Px(tok::RLarge));
    dl->AddRectFilled(a, b, Col(WithAlpha(tint, P->dark ? 0.08f : 0.06f)), Px(tok::RLarge));
    dl->AddRect(a, b, Col(WithAlpha(tint, 0.40f)), Px(tok::RLarge));
    dl->AddRectFilled(a, ImVec2(a.x + Px(3), b.y), Col(tint), Px(tok::RLarge), ImDrawFlags_RoundCornersLeft);
    DrawIcon(dl, Icon::Alert, ImVec2(a.x + pad + Px(9), a.y + pad + Px(9)), Px(17), Col(tint));
    dl->ChannelsMerge();

    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
    ImGui::Dummy(ImVec2(w, Px(tok::S3)));
    if (dismiss) ses.DismissError();
}

// ── Console drawer ────────────────────────────────────────────────────────────

static bool ContainsNoCase(const std::string& hay, const char* needle)
{
    if (!*needle) return true;
    auto it = std::search(hay.begin(), hay.end(), needle, needle + strlen(needle),
                          [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != hay.end();
}

static void ConsolePanel(UiState& ui, UiPrefs& prefs, Session& ses, const UiHost& host, ImVec2 a, ImVec2 b,
                         float maxH)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, Col(P->Code));

    // Resize handle along the top edge
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y - Px(3)));
    ImGui::InvisibleButton("Resize console", ImVec2(b.x - a.x, Px(7)));
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) {
        float hpx = prefs.consoleHeight - ImGui::GetIO().MouseDelta.y / g_scale;
        prefs.consoleHeight = ImClamp(hpx, 120.0f, ImMax(120.0f, maxH / g_scale));
    }
    if (ImGui::IsItemDeactivated()) ui.prefsDirty = true;
    dl->AddLine(ImVec2(a.x, a.y + 0.5f), ImVec2(b.x, a.y + 0.5f), Col(hot ? P->Accent : P->BorderStrong), hot ? Px(2) : 1.0f);

    // Header
    const float hh = Px(40), pad = Px(tok::S4);
    float cy = a.y + hh * 0.5f;
    DrawIcon(dl, Icon::Console, ImVec2(a.x + pad + Px(7), cy), Px(14), Col(P->TextMuted));
    float lx = a.x + pad + Px(22);
    dl->AddText(g_fonts.strong, g_fonts.strong->FontSize, ImVec2(lx, cy - g_fonts.strong->FontSize * 0.5f), Col(P->Text), "Console");
    char count[48];
    snprintf(count, sizeof(count), "%zu lines", ses.lines.size());
    float cx = lx + g_fonts.strong->CalcTextSizeA(g_fonts.strong->FontSize, FLT_MAX, 0, "Console").x + Px(10);
    dl->AddText(g_fonts.small, g_fonts.small->FontSize, ImVec2(cx, cy - g_fonts.small->FontSize * 0.5f), Col(P->TextFaint), count);

    const float bs = 28.0f;
    float x = b.x - pad;
    auto place = [&]() { x -= Px(bs); ImGui::SetCursorScreenPos(ImVec2(x, cy - Px(bs) * 0.5f)); x -= Px(4); };
    place(); if (IconButton("##hide", Icon::Close, "Hide console (Ctrl+`). The receiver keeps running.", bs)) ui.consoleVisible = false;
    place(); if (IconButton("##log", Icon::File, "Open log file (Ctrl+L)", bs)) host.openLogFile();
    place(); if (IconButton("##clear", Icon::Trash, "Clear console (the log file is kept)", bs)) ses.ClearConsole();
    place();
    if (IconButton("##copy", Icon::Copy, "Copy visible lines", bs)) {
        std::string all;
        for (const auto& l : ses.lines) if (ContainsNoCase(l.text, ui.consoleFilter)) { all += l.text; all += "\n"; }
        ImGui::SetClipboardText(all.c_str());
        ShowToast(ui, "Console copied to clipboard");
    }
    place();
    if (IconButton("##follow", Icon::Follow, ui.consoleFollow ? "Auto-scroll is on (click to pause)"
                                                            : "Auto-scroll is paused (click to resume)", bs, ui.consoleFollow))
        ui.consoleFollow = !ui.consoleFollow;
    float fw = ImMin(Px(200), x - (cx + Px(70)) - Px(4));
    if (fw >= Px(80)) {
        x -= fw;
        ImGui::SetCursorScreenPos(ImVec2(x, cy - ImGui::GetFrameHeight() * 0.5f));
        ImGui::SetNextItemWidth(fw);
        ImGui::InputTextWithHint("##filter", "Filter", ui.consoleFilter, sizeof(ui.consoleFilter));
        ImGui::SetItemTooltip("Show only lines containing this text");
    }

    // Body (virtualized: only visible lines are submitted)
    ImVec2 ba(a.x, a.y + hh);
    ImGui::SetCursorScreenPos(ba);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, Px(4)));
    ImGui::BeginChild("Console output", ImVec2(b.x - ba.x, b.y - ba.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar();
    ImGui::PushFont(g_fonts.mono);
    static std::vector<int> idx;
    idx.clear();
    bool filtering = ui.consoleFilter[0] != 0;
    if (filtering)
        for (int i = 0; i < (int)ses.lines.size(); ++i)
            if (ContainsNoCase(ses.lines[i].text, ui.consoleFilter)) idx.push_back(i);
    int n = filtering ? (int)idx.size() : (int)ses.lines.size();
    if (n == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, P->TextFaint);
        ImGui::TextUnformatted(filtering ? "No lines match the filter."
                                         : "No output yet. Messages from uxplay.exe appear here once the receiver starts.");
        ImGui::PopStyleColor();
    }
    ImGuiListClipper clip;
    clip.Begin(n);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const ConsoleLine& l = ses.lines[filtering ? idx[i] : i];
            ImVec4 c = l.level == LineLevel::Error ? P->Danger : l.level == LineLevel::Warning ? P->Warning
                     : l.level == LineLevel::App ? P->AccentGlyph : P->TextMuted;
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    // Scrolling up by hand pauses following; scrolling back to the bottom resumes it.
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && io.MouseWheel > 0.0f) ui.consoleFollow = false;
    if (!ui.consoleFollow && io.MouseWheel < 0.0f && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ui.consoleFollow = true;
    static uint64_t lastSeen = 0;
    if (ui.consoleFollow && (ses.linesAdded != lastSeen || ImGui::IsWindowAppearing())) ImGui::SetScrollHereY(1.0f);
    lastSeen = ses.linesAdded;
    ImGui::PopFont();
    ImGui::EndChild();
}

// ── Footer action bar ─────────────────────────────────────────────────────────

static void SaveNow(const Settings& s, UiState& ui, const UiHost& host)
{
    if (host.save(s)) { ui.savedSnapshot = SerializeSettings(s); ShowToast(ui, "Settings saved"); }
    else ShowToast(ui, "Could not save settings. See the log for details.", true);
}

static void Footer(Settings& s, UiState& ui, const UiHost& host, bool running, bool dirty,
                   const std::string& cmd, ImVec2 a, ImVec2 b)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, Col(P->Footer));
    dl->AddLine(ImVec2(a.x, a.y + 0.5f), ImVec2(b.x, a.y + 0.5f), Col(P->Border));

    const float pad = Px(tok::S5), bh = Px(38);
    float y = a.y + ((b.y - a.y) - bh) * 0.5f;
    float launchW = Px(124), saveW = Px(104);
    float right = b.x - pad;

    ImGui::SetCursorScreenPos(ImVec2(right - launchW, y));
    if (!running) {
        if (ActionButton("Start", Icon::Play, true, ImVec2(launchW, bh), BtnKind::Primary,
                         "Start the AirPlay receiver with these settings (F5)"))
            host.start(s);
    } else {
        if (ActionButton("Stop", Icon::Stop, true, ImVec2(launchW, bh), BtnKind::Danger,
                         "Stop the receiver and disconnect devices (Shift+F5)"))
            host.stop();
    }
    ImGui::SetCursorScreenPos(ImVec2(right - launchW - Px(tok::S2) - saveW, y));
    if (ActionButton(dirty ? "Save" : "Saved", Icon::Save, !dirty, ImVec2(saveW, bh), BtnKind::Secondary,
                     dirty ? "Save settings (Ctrl+S)" : "All changes saved"))
        SaveNow(s, ui, host);
    if (dirty) {
        ImVec2 r = ImGui::GetItemRectMin();
        dl->AddCircleFilled(ImVec2(r.x + Px(16), r.y + bh * 0.5f), Px(3.5f), Col(P->Warning), 12);
    }

    // Command preview chip (click to copy)
    float chipR = right - launchW - saveW - Px(tok::S2) - Px(tok::S4);
    float chipW = chipR - (a.x + pad);
    if (chipW > Px(120)) {
        ImVec2 ca(a.x + pad, y), cb(chipR, y + bh);
        ImGui::SetCursorScreenPos(ca);
        bool clicked = ImGui::InvisibleButton("Command line", ImVec2(cb.x - ca.x, bh));
        bool hov = ImGui::IsItemHovered();
        float th = Animate(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 20.0f);
        dl->AddRectFilled(ca, cb, Col(Mix(P->Code, P->RaisedHover, th * 0.6f)), Px(tok::RMed));
        dl->AddRect(ca, cb, Col(Mix(P->Border, P->BorderStrong, th)), Px(tok::RMed));
        float iconSz = Px(13);
        DrawIcon(dl, Icon::Copy, ImVec2(cb.x - Px(12) - iconSz * 0.5f, ca.y + bh * 0.5f), iconSz,
                 Col(Mix(P->TextFaint, P->Text, th)));
        ImGui::PushFont(g_fonts.mono);
        float lh = ImGui::GetTextLineHeight();
        float px = ca.x + Px(12);
        dl->AddText(ImVec2(px, ca.y + (bh - lh) * 0.5f), Col(P->Accent), "$");
        px += ImGui::CalcTextSize("$ ").x;
        ImGui::PopFont();
        std::string shown = Ellipsize(g_fonts.mono, cmd, cb.x - px - Px(32));
        ImGui::PushFont(g_fonts.mono);
        dl->AddText(ImVec2(px, ca.y + (bh - lh) * 0.5f), Col(P->TextMuted), shown.c_str());
        ImGui::PopFont();
        FocusRing(ca, cb, Px(tok::RMed));
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushFont(g_fonts.mono);
            ImGui::PushTextWrapPos(Px(460));
            ImGui::TextUnformatted(cmd.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            TextColored(g_fonts.small, P->TextFaint, "Click to copy (password masked)");
            ImGui::EndTooltip();
        }
        if (clicked) { ImGui::SetClipboardText(cmd.c_str()); ShowToast(ui, "Command copied to clipboard"); }
    }
}

// ── Menu bar, shortcuts and About ─────────────────────────────────────────────

static void SetTheme(UiPrefs& prefs, UiState& ui, ThemeMode m) { prefs.theme = m; ui.prefsDirty = true; }

static void Restart(const Settings& s, const UiHost& host) { if (host.isRunning()) host.stop(); host.start(s); }

static void Shortcuts(Settings& s, UiState& ui, UiPrefs& prefs, const UiHost& host, bool running)
{
    const ImGuiInputFlags g = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, g))                   SaveNow(s, ui, host);
    if (ImGui::Shortcut(ImGuiKey_F5, g) && !running)                      host.start(s);
    if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_F5, g) && running)      host.stop();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F5, g) && running) Restart(s, host);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_GraveAccent, g))         ui.consoleVisible = !ui.consoleVisible;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_L, g))                   host.openLogFile();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, g) && running)        host.setRotation((host.getRotation() + 1) % 4);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R, g) && running) host.setRotation(0);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, g))                   host.exitApp();
    if (ImGui::Shortcut(ImGuiKey_F1, g))                                  host.openDocs();
    for (int i = 0; i < kPageCount; ++i)
        if (ImGui::Shortcut(ImGuiMod_Ctrl | (ImGuiKey)(ImGuiKey_1 + i), g)) SelectPage(ui, prefs, i);
}

static void MenuBar(Settings& s, UiState& ui, Session& ses, UiPrefs& prefs, const UiHost& host, bool running)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(12), Px(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(6), Px(6)));
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Settings", "Ctrl+S")) SaveNow(s, ui, host);
        if (ImGui::MenuItem("Open Settings Folder")) host.openSettingsFolder();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Ctrl+Q")) host.exitApp();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem(ui.consoleVisible ? "Hide Console" : "Show Console", "Ctrl+`"))
            ui.consoleVisible = !ui.consoleVisible;
        bool soc = prefs.showConsoleOnStartup;
        if (ImGui::MenuItem("Show Console on Startup", nullptr, &soc)) { prefs.showConsoleOnStartup = soc; ui.prefsDirty = true; }
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            if (ImGui::MenuItem("Use System Setting", nullptr, prefs.theme == ThemeMode::System)) SetTheme(prefs, ui, ThemeMode::System);
            if (ImGui::MenuItem("Light", nullptr, prefs.theme == ThemeMode::Light)) SetTheme(prefs, ui, ThemeMode::Light);
            if (ImGui::MenuItem("Dark", nullptr, prefs.theme == ThemeMode::Dark)) SetTheme(prefs, ui, ThemeMode::Dark);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        for (int i = 0; i < kPageCount; ++i) {
            char sc[16]; snprintf(sc, sizeof(sc), "Ctrl+%d", i + 1);
            if (ImGui::MenuItem(kPages[i].name, sc, ui.page == i)) SelectPage(ui, prefs, i);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Receiver")) {
        if (ImGui::MenuItem("Start", "F5", false, !running)) host.start(s);
        if (ImGui::MenuItem("Stop", "Shift+F5", false, running)) host.stop();
        if (ImGui::MenuItem("Restart", "Ctrl+Shift+F5", false, running)) Restart(s, host);
        ImGui::Separator();
        if (ImGui::MenuItem("Rotate Player 90\xc2\xb0", "Ctrl+R", false, running)) host.setRotation((host.getRotation() + 1) % 4);
        if (ImGui::MenuItem("Reset Player Rotation", "Ctrl+Shift+R", false, running && host.getRotation() != 0)) host.setRotation(0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        const char* logIssue = host.logStatus();
        if (ImGui::MenuItem("Open Log File", "Ctrl+L", false, logIssue == nullptr)) host.openLogFile();
        if (ImGui::MenuItem("Open Logs Folder")) host.openLogFolder();
        if (logIssue) ImGui::TextDisabled("Logging unavailable: %s", logIssue);
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Command Line")) {
            std::string cmd = "uxplay.exe " + BuildArgs(s, true);
            ImGui::SetClipboardText(cmd.c_str());
            ShowToast(ui, "Command copied to clipboard");
        }
        if (ImGui::MenuItem("Clear Console")) ses.ClearConsole();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Documentation", "F1")) host.openDocs();
        ImGui::Separator();
        if (ImGui::MenuItem("About UxPlay")) ui.openAbout = true;
        ImGui::EndMenu();
    }
    ImGui::PopStyleVar(2);
}

static void AboutDialog(UiState& ui, const Session& ses)
{
    if (ui.openAbout) { ImGui::OpenPopup("About UxPlay"); ui.openAbout = false; }
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(Px(400), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(24), Px(22)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, P->Surface);
    if (ImGui::BeginPopupModal("About UxPlay", nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float logo = Px(40);
        dl->AddRectFilled(p, ImVec2(p.x + logo, p.y + logo), Col(Hex(0x5A56E0)), Px(10));
        dl->AddRectFilled(p, ImVec2(p.x + logo, p.y + logo * 0.55f), Col(Hex(0x9C99FF, 0.45f)), Px(10), ImDrawFlags_RoundCornersTop);
        DrawIcon(dl, Icon::Logo, ImVec2(p.x + logo * 0.5f, p.y + logo * 0.5f), Px(22), Col(Hex(0xFFFFFF)));
        ImGui::SetCursorScreenPos(ImVec2(p.x + logo + Px(14), p.y - Px(1)));
        TextColored(g_fonts.title, P->Text, "UxPlay");
        ImGui::SetCursorScreenPos(ImVec2(p.x + logo + Px(14), p.y + Px(26)));
        TextColored(g_fonts.small, P->TextMuted, "AirPlay mirroring and audio receiver");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + logo + Px(18)));
        float col2 = Px(150);
        ImGui::TextUnformatted("Launcher version"); ImGui::SameLine(col2); ImGui::TextDisabled("%s", ui.launcherVersion.c_str());
        ImGui::TextUnformatted("Receiver version"); ImGui::SameLine(col2);
        ImGui::TextDisabled("%s", ses.uxplayVersion.empty() ? "Shown after the receiver starts" : ses.uxplayVersion.c_str());
        ImGui::Dummy(ImVec2(0, Px(4)));
        ImGui::PushTextWrapPos(0);
        TextColored(g_fonts.small, P->TextMuted, "Open-source software licensed under the GNU GPL v3. Built on GStreamer.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, Px(10)));
        float bw = Px(96);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - Px(24) - bw);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ActionButton("Close", Icon::Close, false, ImVec2(bw, Px(34)), BtnKind::Secondary) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ── Root ──────────────────────────────────────────────────────────────────────

void DrawLauncherUI(Settings& s, UiState& ui, Session& ses, UiPrefs& prefs, const UiHost& host, const UiFonts& fonts)
{
    g_fonts = fonts;
    g_reduceMotion = ui.reduceMotion;
    ImGuiIO& io = ImGui::GetIO();
    double now = ImGui::GetTime();

    // Theme follows the preference (or the OS when set to System).
    bool dark = prefs.theme == ThemeMode::Dark || (prefs.theme == ThemeMode::System && ui.systemDark);
    if (ui.appliedTheme != (dark ? 1 : 0)) {
        ApplyUiTheme(ui.scale, dark);
        ui.appliedTheme = dark ? 1 : 0;
        if (host.themeApplied) host.themeApplied(dark);
    }

    bool running = host.isRunning();
    bool dirty = SerializeSettings(s) != ui.savedSnapshot;
    std::string cmd = "uxplay.exe";
    std::string args = BuildArgs(s, true);
    if (!args.empty()) { cmd += " "; cmd += args; }

    // Each new error also raises one toast (never repeated for the same error).
    if (ses.errorSerial != ui.seenErrorSerial) {
        ui.seenErrorSerial = ses.errorSerial;
        ui.showTechnical = false;
        if (ses.hasError) ShowToast(ui, ses.error.title.c_str(), ses.error.fatal);
    }

    Shortcuts(s, ui, prefs, host, running);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("UxPlay", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Px(10), Px(8)));
    if (ImGui::BeginMenuBar()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Px(6));
        MenuBar(s, ui, ses, prefs, host, running);
        ImGui::EndMenuBar();
    }
    ImGui::PopStyleVar();
    const float menuH = ImGui::GetCurrentWindow()->MenuBarHeight;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(0, menuH - 0.5f), ImVec2(io.DisplaySize.x, menuH - 0.5f), Col(P->Border));

    const float W = io.DisplaySize.x, H = io.DisplaySize.y - menuH;
    const bool compact = W < Px(720);
    const float sideW = Px(compact ? tok::SidebarCompactW : tok::SidebarW);
    const float footH = Px(tok::FooterH), statusH = Px(tok::StatusH);
    const float mainH = H - footH;
    const float maxC = mainH - statusH - Px(180);   // keep some settings visible
    float consoleH = 0.0f;
    bool consoleShown = ui.consoleVisible && maxC >= Px(100);   // too short a window: keep settings usable
    if (consoleShown) consoleH = ImClamp(Px(prefs.consoleHeight), ImMin(Px(120), maxC), maxC);
    const float y0 = menuH;

    // Sidebar
    ImGui::SetCursorScreenPos(ImVec2(0, y0));
    ImGui::BeginChild("Navigation", ImVec2(sideW, H), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Sidebar(ui, prefs, ses, compact, sideW, H);
    ImGui::EndChild();

    // Status strip
    StatusStrip(s, ui, ses, host, ImVec2(sideW, y0), ImVec2(W, y0 + statusH));

    // Content canvas with a faint accent wash at the top
    ImDrawList* bg = ImGui::GetWindowDrawList();
    bg->AddRectFilledMultiColor(ImVec2(sideW, y0 + statusH), ImVec2(W, y0 + statusH + Px(160)),
        Col(WithAlpha(P->Accent, P->dark ? 0.06f : 0.04f)), Col(WithAlpha(P->Accent, 0.015f)),
        Col(WithAlpha(P->Accent, 0.0f)), Col(WithAlpha(P->Accent, 0.0f)));

    const float padX = compact ? Px(tok::S5) : Px(tok::S6 + 4);
    ImGui::SetCursorScreenPos(ImVec2(sideW, y0 + statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, Px(22)));
    ImGui::BeginChild("Settings", ImVec2(W - sideW, mainH - statusH - consoleH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    {
        float avail = ImGui::GetContentRegionAvail().x;
        float maxW = Px(760);
        if (avail > maxW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - maxW) * 0.5f);
        float colW = ImMin(avail, maxW);
        g_colW = colW;
        ImGui::BeginGroup();
        ImGui::PushItemWidth(colW);
        ImGui::Dummy(ImVec2(colW, 0));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);

        if (ses.hasError) ErrorCard(ui, ses, s, host, colW);

        // Page title with a short fade/slide on change
        ImGuiID pageAnimId = ImGui::GetID("##pageanim");
        static int lastPage = -1;
        if (lastPage != ui.page) { ImGui::GetStateStorage()->SetFloat(pageAnimId, 0.0f); lastPage = ui.page; }
        float pa = Animate(pageAnimId, 1.0f, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f + 0.65f * pa);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Px(6) * (1 - pa));

        TextColored(g_fonts.title, P->Text, kPages[ui.page].name);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(2));
        TextColored(g_fonts.body, P->TextMuted, kPages[ui.page].subtitle);
        ImGui::Dummy(ImVec2(0, Px(2)));

        switch (ui.page) {
        case 0: PageConnection(s);                  break;
        case 1: PageDisplay(s, host);               break;
        case 2: PageAudio(s);                       break;
        case 3: PageAdvanced(s, ui, prefs, cmd);    break;
        }
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    if (consoleShown)
        ConsolePanel(ui, prefs, ses, host, ImVec2(sideW, y0 + mainH - consoleH), ImVec2(W, y0 + mainH), maxC);
    else   // soft shadow cast onto the scrolling content above the footer
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(ImVec2(sideW, y0 + mainH - Px(10)), ImVec2(W, y0 + mainH),
            Col(WithAlpha(P->Shadow, 0.0f)), Col(WithAlpha(P->Shadow, 0.0f)),
            Col(WithAlpha(P->Shadow, P->dark ? 0.28f : 0.06f)), Col(WithAlpha(P->Shadow, P->dark ? 0.28f : 0.06f)));

    Footer(s, ui, host, running, dirty, cmd, ImVec2(sideW, y0 + mainH), ImVec2(W, y0 + H));

    AboutDialog(ui, ses);

    // Toast (fades in/out above the footer or console, right aligned)
    if (!ui.toast.empty()) {
        float remain = (float)(ui.toastUntil - now);
        if (remain <= 0) { ui.toast.clear(); }
        else {
            float total = ui.toastError ? 4.0f : 2.2f;
            float alpha = g_reduceMotion ? 1.0f
                        : ImClamp(ImMin(remain / 0.3f, (total - remain) / 0.15f), 0.0f, 1.0f);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            float maxTW = ImMax(Px(120), ImMin(Px(420), W - sideW - Px(80)));
            ImVec2 ts = g_fonts.body->CalcTextSizeA(g_fonts.body->FontSize, FLT_MAX, maxTW, ui.toast.c_str());
            float pad = Px(14);
            float bottom = y0 + mainH - consoleH - Px(14) + (g_reduceMotion ? 0.0f : Px(6) * (1 - alpha));
            ImVec2 b(W - Px(tok::S5), bottom);
            ImVec2 a(b.x - ts.x - pad * 2 - Px(18), b.y - ts.y - pad * 1.4f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            fg->AddRectFilled(ImVec2(a.x, a.y + Px(4)), ImVec2(b.x, b.y + Px(6)), Col(Hex(0x000000, 0.25f)), Px(tok::RLarge));
            fg->AddRectFilled(a, b, Col(P->dark ? Hex(0x23252D) : Hex(0x1F2029)), Px(tok::RLarge));
            fg->AddRect(a, b, Col(Hex(0xFFFFFF, 0.08f)), Px(tok::RLarge));
            fg->AddCircleFilled(ImVec2(a.x + pad + Px(3), (a.y + b.y) * 0.5f), Px(4),
                                Col(ui.toastError ? kDark.Danger : kDark.Success), 12);
            fg->AddText(g_fonts.body, g_fonts.body->FontSize, ImVec2(a.x + pad + Px(15), (a.y + b.y - ts.y) * 0.5f),
                        Col(Hex(0xF2F3F6)), ui.toast.c_str(), nullptr, maxTW);
            ImGui::PopStyleVar();
        }
    }

    ImGui::End();
}

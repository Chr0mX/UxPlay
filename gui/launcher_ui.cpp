// UxPlay GUI Launcher — design system, widgets and screens (Dear ImGui only).

#include "launcher_ui.h"

#include "imgui_internal.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sstream>
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

std::string BuildArgs(const Settings& s)
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
    if (s.password[0])   { append("-pw"); append(std::string("\"") + s.password + "\""); }

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
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if      (key == "serverName")      strncpy(s.serverName, val.c_str(), sizeof(s.serverName) - 1);
        else if (key == "port")            s.port = toInt(val, s.port);
        else if (key == "fullscreen")      s.fullscreen = (val == "1");
        else if (key == "windowSize")      strncpy(s.windowSize, val.c_str(), sizeof(s.windowSize) - 1);
        else if (key == "pin")             s.pin = (val == "1");
        else if (key == "password")        strncpy(s.password, val.c_str(), sizeof(s.password) - 1);
        else if (key == "videoSinkIdx")    s.videoSinkIdx    = toInt(val, s.videoSinkIdx);
        else if (key == "videoDecoderIdx") s.videoDecoderIdx = toInt(val, s.videoDecoderIdx);
        else if (key == "h265")            s.h265 = (val == "1");
        else if (key == "audioSinkIdx")    s.audioSinkIdx    = toInt(val, s.audioSinkIdx);
        else if (key == "volume")          { try { s.volume = std::stof(val); } catch (...) {} }
        else if (key == "fps")             s.fps = toInt(val, s.fps);
        else if (key == "noFreeze")        s.noFreeze = (val == "1");
        else if (key == "debug")           s.debug = (val == "1");
        else if (key == "extraArgs")       strncpy(s.extraArgs, val.c_str(), sizeof(s.extraArgs) - 1);
    }
    auto clamp = [](int& v, int lo, int hi) { if (v < lo) v = lo; if (v > hi) v = hi; };
    clamp(s.videoSinkIdx,    0, kVideoSinkCount    - 1);
    clamp(s.videoDecoderIdx, 0, kVideoDecoderCount - 1);
    clamp(s.audioSinkIdx,    0, kAudioSinkCount    - 1);
    clamp(s.port,            1, 65535);
    if (s.volume < 0.0f) s.volume = 0.0f;
    if (s.volume > 1.0f) s.volume = 1.0f;
    if (s.fps < 0 || s.fps > 256) s.fps = 0;
}

// ── Design tokens ─────────────────────────────────────────────────────────────
// One palette, one spacing scale, one radius scale. Every widget below draws
// from these; nothing else in the file should contain a literal color.

namespace tok {
    static ImVec4 Hex(uint32_t rgb, float a = 1.0f) {
        return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                      (rgb & 0xFF) / 255.0f, a);
    }
    // Surfaces, darkest to lightest
    static const ImVec4 Base        = Hex(0x0C0D10);   // content canvas
    static const ImVec4 Sidebar     = Hex(0x111217);
    static const ImVec4 Surface     = Hex(0x16181D);   // cards
    static const ImVec4 Raised      = Hex(0x1E2027);   // inputs, secondary buttons
    static const ImVec4 RaisedHover = Hex(0x262932);
    static const ImVec4 RaisedPress = Hex(0x2D303A);
    static const ImVec4 Border      = Hex(0x23262E);
    static const ImVec4 BorderStrong= Hex(0x323642);
    static const ImVec4 Highlight   = Hex(0xFFFFFF, 0.05f);  // top inner edge
    static const ImVec4 Shadow      = Hex(0x000000, 0.35f);
    // Text
    static const ImVec4 Text        = Hex(0xE9EAEE);
    static const ImVec4 TextMuted   = Hex(0x9297A3);
    static const ImVec4 TextFaint   = Hex(0x5F646F);
    // Accent and semantic
    static const ImVec4 Accent      = Hex(0x6D6AF2);
    static const ImVec4 AccentHover = Hex(0x8280F5);
    static const ImVec4 AccentPress = Hex(0x5A57DE);
    static const ImVec4 AccentSoft  = Hex(0x6D6AF2, 0.16f);
    static const ImVec4 Success     = Hex(0x3DBE85);
    static const ImVec4 Warning     = Hex(0xE6A23C);
    static const ImVec4 Danger      = Hex(0xE5484D);
    static const ImVec4 DangerHover = Hex(0xEC5F63);
    static const ImVec4 DangerPress = Hex(0xCC3B40);
    static const ImVec4 TrackOff    = Hex(0x2B2E37);

    // Spacing (unscaled px; multiplied by g_scale)
    static const float S1 = 4, S2 = 8, S3 = 12, S4 = 16, S5 = 24, S6 = 32;
    // Radii
    static const float RSmall = 5, RMed = 7, RLarge = 10;
    // Fixed layout
    static const float SidebarW = 216, SidebarCompactW = 64, FooterH = 68;
    static const float ControlW = 248, ToggleW = 38, ToggleH = 22;
}

static float   g_scale = 1.0f;
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
// Exponential approach animation stored per-widget in ImGui's state storage.
static float Animate(ImGuiID id, float target, float speed = 16.0f) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float* v = st->GetFloatRef(id, -1.0f);
    if (*v < 0.0f) *v = target;
    *v += (target - *v) * ImMin(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (std::fabs(*v - target) < 0.001f) *v = target;
    return *v;
}
static bool NavCursorOn() { return ImGui::GetCurrentContext()->NavCursorVisible; }
static void FocusRing(const ImVec2& a, const ImVec2& b, float r) {
    if (ImGui::IsItemFocused() && NavCursorOn()) {
        float o = Px(2);
        ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - o, a.y - o), ImVec2(b.x + o, b.y + o),
                                            Col(tok::AccentHover, 0.9f), r + o, 0, Px(1.5f));
    }
}

// ── Fonts and theme ───────────────────────────────────────────────────────────

static bool FileExists(const char* p) {
    if (!p || !*p) return false;
    FILE* f = fopen(p, "rb");
    if (f) fclose(f);
    return f != nullptr;
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

ImVec4 UiClearColor() { return tok::Base; }

void ApplyUiTheme(float scale)
{
    g_scale = scale;
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();

    st.WindowPadding     = ImVec2(0, 0);
    st.WindowBorderSize  = 0;
    st.WindowRounding    = 0;
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

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = tok::Text;
    c[ImGuiCol_TextDisabled]         = tok::TextFaint;
    c[ImGuiCol_WindowBg]             = tok::Base;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = tok::Hex(0x1A1C22, 0.98f);
    c[ImGuiCol_Border]               = tok::Border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = tok::Raised;
    c[ImGuiCol_FrameBgHovered]       = tok::RaisedHover;
    c[ImGuiCol_FrameBgActive]        = tok::RaisedPress;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = tok::BorderStrong;
    c[ImGuiCol_ScrollbarGrabHovered] = tok::Hex(0x434756);
    c[ImGuiCol_ScrollbarGrabActive]  = tok::Hex(0x515667);
    c[ImGuiCol_CheckMark]            = tok::Accent;
    c[ImGuiCol_SliderGrab]           = tok::Accent;
    c[ImGuiCol_SliderGrabActive]     = tok::AccentHover;
    c[ImGuiCol_Button]               = tok::Raised;
    c[ImGuiCol_ButtonHovered]        = tok::RaisedHover;
    c[ImGuiCol_ButtonActive]         = tok::RaisedPress;
    c[ImGuiCol_Header]               = tok::AccentSoft;
    c[ImGuiCol_HeaderHovered]        = tok::Hex(0xFFFFFF, 0.06f);
    c[ImGuiCol_HeaderActive]         = tok::Hex(0x6D6AF2, 0.24f);
    c[ImGuiCol_Separator]            = tok::Border;
    c[ImGuiCol_TextSelectedBg]       = tok::Hex(0x6D6AF2, 0.35f);
    c[ImGuiCol_NavCursor]            = tok::AccentHover;
    c[ImGuiCol_ModalWindowDimBg]     = tok::Hex(0x000000, 0.5f);

    st.ScaleAllSizes(scale);
}

// ── Icons (vector, single stroke weight, drawn in a 16-unit box) ─────────────

enum class Icon { Logo, Connection, Display, Audio, Advanced, Copy, Play, Stop, Save, Rotate, Alert, Close };

static void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 col)
{
    const float u = size / 16.0f;      // one design unit
    const float w = ImMax(1.0f, 1.5f * u);
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 8) * u, c.y + (y - 8) * u); };
    switch (icon) {
    case Icon::Logo:   // AirPlay: screen + triangle
    case Icon::Connection:
        dl->PathLineTo(P(5, 12)); dl->PathLineTo(P(2.5f, 12)); dl->PathLineTo(P(2.5f, 2.5f));
        dl->PathLineTo(P(13.5f, 2.5f)); dl->PathLineTo(P(13.5f, 12)); dl->PathLineTo(P(11, 12));
        dl->PathStroke(col, 0, w);
        dl->AddTriangleFilled(P(8, 9), P(12, 14.5f), P(4, 14.5f), col);
        break;
    case Icon::Display:
        dl->AddRect(P(1.5f, 2.5f), P(14.5f, 11.5f), col, 1.5f * u, 0, w);
        dl->AddLine(P(8, 11.5f), P(8, 14), col, w);
        dl->AddLine(P(5, 14), P(11, 14), col, w);
        break;
    case Icon::Audio:
        dl->PathLineTo(P(2, 6)); dl->PathLineTo(P(5, 6)); dl->PathLineTo(P(9, 2.5f));
        dl->PathLineTo(P(9, 13.5f)); dl->PathLineTo(P(5, 10)); dl->PathLineTo(P(2, 10));
        dl->PathStroke(col, ImDrawFlags_Closed, w);
        dl->PathArcTo(P(9, 8), 3.5f * u, -0.9f, 0.9f, 10); dl->PathStroke(col, 0, w);
        dl->PathArcTo(P(9, 8), 6.0f * u, -0.9f, 0.9f, 12); dl->PathStroke(col, 0, w);
        break;
    case Icon::Advanced:
        for (int i = 0; i < 3; ++i) {
            float y = 3.5f + i * 4.5f, k = (i == 1) ? 10.5f : (i == 0 ? 5.5f : 8.0f);
            dl->AddLine(P(2, y), P(14, y), col, w);
            dl->AddCircleFilled(P(k, y), 2.0f * u, col);
        }
        break;
    case Icon::Copy:
        dl->AddRect(P(5, 5), P(14, 14), col, 1.5f * u, 0, w);
        dl->PathLineTo(P(11, 2.5f)); dl->PathLineTo(P(2.5f, 2.5f)); dl->PathLineTo(P(2.5f, 11));
        dl->PathStroke(col, 0, w);
        break;
    case Icon::Play:
        dl->AddTriangleFilled(P(4.5f, 2.5f), P(13.5f, 8), P(4.5f, 13.5f), col);
        break;
    case Icon::Stop:
        dl->AddRectFilled(P(3.5f, 3.5f), P(12.5f, 12.5f), col, 1.5f * u);
        break;
    case Icon::Save:
        dl->PathLineTo(P(2.5f, 9)); dl->PathLineTo(P(6.5f, 13)); dl->PathLineTo(P(13.5f, 3.5f));
        dl->PathStroke(col, 0, w * 1.15f);
        break;
    case Icon::Rotate:
        dl->PathArcTo(P(8, 8.5f), 5.5f * u, IM_PI * 1.15f, IM_PI * 2.75f, 20);
        dl->PathStroke(col, 0, w);
        dl->AddTriangleFilled(P(11.5f, 1.5f), P(14.5f, 5.5f), P(10, 6), col);
        break;
    case Icon::Alert:
        dl->AddCircle(c, 6.5f * u, col, 24, w);
        dl->AddLine(P(8, 4.5f), P(8, 9), col, w * 1.2f);
        dl->AddCircleFilled(P(8, 11.3f), 1.0f * u, col);
        break;
    case Icon::Close:
        dl->AddLine(P(4, 4), P(12, 12), col, w);
        dl->AddLine(P(12, 4), P(4, 12), col, w);
        break;
    }
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

// Raised surface with shadow, 1px border and a faint top highlight.
static void DrawSurface(ImDrawList* dl, ImVec2 a, ImVec2 b, const ImVec4& fill, float r,
                        bool shadow = true)
{
    if (shadow) {
        dl->AddRectFilled(ImVec2(a.x, a.y + Px(2)), ImVec2(b.x, b.y + Px(3)), Col(tok::Shadow, 0.6f), r);
        dl->AddRectFilled(ImVec2(a.x, a.y + Px(1)), ImVec2(b.x, b.y + Px(1)), Col(tok::Shadow), r);
    }
    dl->AddRectFilled(a, b, Col(fill), r);
    dl->AddRect(a, b, Col(tok::Border), r, 0, 1.0f);
    dl->AddLine(ImVec2(a.x + r, a.y + 0.5f), ImVec2(b.x - r, a.y + 0.5f), Col(tok::Highlight), 1.0f);
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

    ImVec4 base, hov, act, fg = tok::Text;
    switch (kind) {
    case BtnKind::Primary:   base = tok::Accent; hov = tok::AccentHover; act = tok::AccentPress; fg = tok::Hex(0xFFFFFF); break;
    case BtnKind::Danger:    base = tok::Danger; hov = tok::DangerHover; act = tok::DangerPress; fg = tok::Hex(0xFFFFFF); break;
    case BtnKind::Secondary: base = tok::Raised; hov = tok::RaisedHover; act = tok::RaisedPress; break;
    case BtnKind::Ghost:     base = ImVec4(0, 0, 0, 0); hov = tok::Hex(0xFFFFFF, 0.06f); act = tok::Hex(0xFFFFFF, 0.1f);
                             fg = Mix(tok::TextMuted, tok::Text, h); break;
    }
    ImVec4 fill = held ? act : Mix(base, hov, h);
    ImVec2 b(p.x + size.x, p.y + size.y);
    float r = Px(tok::RMed);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (kind != BtnKind::Ghost) {
        dl->AddRectFilled(ImVec2(p.x, p.y + Px(1)), ImVec2(b.x, b.y + Px(1)), Col(tok::Shadow), r);
        dl->AddRectFilled(p, b, Col(fill), r);
        if (kind == BtnKind::Secondary) dl->AddRect(p, b, Col(Mix(tok::BorderStrong, tok::Hex(0x404452), h)), r);
        dl->AddLine(ImVec2(p.x + r, p.y + 0.5f), ImVec2(b.x - r, p.y + 0.5f),
                    Col(tok::Hex(0xFFFFFF, kind == BtnKind::Secondary ? 0.05f : 0.18f)));
    } else {
        dl->AddRectFilled(p, b, Col(fill), r);
    }

    ImGui::PushFont(g_fonts.strong);
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    float iconSz = Px(14), gap = withIcon ? Px(8) : 0.0f;
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
    ImVec4 off = hovered ? tok::Hex(0x343845) : tok::TrackOff;
    ImVec4 on  = hovered ? tok::AccentHover : tok::Accent;
    dl->AddRectFilled(a, b, Col(Mix(off, on, t)), h * 0.5f);
    dl->AddRect(a, b, Col(tok::Hex(0x000000, 0.25f)), h * 0.5f);
    float rad = h * 0.5f - Px(3);
    ImVec2 k(a.x + h * 0.5f + (w - h) * t, a.y + h * 0.5f);
    dl->AddCircleFilled(ImVec2(k.x, k.y + Px(1)), rad, Col(tok::Shadow), 20);
    dl->AddCircleFilled(k, rad, Col(Mix(tok::Hex(0xC9CCD4), tok::Hex(0xFFFFFF), t)), 20);
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
                                                    Col(tok::TextFaint), opts[i].flag);
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
    // current row
    float  rowX = 0, rowEndY = 0;
};
static CardCtx g_card;
static float   g_colW = 1e9f;   // readable content column width for this frame

static void SectionHeader(const char* title, const char* caption = nullptr)
{
    ImGui::Dummy(ImVec2(0, Px(tok::S2)));
    TextColored(g_fonts.strong, tok::Text, title);
    if (caption) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4));
        TextColored(g_fonts.small, tok::TextMuted, caption);
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
    DrawSurface(dl, a, b, tok::Surface, Px(tok::RLarge));
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
        dl->AddLine(ImVec2(p.x + padX, p.y), ImVec2(p.x + w - padX, p.y), Col(tok::Border));

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

    // Label (+ optional CLI flag chip) and description
    float ty = stacked ? p.y + padY : p.y + (rowH - textH) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(p.x + padX, ty));
    TextColored(g_fonts.strong, tok::Text, label);
    if (flag) {
        ImGui::PushFont(g_fonts.strong);
        float lx = ImGui::CalcTextSize(label).x;
        ImGui::PopFont();
        ImGui::PushFont(g_fonts.mono);
        ImVec2 fs = ImGui::CalcTextSize(flag);
        ImVec2 fa(p.x + padX + lx + Px(8), ty + (lh - fs.y) * 0.5f - Px(1));
        ImVec2 fb(fa.x + fs.x + Px(10), fa.y + fs.y + Px(2));
        dl->AddRectFilled(fa, fb, Col(tok::Hex(0xFFFFFF, 0.04f)), Px(tok::RSmall));
        dl->AddRect(fa, fb, Col(tok::Border), Px(tok::RSmall));
        dl->AddText(ImVec2(fa.x + Px(5), fa.y + Px(1)), Col(tok::TextFaint), flag);
        ImGui::PopFont();
    }
    if (desc) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + padX, ty + lh + Px(3)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
        TextColored(g_fonts.small, tok::TextMuted, desc);
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
    Toggle("##t", v);
    EndRow();
}

// ── Screens ───────────────────────────────────────────────────────────────────

struct Page { const char* name; const char* subtitle; Icon icon; };
static const Page kPages[] = {
    { "Connection", "How this PC appears to Apple devices and who can connect.", Icon::Connection },
    { "Display",    "Player window, video decoding and frame pacing.",          Icon::Display    },
    { "Audio",      "Where mirrored audio is played and how loud.",              Icon::Audio      },
    { "Advanced",   "Diagnostics, custom flags and the exact command line.",     Icon::Advanced   },
};
static const int kPageCount = (int)(sizeof(kPages) / sizeof(*kPages));

static void ShowToast(UiState& ui, const char* text)
{
    ui.toast = text;
    ui.toastUntil = ImGui::GetTime() + 2.2;
}

static void PageConnection(Settings& s)
{
    SectionHeader("Identity");
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

    SectionHeader("Access");
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
                                             : "Available while UxPlay is running.", kRot[rot]);
        BeginRow("Rotation", desc, nullptr, rot ? 216 : 120);
        ImGui::BeginDisabled(!running);
        float bh = ImGui::GetFrameHeight();
        if (ActionButton("Rotate 90\xc2\xb0", Icon::Rotate, true, ImVec2(Px(120), bh), BtnKind::Secondary,
                         "Rotate the player window clockwise without reconnecting"))
            host.setRotation((rot + 1) % 4);
        if (rot != 0) {
            ImGui::SameLine(0, Px(tok::S2));
            if (ActionButton("Reset", Icon::Rotate, false, ImVec2(Px(88), bh), BtnKind::Ghost))
                host.setRotation(0);
        }
        ImGui::EndDisabled();
        EndRow();
    }
    EndCard();

    SectionHeader("Video pipeline");
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
    SectionHeader("Output");
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

static void PageAdvanced(Settings& s, UiState& ui, const std::string& cmd)
{
    SectionHeader("Behavior");
    BeginCard();
    ToggleRow("Close window on disconnect", "Close the video window when a device disconnects instead of freezing on the last frame.",
              "-nofreeze", &s.noFreeze);
    ToggleRow("Debug logging", "Print detailed GStreamer and protocol logs to the console.", "-d", &s.debug);
    EndCard();

    SectionHeader("Custom arguments", "Appended verbatim after the generated options.");
    BeginCard();
    BeginRow("Extra arguments", nullptr, nullptr, 0);
    ImGui::PushFont(g_fonts.mono);
    ImGui::InputTextWithHint("##v", "e.g. -reset 15 -vsync no", s.extraArgs, sizeof(s.extraArgs));
    ImGui::PopFont();
    EndRow();
    EndCard();

    SectionHeader("Command line", "Exactly what Launch runs.");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetCursorScreenPos();
    float w = ImMin(ImGui::GetContentRegionAvail().x, g_colW), pad = Px(tok::S4);
    ImGui::PushFont(g_fonts.mono);
    float textW = w - pad * 2 - Px(96);
    ImVec2 ts = ImGui::CalcTextSize(cmd.c_str(), nullptr, false, textW);
    ImGui::PopFont();
    float h = ImMax(ts.y, ImGui::GetFrameHeight()) + pad * 2;
    ImVec2 b(a.x + w, a.y + h);
    DrawSurface(dl, a, b, tok::Hex(0x0F1014), Px(tok::RLarge), false);
    ImGui::SetCursorScreenPos(ImVec2(a.x + pad, a.y + pad));
    ImGui::PushFont(g_fonts.mono);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
    ImGui::PushStyleColor(ImGuiCol_Text, tok::Hex(0xC5C8F9));
    ImGui::TextUnformatted(cmd.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(b.x - pad - Px(84), a.y + pad));
    if (ActionButton("Copy", Icon::Copy, true, ImVec2(Px(84), ImGui::GetFrameHeight()), BtnKind::Secondary)) {
        ImGui::SetClipboardText(cmd.c_str());
        ShowToast(ui, "Command copied to clipboard");
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
    ImGui::Dummy(ImVec2(w, Px(tok::S4)));
}

// ── Sidebar ───────────────────────────────────────────────────────────────────

static void Sidebar(UiState& ui, const Settings& s, bool running, bool compact, float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    dl->AddRectFilledMultiColor(o, ImVec2(o.x + width, o.y + height),
        Col(tok::Hex(0x15161C)), Col(tok::Hex(0x15161C)), Col(tok::Sidebar), Col(tok::Sidebar));
    dl->AddLine(ImVec2(o.x + width - 0.5f, o.y), ImVec2(o.x + width - 0.5f, o.y + height), Col(tok::Border));

    const float pad = compact ? Px(12) : Px(tok::S4);

    // Brand
    float logo = Px(30);
    ImVec2 la(o.x + (compact ? (width - logo) * 0.5f : pad), o.y + Px(20));
    ImVec2 lb(la.x + logo, la.y + logo);
    dl->AddRectFilled(ImVec2(la.x, la.y + Px(2)), ImVec2(lb.x, lb.y + Px(2)), Col(tok::Shadow), Px(8));
    dl->AddRectFilled(la, lb, Col(tok::Hex(0x5A56E0)), Px(8));
    dl->AddRectFilled(la, ImVec2(lb.x, la.y + logo * 0.55f), Col(tok::Hex(0x9C99FF, 0.45f)), Px(8),
                      ImDrawFlags_RoundCornersTop);
    dl->AddRect(la, lb, Col(tok::Hex(0xFFFFFF, 0.18f)), Px(8));
    DrawIcon(dl, Icon::Logo, ImVec2(la.x + logo * 0.5f, la.y + logo * 0.5f), Px(17), Col(tok::Hex(0xFFFFFF)));
    if (!compact) {
        ImGui::SetCursorScreenPos(ImVec2(lb.x + Px(10), la.y - Px(1)));
        TextColored(g_fonts.strong, tok::Text, "UxPlay");
        ImGui::SetCursorScreenPos(ImVec2(lb.x + Px(10), la.y + Px(16)));
        TextColored(g_fonts.small, tok::TextFaint, "AirPlay receiver");
    }

    // Navigation
    float y = lb.y + Px(28);
    if (!compact) {
        ImGui::SetCursorScreenPos(ImVec2(o.x + pad + Px(4), y));
        TextColored(g_fonts.small, tok::TextFaint, "SETTINGS");
        y += Px(24);
    }
    const float itemH = Px(36);
    for (int i = 0; i < kPageCount; ++i) {
        ImVec2 a(o.x + Px(8), y), b(o.x + width - Px(8), y + itemH);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##nav", ImVec2(b.x - a.x, itemH))) ui.page = i;
        bool hov = ImGui::IsItemHovered(), sel = ui.page == i;
        float th = Animate(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 22.0f);
        float ts = Animate(ImGui::GetItemID() + 1, sel ? 1.0f : 0.0f, 18.0f);
        if (compact) ImGui::SetItemTooltip("%s", kPages[i].name);
        FocusRing(a, b, Px(tok::RMed));
        ImGui::PopID();

        float r = Px(tok::RMed);
        dl->AddRectFilled(a, b, Col(tok::Hex(0xFFFFFF, 0.04f * th * (1 - ts))), r);
        if (ts > 0.0f) {
            dl->AddRectFilled(a, b, Col(tok::AccentSoft, ts), r);
            dl->AddRect(a, b, Col(tok::Hex(0x6D6AF2, 0.28f * ts)), r);
            float bh = itemH * 0.5f * ts;
            dl->AddRectFilled(ImVec2(a.x - Px(8) + 1, a.y + (itemH - bh) * 0.5f),
                              ImVec2(a.x - Px(8) + Px(3) + 1, a.y + (itemH + bh) * 0.5f),
                              Col(tok::Accent, ts), Px(2));
        }
        ImVec4 fg = Mix(Mix(tok::TextMuted, tok::Text, th), tok::Hex(0xFFFFFF), ts);
        float ix = compact ? (a.x + b.x) * 0.5f : a.x + Px(18);
        DrawIcon(dl, kPages[i].icon, ImVec2(ix, a.y + itemH * 0.5f), Px(16),
                 Col(Mix(fg, tok::Hex(0xA9A7FF), ts)));
        if (!compact) {
            ImGui::PushFont(sel ? g_fonts.strong : g_fonts.body);
            float lh = ImGui::GetTextLineHeight();
            dl->AddText(ImVec2(a.x + Px(36), a.y + (itemH - lh) * 0.5f), Col(fg), kPages[i].name);
            ImGui::PopFont();
        }
        y += itemH + Px(2);
    }

    // Status card
    double now = ImGui::GetTime();
    ImVec4 dot = running ? tok::Success : (ui.exited ? tok::Warning : tok::TextFaint);
    const char* state = running ? "Running" : (ui.exited ? "Stopped" : "Idle");
    if (compact) {
        ImVec2 c(o.x + width * 0.5f, o.y + height - Px(28));
        float pulse = running ? 0.5f + 0.5f * (float)std::sin(now * 2.6) : 0.0f;
        if (running) dl->AddCircleFilled(c, Px(5) + Px(5) * pulse, Col(tok::Success, 0.25f * (1 - pulse)), 24);
        dl->AddCircleFilled(c, Px(4.5f), Col(dot), 16);
        ImGui::SetCursorScreenPos(ImVec2(c.x - Px(14), c.y - Px(14)));
        ImGui::InvisibleButton("##status", ImVec2(Px(28), Px(28)));
        ImGui::SetItemTooltip("UxPlay: %s", state);
        return;
    }
    float ch = Px(74);
    ImVec2 ca(o.x + Px(12), o.y + height - ch - Px(14)), cb(o.x + width - Px(12), o.y + height - Px(14));
    DrawSurface(dl, ca, cb, tok::Surface, Px(tok::RLarge));
    ImVec2 c(ca.x + Px(18), ca.y + Px(22));
    if (running) {
        float pulse = 0.5f + 0.5f * (float)std::sin(now * 2.6);
        dl->AddCircleFilled(c, Px(5) + Px(5) * pulse, Col(tok::Success, 0.25f * (1 - pulse)), 24);
    }
    dl->AddCircleFilled(c, Px(4.5f), Col(dot), 16);
    ImGui::SetCursorScreenPos(ImVec2(ca.x + Px(32), ca.y + Px(13)));
    TextColored(g_fonts.strong, running ? tok::Text : tok::TextMuted, state);

    std::string line1, line2;
    if (running) {
        int secs = (int)(now - ui.runningSince);
        char buf[64];
        snprintf(buf, sizeof(buf), "Up %d:%02d:%02d  \xc2\xb7  port %d", secs / 3600, (secs / 60) % 60, secs % 60, s.port);
        line1 = std::string("Visible as ") + (s.serverName[0] ? s.serverName : "hostname");
        line2 = buf;
    } else if (ui.exited) {
        line1 = "Receiver exited.";
        line2 = "Launch to start again.";
    } else {
        line1 = "Not receiving.";
        line2 = "Launch to go live.";
    }
    float tw = cb.x - ca.x - Px(28);
    ImGui::SetCursorScreenPos(ImVec2(ca.x + Px(14), ca.y + Px(36)));
    TextColored(g_fonts.small, tok::TextMuted, Ellipsize(g_fonts.small, line1, tw).c_str());
    ImGui::SetCursorScreenPos(ImVec2(ca.x + Px(14), ca.y + Px(52)));
    TextColored(g_fonts.small, tok::TextFaint, Ellipsize(g_fonts.small, line2, tw).c_str());
}

// ── Footer action bar ─────────────────────────────────────────────────────────

static void Footer(Settings& s, UiState& ui, const UiHost& host, bool running, bool dirty,
                   const std::string& cmd, ImVec2 a, ImVec2 b)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, Col(tok::Hex(0x101116)));
    dl->AddLine(ImVec2(a.x, a.y + 0.5f), ImVec2(b.x, a.y + 0.5f), Col(tok::Border));
    // soft shadow cast upward onto the scrolling content
    dl->AddRectFilledMultiColor(ImVec2(a.x, a.y - Px(10)), ImVec2(b.x, a.y),
        Col(tok::Hex(0x000000, 0.0f)), Col(tok::Hex(0x000000, 0.0f)),
        Col(tok::Hex(0x000000, 0.28f)), Col(tok::Hex(0x000000, 0.28f)));

    const float pad = Px(tok::S5), bh = Px(38);
    float y = a.y + ((b.y - a.y) - bh) * 0.5f;
    float launchW = Px(132), saveW = Px(104);
    float right = b.x - pad;

    // Primary: Launch / Stop
    ImGui::SetCursorScreenPos(ImVec2(right - launchW, y));
    if (!running) {
        if (ActionButton("Launch", Icon::Play, true, ImVec2(launchW, bh), BtnKind::Primary,
                         "Start uxplay.exe with these settings")) {
            std::string err;
            if (host.launch(s, &err)) {
                ui.error.clear();
                ui.exited = false;
                ui.runningSince = ImGui::GetTime();
                ShowToast(ui, "UxPlay is running");
            } else {
                ui.error = err.empty() ? "UxPlay could not be started." : err;
            }
        }
    } else {
        if (ActionButton("Stop", Icon::Stop, true, ImVec2(launchW, bh), BtnKind::Danger,
                         "Terminate the running receiver")) {
            host.stop();
            ui.exited = false;
            ui.wasRunning = false;   // user-initiated, not an unexpected exit
            ShowToast(ui, "UxPlay stopped");
        }
    }
    // Secondary: Save
    ImGui::SetCursorScreenPos(ImVec2(right - launchW - Px(tok::S2) - saveW, y));
    if (ActionButton(dirty ? "Save" : "Saved", Icon::Save, !dirty, ImVec2(saveW, bh), BtnKind::Secondary,
                     dirty ? "Save settings to uxplay-settings.ini" : "All changes saved")) {
        if (host.save(s)) {
            ui.savedSnapshot = SerializeSettings(s);
            ShowToast(ui, "Settings saved");
        } else {
            ui.error = "Could not write uxplay-settings.ini next to the launcher.";
        }
    }
    if (dirty) {   // unsaved-changes indicator
        ImVec2 r = ImGui::GetItemRectMin();
        dl->AddCircleFilled(ImVec2(r.x + Px(16), r.y + bh * 0.5f), Px(3.5f), Col(tok::Warning), 12);
    }

    // Command preview chip (click to copy)
    float chipR = right - launchW - saveW - Px(tok::S2) - Px(tok::S4);
    float chipW = chipR - (a.x + pad);
    if (chipW > Px(120)) {
        ImVec2 ca(a.x + pad, y), cb(chipR, y + bh);
        ImGui::SetCursorScreenPos(ca);
        bool clicked = ImGui::InvisibleButton("##cmd", ImVec2(cb.x - ca.x, bh));
        bool hov = ImGui::IsItemHovered();
        float th = Animate(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 20.0f);
        dl->AddRectFilled(ca, cb, Col(Mix(tok::Hex(0x0B0C0F), tok::Hex(0x15161B), th)), Px(tok::RMed));
        dl->AddRect(ca, cb, Col(Mix(tok::Border, tok::BorderStrong, th)), Px(tok::RMed));
        float iconSz = Px(13);
        DrawIcon(dl, Icon::Copy, ImVec2(cb.x - Px(12) - iconSz * 0.5f, ca.y + bh * 0.5f), iconSz,
                 Col(Mix(tok::TextFaint, tok::Text, th)));
        ImGui::PushFont(g_fonts.mono);
        float lh = ImGui::GetTextLineHeight();
        float px = ca.x + Px(12);
        dl->AddText(ImVec2(px, ca.y + (bh - lh) * 0.5f), Col(tok::Accent), "$");
        px += ImGui::CalcTextSize("$ ").x;
        ImGui::PopFont();
        std::string shown = Ellipsize(g_fonts.mono, cmd, cb.x - px - Px(32));
        ImGui::PushFont(g_fonts.mono);
        dl->AddText(ImVec2(px, ca.y + (bh - lh) * 0.5f), Col(tok::TextMuted), shown.c_str());
        ImGui::PopFont();
        FocusRing(ca, cb, Px(tok::RMed));
        if (hov) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(10), Px(8)));
            if (ImGui::BeginItemTooltip()) {
                ImGui::PushFont(g_fonts.mono);
                ImGui::PushTextWrapPos(Px(460));
                ImGui::TextUnformatted(cmd.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
                TextColored(g_fonts.small, tok::TextFaint, "Click to copy");
                ImGui::EndTooltip();
            }
            ImGui::PopStyleVar();
        }
        if (clicked) { ImGui::SetClipboardText(cmd.c_str()); ShowToast(ui, "Command copied to clipboard"); }
    }
}

// ── Root ──────────────────────────────────────────────────────────────────────

void DrawLauncherUI(Settings& s, UiState& ui, const UiHost& host, const UiFonts& fonts)
{
    g_fonts = fonts;
    ImGuiIO& io = ImGui::GetIO();
    double now = ImGui::GetTime();

    bool running = host.isRunning();
    if (ui.wasRunning && !running && !ui.exited) {
        // Distinguish "user pressed Stop" (cleared in Footer) from the process exiting on its own.
        ui.exited = true;
    }
    if (!ui.wasRunning && running && ui.runningSince == 0.0) ui.runningSince = now;
    if (!running) ui.runningSince = 0.0;
    ui.wasRunning = running;

    bool dirty = SerializeSettings(s) != ui.savedSnapshot;
    std::string cmd = "uxplay.exe";
    std::string args = BuildArgs(s);
    if (!args.empty()) { cmd += " "; cmd += args; }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);

    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const bool compact = W < Px(720);
    const float sideW = Px(compact ? tok::SidebarCompactW : tok::SidebarW);
    const float footH = Px(tok::FooterH);

    // Sidebar
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::BeginChild("##sidebar", ImVec2(sideW, H), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Sidebar(ui, s, running, compact, sideW, H);
    ImGui::EndChild();

    // Content canvas with a faint accent wash at the top
    ImDrawList* bg = ImGui::GetWindowDrawList();
    bg->AddRectFilledMultiColor(ImVec2(sideW, 0), ImVec2(W, Px(180)),
        Col(tok::Hex(0x6D6AF2, 0.07f)), Col(tok::Hex(0x6D6AF2, 0.02f)),
        Col(tok::Hex(0x6D6AF2, 0.0f)), Col(tok::Hex(0x6D6AF2, 0.0f)));

    const float padX = compact ? Px(tok::S5) : Px(tok::S6 + 4);
    ImGui::SetCursorPos(ImVec2(sideW, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, Px(28)));
    ImGui::BeginChild("##content", ImVec2(W - sideW, H - footH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    {
        // Constrain line length on wide windows so rows stay readable.
        float avail = ImGui::GetContentRegionAvail().x;
        float maxW = Px(760);
        if (avail > maxW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - maxW) * 0.5f);
        ImGui::BeginGroup();
        ImGui::PushItemWidth(ImMin(avail, maxW));
        float colW = ImMin(avail, maxW);
        g_colW = colW;
        ImGui::Dummy(ImVec2(colW, 0));   // fixes the group's width
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);

        // Page title with slide-in on change
        ImGuiID pageAnimId = ImGui::GetID("##pageanim");
        static int lastPage = -1;
        if (lastPage != ui.page) { ImGui::GetStateStorage()->SetFloat(pageAnimId, 0.0f); lastPage = ui.page; }
        float pa = Animate(pageAnimId, 1.0f, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f + 0.65f * pa);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Px(6) * (1 - pa));

        TextColored(g_fonts.title, tok::Text, kPages[ui.page].name);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(2));
        TextColored(g_fonts.body, tok::TextMuted, kPages[ui.page].subtitle);
        ImGui::Dummy(ImVec2(0, Px(tok::S2)));

        // Error banner
        if (!ui.error.empty()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 a = ImGui::GetCursorScreenPos();
            float w = colW, pad = Px(tok::S3 + 2);
            ImGui::PushFont(g_fonts.body);
            float th = ImGui::CalcTextSize(ui.error.c_str(), nullptr, false, w - Px(80)).y;
            ImGui::PopFont();
            float h = th + pad * 2;
            ImVec2 b(a.x + w, a.y + h);
            dl->AddRectFilled(a, b, Col(tok::Hex(0xE5484D, 0.10f)), Px(tok::RLarge));
            dl->AddRect(a, b, Col(tok::Hex(0xE5484D, 0.38f)), Px(tok::RLarge));
            DrawIcon(dl, Icon::Alert, ImVec2(a.x + pad + Px(8), a.y + pad + Px(9)), Px(16), Col(tok::Danger));
            ImGui::SetCursorScreenPos(ImVec2(a.x + pad + Px(26), a.y + pad));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - Px(80));
            ImGui::TextUnformatted(ui.error.c_str());
            ImGui::PopTextWrapPos();
            ImGui::SetCursorScreenPos(ImVec2(b.x - pad - Px(26), a.y + (h - Px(26)) * 0.5f));
            if (ActionButton("##dismiss", Icon::Close, false, ImVec2(Px(26), Px(26)), BtnKind::Ghost, "Dismiss"))
                ui.error.clear();
            DrawIcon(dl, Icon::Close, ImVec2(b.x - pad - Px(13), a.y + h * 0.5f), Px(12), Col(tok::TextMuted));
            ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
            ImGui::Dummy(ImVec2(w, Px(tok::S2)));
        }

        switch (ui.page) {
        case 0: PageConnection(s);         break;
        case 1: PageDisplay(s, host);      break;
        case 2: PageAudio(s);              break;
        case 3: PageAdvanced(s, ui, cmd);  break;
        }
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    // Footer
    Footer(s, ui, host, running, dirty, cmd, ImVec2(sideW, H - footH), ImVec2(W, H));

    // Toast (fades in/out above the footer, right aligned)
    if (!ui.toast.empty()) {
        float remain = (float)(ui.toastUntil - now);
        if (remain <= 0) { ui.toast.clear(); }
        else {
            float alpha = ImMin(1.0f, remain / 0.3f) * ImMin(1.0f, (2.2f - remain) / 0.15f + 0.0f);
            alpha = ImClamp(alpha, 0.0f, 1.0f);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImGui::PushFont(g_fonts.body);
            ImVec2 ts = ImGui::CalcTextSize(ui.toast.c_str());
            ImGui::PopFont();
            float pad = Px(14);
            ImVec2 b(W - Px(tok::S5), H - footH - Px(14) + Px(6) * (1 - alpha));
            ImVec2 a(b.x - ts.x - pad * 2 - Px(22), b.y - ts.y - pad * 1.4f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            fg->AddRectFilled(ImVec2(a.x, a.y + Px(4)), ImVec2(b.x, b.y + Px(6)), Col(tok::Hex(0x000000, 0.4f)), Px(tok::RLarge));
            fg->AddRectFilled(a, b, Col(tok::Hex(0x23252D)), Px(tok::RLarge));
            fg->AddRect(a, b, Col(tok::BorderStrong), Px(tok::RLarge));
            fg->AddCircleFilled(ImVec2(a.x + pad + Px(4), (a.y + b.y) * 0.5f), Px(4), Col(tok::Success), 12);
            fg->AddText(g_fonts.body, g_fonts.body->FontSize,
                        ImVec2(a.x + pad + Px(16), (a.y + b.y - ts.y) * 0.5f), Col(tok::Text), ui.toast.c_str());
            ImGui::PopStyleVar();
        }
    }

    ImGui::End();
}

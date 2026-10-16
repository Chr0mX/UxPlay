// UxPlay GUI Launcher — receiver session model (see session.h).

#include "session.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace {

bool Contains(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

bool StartsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

// Value of "key=" in a status line. "name" is always last and runs to the end.
std::string Field(const std::string& body, const char* key) {
    std::string k = std::string(" ") + key + "=";
    size_t p = (" " + body).find(k);
    if (p == std::string::npos) return "";
    size_t start = p + k.size() - 1;   // offset into body
    if (std::strcmp(key, "name") == 0) return body.substr(start);
    size_t end = body.find(' ', start);
    return body.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

}  // namespace

const char* PhaseLabel(Phase p) {
    switch (p) {
    case Phase::Off:        return "Receiver off";
    case Phase::Starting:   return "Starting";
    case Phase::Waiting:    return "Disconnected";
    case Phase::Connecting: return "Connecting";
    case Phase::Connected:  return "Connected";
    case Phase::Error:      return "Error";
    }
    return "";
}

std::string ClassifyProblemLine(const std::string& line) {
    if (Contains(line, "kDNSServiceErr_Unknown") || Contains(line, "No DNS-SD Server") ||
        Contains(line, "Could not initialize dnssd") || Contains(line, "dnssd.dll"))
        return "bonjour";
    if (Contains(line, "kDNSServiceErr_NameConflict")) return "name-conflict";
    if (Contains(line, "Error initializing raop") || Contains(line, "Address already in use") ||
        Contains(line, "WSAEADDRINUSE") || Contains(line, "bind failed") || Contains(line, "Error binding"))
        return "port";
    if (Contains(line, "no element \"") || Contains(line, "gst_parse_launch error") ||
        Contains(line, "could not link") || Contains(line, "missing plugin") ||
        Contains(line, "Required gstreamer plugin"))
        return "gstreamer";
    if (Contains(line, "invalid client-access password") || Contains(line, "Error: detected a non-ascii") ||
        Contains(line, "invalid \"") || Contains(line, "Unknown option") || Contains(line, "unknown option"))
        return "config";
    if (Contains(line, "lost connection with client")) return "connection-lost";
    return "";
}

ErrorInfo DescribeErrorKind(const std::string& kind) {
    ErrorInfo e;
    e.kind = kind;
    if (kind == "bonjour") {
        e.title  = "Device discovery is unavailable";
        e.detail = "UxPlay could not reach the Bonjour (mDNS) service, so iPhone and iPad cannot find this PC.";
        e.hint   = "Install Apple's \"Bonjour Print Services for Windows\" (or iTunes), make sure the "
                   "Bonjour Service is running, then try again.";
    } else if (kind == "name-conflict") {
        e.title  = "Server name already in use";
        e.detail = "Another AirPlay receiver on the network is advertising the same name.";
        e.hint   = "Choose a different server name on the Connection page, then try again.";
    } else if (kind == "port") {
        e.title  = "Network port unavailable";
        e.detail = "UxPlay could not open its network ports. Another program (often another UxPlay) may be using them.";
        e.hint   = "Close other AirPlay receivers, or set a different port on the Connection page.";
    } else if (kind == "gstreamer") {
        e.title  = "Video or audio component missing";
        e.detail = "A GStreamer component needed for the selected sink or decoder could not be loaded.";
        e.hint   = "Switch the video sink, decoder or audio sink to Auto, or reinstall UxPlay.";
    } else if (kind == "config") {
        e.title  = "Invalid setting";
        e.detail = "uxplay.exe rejected one of the options it was started with.";
        e.hint   = "Check the password and extra arguments, then try again. Details are in the console.";
    } else if (kind == "connection-lost") {
        e.title  = "Connection lost";
        e.detail = "The device stopped responding, usually because of a network interruption.";
        e.hint   = "Reconnect from the device's Screen Mirroring menu. The receiver is still running.";
        e.retry  = false;
        e.fatal  = false;
    } else if (kind == "exited") {
        e.title  = "UxPlay stopped unexpectedly";
        e.detail = "The receiver process exited on its own.";
        e.hint   = "Try again. If it keeps happening, open the console or log file for details.";
    } else if (kind == "startup") {
        e.title  = "UxPlay could not start";
        e.detail = "The receiver exited before it was ready to accept devices.";
        e.hint   = "Open the console for the exact message, adjust settings, then try again.";
    } else {
        e.kind   = "unknown";
        e.title  = "Something went wrong";
        e.detail = "The receiver reported an error.";
        e.hint   = "Open the console or log file for details.";
    }
    return e;
}

ErrorInfo MakeMissingExeError(const std::string& path) {
    ErrorInfo e;
    e.kind   = "missing-exe";
    e.title  = "uxplay.exe not found";
    e.detail = "The receiver program is missing from the UxPlay folder.";
    e.hint   = "Reinstall UxPlay, or run the launcher from the folder that contains uxplay.exe.";
    e.technical = "Expected at: " + path;
    return e;
}

ErrorInfo MakeLaunchError(const std::string& systemMessage, unsigned long code) {
    ErrorInfo e;
    e.kind   = "launch-failed";
    e.title  = "UxPlay could not be started";
    e.detail = "Windows refused to start uxplay.exe.";
    e.hint   = "Check that antivirus software is not blocking it, then try again.";
    char buf[64];
    std::snprintf(buf, sizeof(buf), " (error %lu)", code);
    e.technical = systemMessage + buf;
    return e;
}

void Session::SetPhase(Phase p, double now) {
    if (phase != p) { phase = p; phaseSince = now; }
}

void Session::SetError(const ErrorInfo& e, double now) {
    error = e;
    hasError = true;
    ++errorSerial;
    SetPhase(Phase::Error, now);
    std::string first = e.technical.substr(0, e.technical.find('\n'));
    Push(now, "[launcher] " + e.title + (first.empty() ? "" : " - " + first), LineLevel::App);
}

void Session::Push(double now, const std::string& text, LineLevel lvl) {
    ConsoleLine l;
    l.text = text; l.level = lvl; l.time = now;
    lines.push_back(std::move(l));
    ++linesAdded;
    while (lines.size() > maxLines) lines.pop_front();
}

void Session::AppNote(double now, const std::string& text) { Push(now, "[launcher] " + text, LineLevel::App); }

void Session::ClearConsole() { lines.clear(); }

void Session::DismissError() {
    hasError = false;
    if (phase == Phase::Error) phase = error.fatal ? Phase::Off : Phase::Waiting;
}

void Session::OnLaunched(double now, unsigned newPid, const std::string& cmd) {
    pid = newPid;
    port = 0;
    launchedAt = now;
    streamingSince = 0.0;
    deviceName.clear(); deviceModel.clear(); deviceId.clear(); deviceAddress.clear(); streamKinds.clear();
    recentProblems_.clear();
    pendingKind_.clear();
    stopRequested_ = false;
    sawReady_ = false;
    hasError = false;
    SetPhase(Phase::Starting, now);
    Push(now, "[launcher] Started uxplay.exe (PID " + std::to_string(newPid) + "): " + cmd, LineLevel::App);
}

void Session::OnLaunchFailed(double now, const ErrorInfo& err) {
    pid = 0;
    SetError(err, now);
}

void Session::OnStoppedByUser(double now) {
    stopRequested_ = true;
    Push(now, "[launcher] Receiver stopped.", LineLevel::App);
    pid = 0;
    hasError = false;
    SetPhase(Phase::Off, now);
}

void Session::OnProcessExited(double now, long exitCode) {
    if (stopRequested_ || phase == Phase::Off) return;   // expected
    pid = 0;
    std::string kind = !pendingKind_.empty() && pendingKind_ != "connection-lost"
                     ? pendingKind_ : (sawReady_ ? "exited" : "startup");
    ErrorInfo e = DescribeErrorKind(kind);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Exit code %ld.", exitCode);
    e.technical = buf;
    for (const auto& l : recentProblems_) e.technical += "\n" + l;
    e.fatal = true;
    e.retry = true;
    SetError(e, now);
}

void Session::HandleStatus(double now, const std::string& body) {
    std::string ev = body.substr(0, body.find(' '));
    if (ev == "ready") {
        sawReady_ = true;
        port = (unsigned) std::strtoul(Field(body, "port").c_str(), nullptr, 10);
        if (phase == Phase::Starting) SetPhase(Phase::Waiting, now);
    } else if (ev == "client-request") {
        bool admit = Field(body, "admit") == "1";
        std::string name = Field(body, "name"), model = Field(body, "model");
        if (!admit) {
            Push(now, "[launcher] Refused connection from " + name + " (device not allowed).", LineLevel::Warning);
            return;
        }
        deviceName = name; deviceModel = model; deviceId = Field(body, "deviceid");
        deviceAddress = lastRemote_;
        streamKinds.clear();
        streamingSince = 0.0;
        hasError = false;
        SetPhase(Phase::Connecting, now);
    } else if (ev == "stream-start") {
        std::string kind = Field(body, "kind");
        if (streamKinds.find(kind) == std::string::npos)
            streamKinds = streamKinds.empty() ? kind : "audio + video";
        if (phase != Phase::Connected) { streamingSince = now; hasError = false; SetPhase(Phase::Connected, now); }
    } else if (ev == "connection-closed") {
        if (Field(body, "count") == "0" && (phase == Phase::Connecting || phase == Phase::Connected)) {
            Push(now, "[launcher] " + (deviceName.empty() ? std::string("Device") : deviceName) + " disconnected.",
                 LineLevel::App);
            streamingSince = 0.0;
            SetPhase(Phase::Waiting, now);
        }
    } else if (ev == "connection-reset") {
        if (Field(body, "reason") == "1") {
            ErrorInfo e = DescribeErrorKind("connection-lost");
            e.technical = deviceName.empty() ? "" : "Device: " + deviceName;
            streamingSince = 0.0;
            SetError(e, now);
        }
    } else if (ev == "error") {
        // uxplay.exe may keep running after these (e.g. without Bonjour it is
        // alive but undiscoverable), so report now instead of waiting for an exit.
        std::string kind = Field(body, "kind");
        if (pendingKind_.empty()) pendingKind_ = kind == "dnssd" ? "bonjour" : "port";
        ErrorInfo e = DescribeErrorKind(pendingKind_);
        for (const auto& l : recentProblems_) e.technical += (e.technical.empty() ? "" : "\n") + l;
        e.fatal = false;   // the process is still running; Try again restarts it
        e.retry = true;
        SetError(e, now);
    }
}

void Session::OnLine(double now, const std::string& raw) {
    std::string line = Trim(raw);
    if (line.empty()) return;

    if (StartsWith(line, "@uxplay-status ")) {   // protocol line: not shown in the console
        HandleStatus(now, line.substr(15));
        return;
    }

    LineLevel lvl = LineLevel::Info;
    if (StartsWith(line, "*** ERROR") || Contains(line, "ERROR") || Contains(line, "error:"))
        lvl = LineLevel::Error;
    else if (StartsWith(line, "*** WARNING") || Contains(line, "WARNING") || Contains(line, "WARN "))
        lvl = LineLevel::Warning;

    if (StartsWith(line, "Remote: ")) lastRemote_ = line.substr(8);
    if (uxplayVersion.empty() && StartsWith(line, "UxPlay ")) {
        size_t colon = line.find(':');
        if (colon != std::string::npos) uxplayVersion = line.substr(7, colon - 7);
    }

    std::string kind = ClassifyProblemLine(line);
    if (!kind.empty()) {
        if (kind != "connection-lost") pendingKind_ = kind;
        if (lvl == LineLevel::Info) lvl = LineLevel::Error;
    }
    if (lvl != LineLevel::Info) {
        recentProblems_.push_back(line);
        if (recentProblems_.size() > 6) recentProblems_.erase(recentProblems_.begin());
    }
    Push(now, line, lvl);
}

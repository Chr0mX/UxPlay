// UxPlay GUI Launcher — receiver session model.
//
// Turns what the launcher actually observes about uxplay.exe (launch result,
// captured stdout/stderr lines, process exit) into a connection state, a
// bounded console buffer and user-facing error descriptions. Platform-free
// (standard C++ only) so it can be unit-tested anywhere; the Win32 host feeds
// it from the UI thread.
//
// State only changes on real evidence: uxplay.cpp prints
// "@uxplay-status <event> ..." lines when started with UXPLAY_GUI_STATUS set.
// Launching the process alone never produces "Connected".

#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

enum class Phase {
    Off,          // receiver process not running
    Starting,     // process launched, not yet advertising
    Waiting,      // advertising; no device connected ("Disconnected")
    Connecting,   // a device was admitted, no media received yet
    Connected,    // audio or video is streaming
    Error,        // launch failure, unexpected exit, or lost connection
};

enum class LineLevel : uint8_t { Info, Warning, Error, App };

struct ConsoleLine {
    std::string text;
    LineLevel   level = LineLevel::Info;
    double      time  = 0.0;   // seconds, host clock
};

struct ErrorInfo {
    std::string kind;     // stable key, used to de-duplicate notifications
    std::string title;    // what happened, one line
    std::string detail;   // plain-language explanation
    std::string hint;     // next step
    std::string technical;// raw evidence (last error lines, exit code)
    bool        retry = true;   // offering "Try again" makes sense
    bool        fatal = true;   // receiver is no longer running
};

class Session {
public:
    // Lifecycle, driven by the host.
    void OnLaunched(double now, unsigned pid, const std::string& commandLineRedacted);
    void OnLaunchFailed(double now, const ErrorInfo& err);
    void OnStoppedByUser(double now);
    void OnProcessExited(double now, long exitCode);
    void OnLine(double now, const std::string& raw);   // one line of uxplay output
    void AppNote(double now, const std::string& text); // launcher's own console note
    void DismissError();
    void ClearConsole();

    // Observed state.
    Phase       phase        = Phase::Off;
    double      phaseSince   = 0.0;
    double      launchedAt   = 0.0;
    double      streamingSince = 0.0;
    unsigned    pid          = 0;
    unsigned    port         = 0;
    std::string deviceName, deviceModel, deviceId, deviceAddress;
    std::string streamKinds;          // "audio", "video", "audio + video"
    unsigned    videoW = 0, videoH = 0;   // current stream frame size (0 = unknown)
    int         videoSizeSerial = 0;      // increments on each size report (rotation, new stream)
    std::string uxplayVersion;        // from the startup banner, if seen
    std::string discovery;            // "builtin" (UxPlay's own mDNS) or "bonjour" (Apple), once advertised
    bool        hasError     = false;
    ErrorInfo   error;
    int         errorSerial  = 0;     // increments on each new error

    // Console buffer (bounded).
    std::deque<ConsoleLine> lines;
    uint64_t    linesAdded   = 0;     // monotonic, for auto-scroll
    size_t      maxLines     = 5000;

    bool Running() const { return phase != Phase::Off && !(phase == Phase::Error && error.fatal); }

private:
    void SetPhase(Phase p, double now);
    void SetError(const ErrorInfo& e, double now);
    void Push(double now, const std::string& text, LineLevel lvl);
    void HandleStatus(double now, const std::string& body);

    std::vector<std::string> recentProblems_;   // last few warning/error lines
    std::string pendingKind_;                    // error kind inferred from output
    std::string lastRemote_;                     // "Remote: <ip>" before a request
    bool        stopRequested_ = false;
    bool        sawReady_      = false;
};

const char* PhaseLabel(Phase p);

// Error descriptions for failures the host detects itself.
ErrorInfo MakeMissingExeError(const std::string& path);
ErrorInfo MakeLaunchError(const std::string& systemMessage, unsigned long code);

// Exposed for tests: classify an output line into an error kind ("" if none).
std::string ClassifyProblemLine(const std::string& line);
ErrorInfo   DescribeErrorKind(const std::string& kind);

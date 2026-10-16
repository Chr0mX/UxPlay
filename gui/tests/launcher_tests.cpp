// Unit tests for the launcher's platform-free logic (session model, prefs).
// Build: cmake -DUXPLAY_GUI_TESTS=ON ... or compile directly:
//   g++ -std=c++17 -I gui gui/tests/launcher_tests.cpp gui/session.cpp gui/prefs.cpp

#include "session.h"
#include "prefs.h"

#include <cstdio>
#include <string>

static int g_failed = 0, g_run = 0;
#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_failed; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestLaunchIsNotConnected()
{
    Session s;
    s.OnLaunched(0.0, 1234, "uxplay.exe");
    CHECK(s.phase == Phase::Starting);          // launching alone is never "connected"
    s.OnLine(0.1, "UxPlay 1.73.6: An Open-Source AirPlay mirroring and audio-streaming server.");
    CHECK(s.uxplayVersion == "1.73.6");
    CHECK(s.phase == Phase::Starting);
    s.OnLine(0.2, "@uxplay-status ready port=7100");
    CHECK(s.phase == Phase::Waiting);
    CHECK(s.port == 7100);
}

static void TestFullConnectionLifecycle()
{
    Session s;
    s.OnLaunched(0, 1, "x");
    s.OnLine(1, "@uxplay-status ready port=7100");
    s.OnLine(2, "Remote: 192.168.1.20");
    s.OnLine(2, "@uxplay-status connection-open count=1");
    CHECK(s.phase == Phase::Waiting);           // a bare TCP connection (e.g. /info probe) is not a session
    s.OnLine(3, "@uxplay-status client-request admit=1 deviceid=AA:BB model=iPhone15,2 name=Alex's iPhone");
    CHECK(s.phase == Phase::Connecting);
    CHECK(s.deviceName == "Alex's iPhone");
    CHECK(s.deviceModel == "iPhone15,2");
    CHECK(s.deviceAddress == "192.168.1.20");
    s.OnLine(4, "@uxplay-status stream-start kind=video");
    CHECK(s.phase == Phase::Connected);
    CHECK(s.streamingSince == 4);
    s.OnLine(5, "@uxplay-status stream-start kind=audio");
    CHECK(s.streamKinds == "audio + video");
    s.OnLine(6, "@uxplay-status connection-closed count=1");
    CHECK(s.phase == Phase::Connected);         // other connections still open
    s.OnLine(7, "@uxplay-status connection-closed count=0");
    CHECK(s.phase == Phase::Waiting);
    // Protocol lines are not shown in the console.
    for (const auto& l : s.lines) CHECK(l.text.find("@uxplay-status") == std::string::npos);
}

static void TestDeniedClientStaysDisconnected()
{
    Session s;
    s.OnLaunched(0, 1, "x");
    s.OnLine(1, "@uxplay-status ready port=7100");
    s.OnLine(2, "@uxplay-status client-request admit=0 deviceid=X model=M name=Blocked");
    CHECK(s.phase == Phase::Waiting);
}

static void TestConnectionLostIsNonFatal()
{
    Session s;
    s.OnLaunched(0, 1, "x");
    s.OnLine(1, "@uxplay-status ready port=7100");
    s.OnLine(2, "@uxplay-status client-request admit=1 deviceid=X model=M name=Pad");
    s.OnLine(3, "@uxplay-status stream-start kind=video");
    s.OnLine(4, "*** ERROR lost connection with client (network problem?)");
    s.OnLine(4, "@uxplay-status connection-reset reason=1");
    CHECK(s.phase == Phase::Error);
    CHECK(s.hasError && s.error.kind == "connection-lost" && !s.error.fatal);
    CHECK(s.Running());
    s.OnLine(5, "@uxplay-status connection-closed count=0");
    CHECK(s.phase == Phase::Error);             // error stays visible until something new happens
    s.OnLine(9, "@uxplay-status client-request admit=1 deviceid=X model=M name=Pad");
    CHECK(s.phase == Phase::Connecting && !s.hasError);
}

static void TestStartupFailureClassified()
{
    Session s;
    s.OnLaunched(0, 1, "x");
    s.OnLine(1, "*** ERROR: No DNS-SD Server found (DNSServiceRegister call returned kDNSServiceErr_Unknown)");
    s.OnLine(1, "@uxplay-status error kind=dnssd");
    CHECK(s.phase == Phase::Error);             // reported immediately: uxplay keeps running undiscoverable
    CHECK(s.error.kind == "bonjour" && !s.error.fatal && s.error.retry && s.Running());
    s.OnProcessExited(2, 1);
    CHECK(s.phase == Phase::Error);
    CHECK(s.error.kind == "bonjour");
    CHECK(s.error.fatal && s.error.retry);
    CHECK(s.error.technical.find("Exit code 1") != std::string::npos);
    CHECK(s.error.technical.find("kDNSServiceErr_Unknown") != std::string::npos);
    CHECK(!s.Running());
}

static void TestPortAndPluginErrors()
{
    CHECK(ClassifyProblemLine("*** ERROR: Error initializing raop (2)!") == "port");
    CHECK(ClassifyProblemLine("gst_parse_launch error (audio 1): no element \"level\"") == "gstreamer");
    CHECK(ClassifyProblemLine("DNSServiceRegister call returned kDNSServiceErr_NameConflict") == "name-conflict");
    CHECK(ClassifyProblemLine("invalid client-access password \"ab\": length must be at least 4") == "config");
    CHECK(ClassifyProblemLine("Required gstreamer plugin 'libav' not found") == "gstreamer");
    CHECK(ClassifyProblemLine("Open connections: 1") == "");
}

static void TestUnexpectedExitAfterReady()
{
    Session s;
    s.OnLaunched(0, 1, "x");
    s.OnLine(1, "@uxplay-status ready port=7100");
    s.OnProcessExited(50, -1073741819);
    CHECK(s.error.kind == "exited");
    s = Session();
    s.OnLaunched(0, 1, "x");
    s.OnProcessExited(1, 3);
    CHECK(s.error.kind == "startup");
}

static void TestUserStopIsNotACrash()
{
    Session s;
    s.OnLaunched(0, 1, "x");
    s.OnLine(1, "@uxplay-status ready port=7100");
    s.OnStoppedByUser(2);
    s.OnProcessExited(2, 0);
    CHECK(s.phase == Phase::Off);
    CHECK(!s.hasError);
    CHECK(s.errorSerial == 0);
}

static void TestLaunchFailure()
{
    Session s;
    s.OnLaunchFailed(0, MakeMissingExeError("C:\\x\\uxplay.exe"));
    CHECK(s.phase == Phase::Error && s.error.kind == "missing-exe" && !s.Running());
    s.DismissError();
    CHECK(s.phase == Phase::Off && !s.hasError);
}

static void TestConsoleBounded()
{
    Session s;
    s.maxLines = 100;
    for (int i = 0; i < 1000; ++i) s.OnLine(i, "line " + std::to_string(i));
    CHECK(s.lines.size() == 100);
    CHECK(s.lines.back().text == "line 999");
    CHECK(s.linesAdded == 1000);
}

static void TestPrefsRoundTripAndValidation()
{
    UiPrefs p;
    CHECK(!p.showConsoleOnStartup);             // console hidden by default
    p.theme = ThemeMode::Light; p.x = -1200; p.y = 40; p.w = 1000; p.h = 700;
    p.maximized = true; p.showConsoleOnStartup = true; p.consoleHeight = 300; p.page = 2;
    UiPrefs q = ParsePrefs(SerializePrefs(p));
    CHECK(q.theme == ThemeMode::Light && q.x == -1200 && q.y == 40 && q.w == 1000 && q.h == 700);
    CHECK(q.maximized && q.showConsoleOnStartup && q.consoleHeight == 300 && q.page == 2);

    UiPrefs bad = ParsePrefs("theme=9\nwindowW=abc\nwindowH=500\nconsoleHeight=5\npage=-3\nshowConsoleOnStartup=yes\n\x01garbage");
    CHECK(bad.theme == ThemeMode::System);
    CHECK(bad.w == 0 && bad.h == 0);            // half a geometry is no geometry
    CHECK(bad.consoleHeight == 240.0f);
    CHECK(bad.page == 0);
    CHECK(!bad.showConsoleOnStartup);

    UiPrefs empty = ParsePrefs("");
    CHECK(empty.w == 0 && !empty.maximized);
    UiPrefs crlf = ParsePrefs("theme=2\r\nmaximized=1\r\n");
    CHECK(crlf.theme == ThemeMode::Dark && crlf.maximized);
}

int main()
{
    TestLaunchIsNotConnected();
    TestFullConnectionLifecycle();
    TestDeniedClientStaysDisconnected();
    TestConnectionLostIsNonFatal();
    TestStartupFailureClassified();
    TestPortAndPluginErrors();
    TestUnexpectedExitAfterReady();
    TestUserStopIsNotACrash();
    TestLaunchFailure();
    TestConsoleBounded();
    TestPrefsRoundTripAndValidation();
    std::printf("%d checks, %d failed\n", g_run, g_failed);
    return g_failed ? 1 : 0;
}

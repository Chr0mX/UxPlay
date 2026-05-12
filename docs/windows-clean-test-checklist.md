# Windows Clean-VM Test Checklist (Windows 10 / Windows 11)

Use this checklist for QA validation on **pristine** Windows VMs (fresh OS install, no developer toolchains preinstalled).

## Test matrix

- OS: Windows 10 (64-bit), Windows 11 (64-bit)
- VM state: clean snapshot before first run
- Network: same local subnet as AirPlay sender test device
- Audio path: host speakers/headset passthrough as available

## 1) First-launch success criteria

- [ ] Portable ZIP downloads and extracts without errors.
- [ ] `uxplay.exe` launches from extracted folder without missing-DLL errors.
- [ ] Firewall prompt appears (or existing rule can be configured) and private-network access can be granted.
- [ ] UxPlay appears in AirPlay target list from iOS/iPadOS/macOS sender within expected discovery time.

**Pass criteria:** first usable mirror session can be started on a clean VM without installing MSYS2 or extra runtime packages.

## 2) Screen-mirroring latency expectations

- [ ] Start mirror session from at least one modern iPhone/iPad and one macOS sender (if available).
- [ ] Observe interactive operations (home-screen swipe, app open, scrolling text).
- [ ] Measure rough end-to-end delay with visual cue method (stopwatch/clap/video cue).

**Expected:** latency is low enough for general mirroring and media viewing on LAN; brief jitter spikes may occur in VM environments.

## 3) Fullscreen stability and monitor switching

- [ ] Enter fullscreen after stream start.
- [ ] Exit fullscreen repeatedly (at least 5 cycles).
- [ ] If VM exposes multiple virtual displays, switch active monitor/display target and repeat fullscreen test.
- [ ] Confirm no crash/hang when sender rotates orientation during fullscreen.

**Pass criteria:** no process crash, no permanent black frame, and fullscreen transitions remain responsive.

## 4) Audio/video sync checks

- [ ] Play a clip with clear lip-sync cues.
- [ ] Verify continuous playback for at least 10 minutes.
- [ ] Check for drift (A/V desync increasing over time).
- [ ] Pause/resume from sender and confirm sync recovers correctly.

**Expected:** stable sync suitable for normal viewing; minor transient offsets after network hiccups can self-correct.

## 5) Hardware decode path verification and fallback

- [ ] Run with preferred hardware-accelerated Windows video sink.
- [ ] Confirm stream renders normally and CPU usage is within expected range for VM/GPU profile.
- [ ] Simulate/force fallback scenario (unsupported sink/GPU feature level) using alternate sink option.
- [ ] Confirm software/alternate path still produces functional playback.

**Pass criteria:** hardware path works where available, and fallback path remains functional without crash.

## 6) Basic failure triage data to capture

For any failure, collect:

- Command line used to launch `uxplay.exe`
- OS build number and VM platform details
- Whether Bonjour or BLE discovery mode was used
- Firewall decision made at first prompt
- Repro steps + timestamp + screenshot/video

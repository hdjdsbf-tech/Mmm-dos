#include "Session.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <algorithm>
#include <cmath>
#include <optional>

#include "Overlay.hpp"

using namespace geode::prelude;

namespace fp::session {
namespace {
MacroRecorder g_recorder;
Playback g_playback;
std::filesystem::path g_selected;
std::size_t g_selectedEvent = 0;
std::optional<Macro> g_selectedMacro;
bool g_armed = false;
bool g_diverged = false;      // level was reset after the recording started
bool g_injecting = false;
bool g_speedActive = false;
float g_originalScale = 1.f;

std::uint32_t currentLevelId() {
    auto* pl = PlayLayer::get();
    if (!pl || !pl->m_level) return 0;
    return static_cast<std::uint32_t>(pl->m_level->m_levelID.value());
}
}

std::filesystem::path macrosDir() { return Mod::get()->getSaveDir() / "macros"; }
std::filesystem::path pendingPath() { return Mod::get()->getSaveDir() / "pending.fpm.json"; }

std::vector<std::filesystem::path> listMacros() {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::exists(macrosDir(), ec)) return out;
    for (auto const& entry : std::filesystem::directory_iterator(macrosDir(), ec)) {
        if (ec) break;
        auto name = entry.path().filename().string();
        if (entry.is_regular_file() && (name.ends_with(".fpm.json") || name.ends_with(".gdr.json")))
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

MacroRecorder& recorder() { return g_recorder; }
Playback& playback() { return g_playback; }

float speedMultiplier() {
    auto v = static_cast<float>(Mod::get()->getSettingValue<double>("speed-multiplier"));
    if (!std::isfinite(v)) return 1.f;
    return std::clamp(v, 0.10f, 3.00f);
}

float configuredTps() {
    auto v = static_cast<float>(Mod::get()->getSettingValue<int64_t>("tps"));
    return std::clamp(v, 30.f, 1000.f);
}

std::filesystem::path const& selected() { return g_selected; }
void select(std::filesystem::path p) {
    g_selected = std::move(p);
    g_selectedEvent = 0;
    g_selectedMacro.reset();
    if (!g_selected.empty()) {
        auto r = loadMacroFile(g_selected);
        if (r.status == LoadStatus::Ok) g_selectedMacro = std::move(r.macro);
    }
}
std::size_t selectedEvent() { return g_selectedEvent; }
void setSelectedEvent(std::size_t i) { g_selectedEvent = i; refreshOverlay(); }
void setSelectedMacro(Macro m) { g_selectedMacro = std::move(m); }
Macro const* selectedMacro() { return g_selectedMacro ? &*g_selectedMacro : nullptr; }

bool armed() { return g_armed; }

bool playerAllowed(bool player2) {
    auto f = Mod::get()->getSettingValue<std::string>("player-filter");
    if (f == "Player 1") return !player2;
    if (f == "Player 2") return player2;
    return true;
}

void applySpeed() {
    if (g_speedActive) return;
    if (!Mod::get()->getSettingValue<bool>("speedhack-enabled")) return;
    if (g_recorder.state() != SessionState::Recording) return;
    auto* sched = CCDirector::get()->getScheduler();
    g_originalScale = sched->getTimeScale();
    sched->setTimeScale(speedMultiplier());
    g_speedActive = true;
}

void restoreSpeed() {
    if (!g_speedActive) return;
    CCDirector::get()->getScheduler()->setTimeScale(g_originalScale);
    g_speedActive = false;
}

std::string startRecording() {
    auto* pl = PlayLayer::get();
    if (!pl) return "Open a level first";
    if (g_playback.active()) return "Stop playback first";
    if (g_recorder.state() == SessionState::Recording) return "Already recording";
    g_diverged = false;
    g_armed = true;
    // Entering Practice Mode is allowed here because the user pressed Record.
    if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
    pl->resetLevel();
    return {};
}

static void beginIfReady(PlayLayer* pl) {
    if (!g_armed || !pl->m_isPracticeMode) return; // never record outside Practice Mode
    MacroInfo info;
    info.levelId = currentLevelId();
    info.levelName = pl->m_level ? std::string(pl->m_level->m_levelName) : "";
    info.tps = configuredTps();
    info.speedMultiplier = Mod::get()->getSettingValue<bool>("speedhack-enabled") ? speedMultiplier() : 1.f;
    g_armed = false;
    if (!g_recorder.begin(std::move(info), pendingPath())) {
        Notification::create("Recording failed: " + g_recorder.lastError(), NotificationIcon::Error)->show();
        return;
    }
    applySpeed();
    Notification::create("Recording started", NotificationIcon::Info)->show();
}

void stopAndSavePartial(std::string& out) {
    restoreSpeed();
    g_armed = false;
    auto st = g_recorder.state();
    if (st != SessionState::Recording && st != SessionState::Interrupted) { out = "Nothing to save"; return; }
    auto name = fmt::format("level{}-partial-{}f.fpm.json", g_recorder.info().levelId, g_recorder.currentFrame());
    auto path = macrosDir() / name;
    if (!g_recorder.savePartial(path)) { out = g_recorder.lastError(); return; }
    g_recorder.discard();
    out = "Saved partial: " + name;
}

void discardRecording() {
    restoreSpeed();
    g_armed = false;
    g_recorder.discard();
}

bool resumeAllowed() {
    return g_recorder.state() == SessionState::Interrupted && !g_diverged;
}

std::string resumeRecording() {
    if (!resumeAllowed()) return "Cannot resume: the level was restarted. Save as partial or record again.";
    if (!g_recorder.resume()) return g_recorder.lastError();
    applySpeed();
    return {};
}

std::string startPlayback(Macro macro) {
    auto* pl = PlayLayer::get();
    if (!pl) return "Open a level first";
    if (g_recorder.state() == SessionState::Recording) return "Stop recording first";
    if (!g_playback.start(std::move(macro))) return "Macro has no inputs";
    pl->resetLevel();
    return {};
}

static void releaseHeld() {
    if (!g_playback.active()) return;
    auto* pl = PlayLayer::get();
    if (!pl) return;
    g_injecting = true;
    g_playback.releaseAll([&](InputEvent const& e) {
        static_cast<GJBaseGameLayer*>(pl)->handleButton(false, e.button, !e.player2);
    });
    g_injecting = false;
}

void stopPlayback() {
    releaseHeld();
    g_playback.stop();
}

bool isInjecting() { return g_injecting; }

void onStep(GJBaseGameLayer* layer, float, bool beforeOriginal, bool countTick) {
    auto* pl = PlayLayer::get();
    if (!pl || static_cast<GJBaseGameLayer*>(pl) != layer) return;

    if (beforeOriginal) {
        if (g_armed) beginIfReady(pl);
        if (countTick && g_playback.active() && !g_playback.paused()) {
            g_injecting = true;
            g_playback.step([&](InputEvent const& e) {
                if (!playerAllowed(e.player2)) return;
                layer->handleButton(e.down, e.button, !e.player2);
            });
            g_injecting = false;
            if (g_playback.finished()) {
                stopPlayback();
                Notification::create("Playback finished", NotificationIcon::Success)->show();
            }
        }
        return;
    }

    if (countTick && g_recorder.state() == SessionState::Recording) {
        if (!g_recorder.onGameFrame()) {
            restoreSpeed();
            Notification::create("Recording error: " + g_recorder.lastError(), NotificationIcon::Error)->show();
        }
    }
    refreshOverlay();
}

void onButton(bool down, int button, bool isPlayer1) {
    if (g_injecting) return;
    if (g_recorder.state() != SessionState::Recording) return;
    if (!playerAllowed(!isPlayer1)) return;
    if (!g_recorder.captureInput(button, down, !isPlayer1)) {
        restoreSpeed();
        Notification::create("Input not recorded: " + g_recorder.lastError(), NotificationIcon::Error)->show();
    }
}

void onReset() {
    if (g_playback.active()) {
        releaseHeld();
        g_playback.rewind();
    }
    if (g_recorder.state() == SessionState::Recording) {
        g_diverged = true;
        restoreSpeed();
        g_recorder.interrupt();
    }
}

void onDeath() {
    restoreSpeed();
    if (g_playback.active()) releaseHeld();
    if (g_recorder.state() == SessionState::Recording) {
        g_diverged = true;
        g_recorder.interrupt();
    }
}

void onQuit() {
    restoreSpeed();
    g_armed = false;
    stopPlayback();
    // Keep the checkpoint file on disk (it is offered for recovery on the next
    // level entry) but drop the in-memory session.
    if (g_recorder.state() == SessionState::Recording) g_recorder.interrupt();
    g_recorder.reset();
}

void onLevelEntered(PlayLayer*) {
    auto path = pendingPath();
    std::error_code ec;
    if (g_recorder.state() != SessionState::Idle || !std::filesystem::exists(path, ec)) return;
    auto r = loadMacroFile(path);
    bool ok = r.status == LoadStatus::Ok;
    std::string desc = ok
        ? fmt::format("Found an unfinished recording: level {} \"{}\", {} inputs, {} ticks.\n\n"
                      "Save it as a partial macro, or delete it?",
                      r.macro.info.levelId, r.macro.info.levelName, r.macro.inputs.size(), r.macro.durationFrames)
        : "The unfinished recording is damaged (" + r.message + ").\n\nDelete it, or keep the file?";
    Loader::get()->queueInMainThread([=]() {
        createQuickPopup("Unfinished recording", desc, ok ? "Delete" : "Keep", ok ? "Save partial" : "Delete",
            [r, path, ok](FLAlertLayer*, bool second) {
                std::error_code e2;
                if (ok && second) {
                    Macro m = r.macro;
                    m.complete = false;
                    auto name = fmt::format("level{}-recovered-{}f.fpm.json", m.info.levelId, m.durationFrames);
                    if (writeMacroFile(macrosDir() / name, m)) std::filesystem::remove(path, e2);
                    else FLAlertLayer::create("Save failed", "Could not write the recovered macro. The checkpoint was kept.", "OK")->show();
                } else if (ok && !second) {
                    std::filesystem::remove(path, e2);
                } else if (!ok && second) {
                    std::filesystem::remove(path, e2);
                }
            });
    });
}

void onPauseOpened() {
    restoreSpeed(); // game is paused: no frames advance, normal speed in menus
    if (g_playback.active()) { releaseHeld(); g_playback.pause(true); }
}

void onResumed() {
    if (g_playback.active()) g_playback.pause(false);
    if (g_recorder.state() == SessionState::Recording) applySpeed();
}

void onLevelComplete() {
    restoreSpeed();
    if (g_playback.active()) stopPlayback();
    if (g_recorder.state() != SessionState::Recording) return;
    auto name = fmt::format("level{}-complete-{}f.fpm.json", g_recorder.info().levelId, g_recorder.currentFrame());
    if (g_recorder.complete(macrosDir() / name))
        Notification::create("Macro completed and saved: " + name, NotificationIcon::Success)->show();
    else
        Notification::create("Could not save macro: " + g_recorder.lastError(), NotificationIcon::Error)->show();
    g_recorder.reset();
}

std::string statusText() {
    if (g_playback.active())
        return fmt::format("Playback {}  frame {}", g_playback.paused() ? "(paused)" : "", g_playback.frame());
    if (g_armed) return "Armed: waiting for Practice Mode restart";
    switch (g_recorder.state()) {
        case SessionState::Recording: return fmt::format("Recording  frame {}  inputs {}", g_recorder.currentFrame(), g_recorder.inputCount());
        case SessionState::Interrupted: return fmt::format("Interrupted at frame {} ({} inputs)", g_recorder.currentFrame(), g_recorder.inputCount());
        case SessionState::Error: return "Error: " + g_recorder.lastError();
        case SessionState::Completed: return "Completed";
        default: return "Idle";
    }
}

} // namespace fp::session

#include "UI.hpp"

#include "Picker.hpp"
#include "Session.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace fp::ui {
namespace {

constexpr ccColor3B Orange{255, 140, 0};

struct AnalysisState {
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<float> progress{0.f};
    fp::Stats stats;
    float tps = 240.f;
};

std::string g_lastStatsText;
std::string g_lastStatsSummary;

void alert(std::string const& title, std::string const& text) {
    FLAlertLayer::create(title.c_str(), text, "OK")->show();
}

bool confirmationsOn() { return Mod::get()->getSettingValue<bool>("confirmations"); }

std::string baseName(fs::path const& p) { return p.filename().string(); }

std::string suffixOf(std::string const& name) {
    for (auto s : {std::string(".fpm.json"), std::string(".gdr.json")})
        if (name.size() > s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0) return s;
    return ".fpm.json";
}

std::string sanitize(std::string const& in) {
    std::string out;
    for (char c : in) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            out += c;
        else if (c == ' ') out += '_';
    }
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    return out.substr(0, 80);
}

class FPMenu : public CCLayerColor {
public:
    enum class Tab { Macros, Recorder, Playback, Analysis, Settings };

    static FPMenu* create() {
        auto* ret = new FPMenu();
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }

    ~FPMenu() override {
        if (m_analysis) m_analysis->cancel = true;
    }

    bool init() override {
        if (!CCLayerColor::initWithColor({10, 10, 12, 250})) return false;
        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        this->schedule(schedule_selector(FPMenu::tick), 0.15f);
        m_files = session::listMacros();
        if (!session::selected().empty()) {
            auto it = std::find(m_files.begin(), m_files.end(), session::selected());
            if (it != m_files.end()) m_idx = static_cast<std::size_t>(it - m_files.begin());
        }
        rebuild();
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -500, true);
    }
    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }
    void keyBackClicked() override { this->close(); }
    void close() { this->removeFromParentAndCleanup(true); }

private:
    Tab m_tab = Tab::Recorder;
    CCNode* m_root = nullptr;
    CCMenu* m_menu = nullptr;
    CCLabelBMFont* m_progressLabel = nullptr;
    TextInput* m_rename = nullptr;
    std::vector<fs::path> m_files;
    std::size_t m_idx = 0;
    std::shared_ptr<AnalysisState> m_analysis;
    float m_s = 1.f;       // scale factor derived from screen height
    float m_left = 0, m_right = 0, m_y = 0, m_rowH = 0;

    // ---- layout helpers (all relative to the window size) ----
    CCLabelBMFont* label(std::string const& text, float y, float baseScale, ccColor3B color = {255, 255, 255},
                         char const* font = "chatFont.fnt") {
        auto* l = CCLabelBMFont::create(text.c_str(), font);
        l->setColor(color);
        float maxW = m_right - m_left;
        float sc = baseScale * m_s;
        if (l->getContentSize().width * sc > maxW) sc = maxW / l->getContentSize().width;
        l->setScale(sc);
        l->setPosition({(m_left + m_right) / 2, y});
        m_root->addChild(l);
        return l;
    }

    CCMenuItemSpriteExtra* button(std::string const& text, std::function<void()> cb) {
        auto* spr = ButtonSprite::create(text.c_str());
        auto* item = CCMenuItemExt::createSpriteExtra(spr, [cb = std::move(cb)](CCMenuItemSpriteExtra*) { cb(); });
        item->setScale(0.7f * m_s);
        item->m_baseScale = 0.7f * m_s;
        return item;
    }

    void row(std::vector<CCMenuItemSpriteExtra*> items, float y) {
        float gap = 8.f * m_s, total = gap * (items.size() - 1);
        for (auto* i : items) total += i->getContentSize().width * i->getScale();
        float usable = m_right - m_left, k = total > usable ? usable / total : 1.f;
        float x = (m_left + m_right) / 2 - total * k / 2;
        for (auto* i : items) {
            i->setScale(i->getScale() * k);
            i->m_baseScale = i->getScale();
            float w = i->getContentSize().width * i->getScale();
            i->setPosition({x + w / 2, y});
            m_menu->addChild(i);
            x += w + gap * k;
        }
    }

    void next(float& y, float rows = 1.f) { y -= m_rowH * rows; }

    void rebuild() {
        if (m_root) m_root->removeFromParentAndCleanup(true);
        m_root = CCNode::create();
        this->addChild(m_root);
        m_rename = nullptr;
        m_progressLabel = nullptr;

        auto win = CCDirector::get()->getWinSize();
        m_s = std::clamp(win.height / 320.f, 0.8f, 1.6f);
        float margin = std::max(win.width * 0.07f, 24.f); // keeps clear of notches / rounded corners
        m_left = margin;
        m_right = win.width - margin;
        m_rowH = 36.f * m_s;
        float y = win.height - std::max(win.height * 0.07f, 16.f);

        m_menu = CCMenu::create();
        m_menu->setPosition({0, 0});
        m_root->addChild(m_menu, 5);

        label("FRAME PREVIEW", y, 0.9f, Orange, "goldFont.fnt");
        next(y, 0.9f);

        auto tabBtn = [&](char const* name, Tab t) {
            auto* b = button(name, [this, t] { m_tab = t; rebuild(); });
            if (t == m_tab) static_cast<ButtonSprite*>(b->getNormalImage())->setColor(Orange);
            return b;
        };
        row({tabBtn("Macros", Tab::Macros), tabBtn("Recorder", Tab::Recorder), tabBtn("Playback", Tab::Playback),
             tabBtn("Analysis", Tab::Analysis), tabBtn("Settings", Tab::Settings)}, y);
        next(y);
        label(session::statusText(), y, 0.5f, Orange);
        next(y);

        switch (m_tab) {
            case Tab::Macros: buildMacros(y); break;
            case Tab::Recorder: buildRecorder(y); break;
            case Tab::Playback: buildPlayback(y); break;
            case Tab::Analysis: buildAnalysis(y); break;
            case Tab::Settings: buildSettings(y); break;
        }

        row({button("Close", [this] { this->close(); })}, win.height * 0.06f);
        handleTouchPriority(this);
    }

    void tick(float) {
        if (!m_analysis) return;
        if (m_progressLabel && !m_analysis->done)
            m_progressLabel->setString(fmt::format("Analyzing... {}%", static_cast<int>(m_analysis->progress * 100)).c_str());
        if (m_analysis->done) {
            auto a = m_analysis;
            m_analysis.reset();
            if (a->stats.cancelled) { g_lastStatsSummary = "Analysis cancelled"; }
            else {
                g_lastStatsText = fp::formatStats(a->stats, a->tps);
                g_lastStatsSummary = fmt::format("{} inputs, peak {:.0f}/s, min gap {} ticks", a->stats.events, a->stats.peakCps, a->stats.minInterval);
            }
            rebuild();
        }
    }

    fs::path currentFile() const { return m_idx < m_files.size() ? m_files[m_idx] : fs::path{}; }

    // ---------------- Macros ----------------
    void buildMacros(float y) {
        m_files = session::listMacros();
        if (m_idx >= m_files.size()) m_idx = m_files.empty() ? 0 : m_files.size() - 1;
        if (m_files.empty()) label("No macros yet. Record one or import a file.", y, 0.55f);
        else label(fmt::format("{}/{}  {}{}", m_idx + 1, m_files.size(), baseName(currentFile()),
                               currentFile() == session::selected() ? "  [selected]" : ""), y, 0.55f);
        next(y);
        if (auto const* m = session::selectedMacro(); m && session::selected() == currentFile()) {
            label(fmt::format("{}  |  {} inputs  |  {} ticks  |  level {}  |  {}",
                              m->complete ? "COMPLETE" : "PARTIAL", m->inputs.size(), m->durationFrames,
                              m->info.levelId, m->sourceFormat), y, 0.45f, {200, 200, 200});
        }
        next(y);
        row({button("Prev", [this] { if (m_idx > 0) --m_idx; rebuild(); }),
             button("Next", [this] { if (m_idx + 1 < m_files.size()) ++m_idx; rebuild(); }),
             button("Select", [this] { selectCurrent(); })}, y);
        next(y);
        row({button("Import", [this] { importMacro(); }),
             button("Export", [this] { exportMacro(); }),
             button("Delete", [this] { deleteMacro(); }),
             button("Complete/Partial", [this] { toggleComplete(); })}, y);
        next(y);
        m_rename = TextInput::create(std::max((m_right - m_left) * 0.4f, 120.f), "New name");
        m_rename->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_. ");
        m_rename->setMaxCharCount(60);
        m_rename->setScale(m_s);
        m_rename->setPosition({m_left + (m_right - m_left) * 0.3f, y});
        m_root->addChild(m_rename);
        auto* rn = button("Rename", [this] { renameMacro(); });
        rn->setPosition({m_left + (m_right - m_left) * 0.75f, y});
        m_menu->addChild(rn);
    }

    void selectCurrent() {
        auto f = currentFile();
        if (f.empty()) return;
        auto r = fp::loadMacroFile(f);
        if (r.status != fp::LoadStatus::Ok) { alert("Cannot open macro", r.message); return; }
        session::select(f);
        rebuild();
    }

    void importMacro() {
        auto self = Ref<FPMenu>(this);
        picker::pickMacroToImport([self](fs::path p) {
            auto r = fp::loadMacroFile(p);
            switch (r.status) {
                case fp::LoadStatus::Ok: break;
                case fp::LoadStatus::BinaryNotSupported:
                    alert("Format not supported", r.message); return;
                case fp::LoadStatus::UnsupportedFormat:
                    alert("Unknown format", r.message); return;
                default:
                    alert("File is damaged or unreadable", r.message); return;
            }
            auto stem = sanitize(p.filename().string());
            for (auto s : {std::string(".fpm.json"), std::string(".gdr.json"), std::string(".json")})
                if (stem.size() > s.size() && stem.compare(stem.size() - s.size(), s.size(), s) == 0) { stem.resize(stem.size() - s.size()); break; }
            if (stem.empty()) stem = "macro";
            auto dir = session::macrosDir();
            auto target = dir / ("imported-" + stem + ".fpm.json");
            for (int n = 2; fs::exists(target); ++n) target = dir / fmt::format("imported-{}-{}.fpm.json", stem, n);
            if (!fp::writeMacroFile(target, r.macro)) { alert("Import failed", "Could not write the macro into the mod folder."); return; }
            session::select(target);
            if (self->getParent()) { self->m_files = session::listMacros(); self->rebuild(); }
        });
    }

    void exportMacro() {
        auto f = currentFile();
        if (f.empty()) return;
        picker::pickExportTarget(baseName(f), [f](fs::path target) {
            std::error_code ec;
            fs::copy_file(f, target, fs::copy_options::overwrite_existing, ec);
            if (ec) alert("Export failed", ec.message());
            else Notification::create("Macro exported", NotificationIcon::Success)->show();
        });
    }

    void deleteMacro() {
        auto f = currentFile();
        if (f.empty()) return;
        createQuickPopup("Delete macro", "Delete " + baseName(f) + "? This cannot be undone.", "Cancel", "Delete",
            [this, f](FLAlertLayer*, bool yes) {
                if (!yes) return;
                std::error_code ec;
                fs::remove(f, ec);
                if (ec) { alert("Delete failed", ec.message()); return; }
                if (session::selected() == f) session::select({});
                rebuild();
            });
    }

    void renameMacro() {
        auto f = currentFile();
        if (f.empty() || !m_rename) return;
        auto name = sanitize(m_rename->getString());
        if (name.empty()) { alert("Rename", "Enter a name using letters, digits, '-', '_' or '.'."); return; }
        auto suffix = suffixOf(baseName(f));
        for (auto s : {std::string(".fpm.json"), std::string(".gdr.json")})
            if (name.size() > s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0) { name.resize(name.size() - s.size()); break; }
        auto target = f.parent_path() / (name + suffix);
        if (fs::exists(target)) { alert("Rename", "A macro with that name already exists."); return; }
        std::error_code ec;
        fs::rename(f, target, ec);
        if (ec) { alert("Rename failed", ec.message()); return; }
        if (session::selected() == f) session::select(target);
        m_files = session::listMacros();
        auto it = std::find(m_files.begin(), m_files.end(), target);
        if (it != m_files.end()) m_idx = static_cast<std::size_t>(it - m_files.begin());
        rebuild();
    }

    void toggleComplete() {
        auto f = currentFile();
        if (f.empty()) return;
        auto r = fp::loadMacroFile(f);
        if (r.status != fp::LoadStatus::Ok) { alert("Cannot open macro", r.message); return; }
        if (r.macro.sourceFormat != "fpm") { alert("Not supported", "Only .fpm.json macros carry a Complete/Partial flag."); return; }
        r.macro.complete = !r.macro.complete;
        if (!fp::writeMacroFile(f, r.macro)) { alert("Write failed", "Could not update the macro file."); return; }
        if (session::selected() == f) session::select(f);
        rebuild();
    }

    // ---------------- Recorder ----------------
    void buildRecorder(float y) {
        auto* pl = PlayLayer::get();
        if (!pl) { label("Open a level and pause it to use the recorder.", y, 0.6f); return; }
        label(fmt::format("Practice Mode: {}   Frame: {}", pl->m_isPracticeMode ? "ON" : "OFF",
                          session::recorder().currentFrame()), y, 0.55f);
        next(y);
        row({button("Record", [this] {
                 auto err = session::startRecording();
                 if (!err.empty()) { alert("Cannot record", err); return; }
                 Notification::create("Armed. Resume the game: recording starts in Practice Mode.", NotificationIcon::Info)->show();
                 this->close();
             }),
             button("Stop & Save", [this] {
                 std::string out;
                 session::stopAndSavePartial(out);
                 alert("Recorder", out + (out.rfind("Saved", 0) == 0 ? "\n\nMarked PARTIAL (complete=false)." : ""));
                 rebuild();
             })}, y);
        next(y);
        row({button("Pause rec", [this] {
                 if (session::recorder().state() == fp::SessionState::Recording) {
                     session::restoreSpeed();
                     session::recorder().interrupt();
                 }
                 rebuild();
             }),
             button("Resume", [this] {
                 auto err = session::resumeRecording();
                 if (!err.empty()) alert("Resume", err); else this->close();
             }),
             button("Discard", [this] {
                 auto doIt = [this] { session::discardRecording(); rebuild(); };
                 if (!confirmationsOn()) { doIt(); return; }
                 createQuickPopup("Discard recording", "Delete the unsaved recording?", "Cancel", "Discard",
                     [doIt](FLAlertLayer*, bool yes) { if (yes) doIt(); });
             })}, y);
        next(y);
        auto sp = session::speedMultiplier();
        bool on = Mod::get()->getSettingValue<bool>("speedhack-enabled");
        label(fmt::format("Speed while recording: {} x{:.2f}", on ? "ON" : "OFF", sp), y, 0.55f);
        next(y);
        row({button("-0.05", [this] { stepSpeed(-0.05); }),
             button(on ? "Speed: ON" : "Speed: OFF", [this] { toggleBool("speedhack-enabled"); }),
             button("+0.05", [this] { stepSpeed(0.05); })}, y);
    }

    void stepSpeed(double d) {
        double v = std::clamp(static_cast<double>(session::speedMultiplier()) + d, 0.10, 3.00);
        Mod::get()->setSettingValue<double>("speed-multiplier", std::round(v * 100.0) / 100.0);
        rebuild();
    }

    void toggleBool(char const* key) {
        Mod::get()->setSettingValue<bool>(key, !Mod::get()->getSettingValue<bool>(key));
        rebuild();
    }

    // ---------------- Playback ----------------
    void buildPlayback(float y) {
        auto const* m = session::selectedMacro();
        if (!m) label("No macro selected (Macros tab -> Select).", y, 0.55f);
        else label(fmt::format("{}  ({} inputs, level {})", baseName(session::selected()), m->inputs.size(), m->info.levelId), y, 0.5f);
        next(y);
        if (!PlayLayer::get()) { label("Open a level to play a macro.", y, 0.55f); return; }
        row({button("Play", [this] { play(); }),
             button("Pause", [this] { session::playback().pause(true); rebuild(); }),
             button("Stop", [this] { session::stopPlayback(); rebuild(); }),
             button("Restart", [this] { restartPlayback(); })}, y);
    }

    void play() {
        auto& pb = session::playback();
        if (pb.active() && pb.paused()) { pb.pause(false); this->close(); return; }
        auto const* m = session::selectedMacro();
        if (!m) { alert("Playback", "Select a macro first."); return; }
        launch(*m);
    }

    void restartPlayback() {
        auto const* m = session::selectedMacro();
        if (!m) { alert("Playback", "Select a macro first."); return; }
        session::stopPlayback();
        launch(*m);
    }

    void launch(fp::Macro macro) {
        auto* pl = PlayLayer::get();
        auto id = pl && pl->m_level ? static_cast<std::uint32_t>(pl->m_level->m_levelID.value()) : 0u;
        auto start = [this, macro]() {
            auto err = session::startPlayback(macro);
            if (!err.empty()) { alert("Playback", err); return; }
            Notification::create("Resume the game to start playback.", NotificationIcon::Info)->show();
            this->close();
        };
        if (macro.info.levelId != 0 && macro.info.levelId != id) {
            createQuickPopup("Level mismatch",
                fmt::format("This macro was made for level {}, but the current level is {}. Play anyway?", macro.info.levelId, id),
                "Cancel", "Play anyway", [start](FLAlertLayer*, bool yes) { if (yes) start(); });
            return;
        }
        start();
    }

    // ---------------- Analysis ----------------
    void buildAnalysis(float y) {
        auto const* m = session::selectedMacro();
        if (!m) { label("No macro selected (Macros tab -> Select).", y, 0.55f); return; }
        label(baseName(session::selected()), y, 0.5f);
        next(y);
        row({button("Analyze", [this] { startAnalysis(*session::selectedMacro()); }),
             button("Cancel", [this] { if (m_analysis) m_analysis->cancel = true; }),
             button("Full stats", [this] {
                 if (g_lastStatsText.empty()) alert("Analysis", "Run the analysis first.");
                 else alert("Frame window analysis", g_lastStatsText);
             })}, y);
        next(y);
        m_progressLabel = label(m_analysis ? "Analyzing..." : (g_lastStatsSummary.empty() ? "Not analyzed yet" : g_lastStatsSummary),
                                y, 0.5f, {200, 200, 200});
        next(y);
        auto n = m->inputs.size();
        auto sel = std::min(session::selectedEvent(), n ? n - 1 : 0);
        if (n) {
            auto const& e = m->inputs[sel];
            label(fmt::format("Event {}/{}: tick {}  P{}  btn {}  {}", sel + 1, n, e.frame, e.player2 ? 2 : 1, e.button,
                              e.down ? "press" : "release"), y, 0.5f);
        }
        next(y);
        row({button("<<", [this, n] { jump(-10, n); }), button("Prev", [this, n] { jump(-1, n); }),
             button("Next", [this, n] { jump(1, n); }), button(">>", [this, n] { jump(10, n); })}, y);
    }

    void jump(long d, std::size_t n) {
        if (!n) return;
        long v = std::clamp(static_cast<long>(session::selectedEvent()) + d, 0L, static_cast<long>(n) - 1);
        session::setSelectedEvent(static_cast<std::size_t>(v));
        rebuild();
    }

    void startAnalysis(fp::Macro macro) {
        if (m_analysis) return;
        auto st = std::make_shared<AnalysisState>();
        st->tps = macro.info.tps;
        m_analysis = st;
        // Pure data processing off the game thread; no game API is touched here.
        std::thread([st, macro = std::move(macro)]() {
            st->stats = fp::analyze(macro, st->cancel, [st](float p) { st->progress = p; });
            st->done = true;
        }).detach();
        rebuild();
    }

    // ---------------- Settings ----------------
    void buildSettings(float y) {
        auto* mod = Mod::get();
        auto onOff = [](bool b) { return b ? "ON" : "OFF"; };
        row({button(fmt::format("HUD: {}", onOff(mod->getSettingValue<bool>("show-hud"))), [this] { toggleBool("show-hud"); }),
             button(fmt::format("Markers: {}", onOff(mod->getSettingValue<bool>("show-markers"))), [this] { toggleBool("show-markers"); }),
             button(fmt::format("Confirm: {}", onOff(mod->getSettingValue<bool>("confirmations"))), [this] { toggleBool("confirmations"); })}, y);
        next(y);
        auto tps = mod->getSettingValue<int64_t>("tps");
        row({button("TPS -10", [this, tps] { setTps(tps - 10); }), button(fmt::format("TPS {}", tps), [] {}),
             button("TPS +10", [this, tps] { setTps(tps + 10); })}, y);
        next(y);
        auto filter = mod->getSettingValue<std::string>("player-filter");
        row({button("Players: " + filter, [this, filter, mod] {
                 mod->setSettingValue<std::string>("player-filter", filter == "Both" ? "Player 1" : filter == "Player 1" ? "Player 2" : "Both");
                 rebuild();
             })}, y);
        next(y);
        auto size = mod->getSettingValue<double>("marker-size");
        row({button("Marker -", [this, size] { setMarker(size - 0.5); }), button(fmt::format("Size {:.1f}", size), [] {}),
             button("Marker +", [this, size] { setMarker(size + 0.5); }),
             button("Color", [this] { cycleColor(); })}, y);
    }

    void setTps(int64_t v) {
        Mod::get()->setSettingValue<int64_t>("tps", std::clamp<int64_t>(v, 30, 1000));
        rebuild();
    }
    void setMarker(double v) {
        Mod::get()->setSettingValue<double>("marker-size", std::clamp(v, 1.0, 6.0));
        rebuild();
    }
    void cycleColor() {
        static constexpr ccColor3B palette[] = {{255, 140, 0}, {0, 200, 255}, {80, 220, 100}, {255, 255, 255}, {240, 70, 70}};
        auto cur = Mod::get()->getSettingValue<ccColor3B>("marker-color");
        std::size_t next = 0;
        for (std::size_t i = 0; i < std::size(palette); ++i)
            if (palette[i].r == cur.r && palette[i].g == cur.g && palette[i].b == cur.b) next = (i + 1) % std::size(palette);
        Mod::get()->setSettingValue<ccColor3B>("marker-color", palette[next]);
        session::refreshOverlay();
        rebuild();
    }
};

} // namespace

void openMenu() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    if (auto* menu = FPMenu::create()) scene->addChild(menu, 99999);
}

void addPauseButton(CCLayer* pauseLayer) {
    auto win = CCDirector::get()->getWinSize();
    auto* spr = ButtonSprite::create("Frame Preview");
    auto* item = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) { openMenu(); });
    float s = std::clamp(win.height / 320.f, 0.8f, 1.6f) * 0.6f;
    item->setScale(s);
    item->m_baseScale = s;
    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    // Bottom-left corner, offset by a relative margin so it clears rounded corners / notches.
    item->setPosition({std::max(win.width * 0.07f, 24.f) + item->getContentSize().width * s / 2, win.height * 0.08f});
    menu->addChild(item);
    pauseLayer->addChild(menu, 100);
}

} // namespace fp::ui

#include "../src/Analyzer.hpp"
#include "../src/MacroFormat.hpp"
#include "../src/Playback.hpp"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace fp;
namespace fs = std::filesystem;

static void write(fs::path const& p, std::string const& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

int main() {
    auto root = fs::temp_directory_path() / "fp-core-tests";
    fs::remove_all(root);

    // round-trip .fpm.json
    Macro m;
    m.info = {77, "Lvl \"x\"", 240.f, 0.5f};
    m.complete = true;
    m.inputs = {{0, 1, true, false}, {10, 1, false, false}, {12, 1, true, true}, {13, 1, false, true}, {14, 1, true, false}};
    m.durationFrames = 100;
    assert(writeMacroFile(root / "a.fpm.json", m));
    auto r = loadMacroFile(root / "a.fpm.json");
    assert(r.status == LoadStatus::Ok);
    assert(r.macro.inputs.size() == 5 && r.macro.complete && r.macro.info.levelId == 77);
    assert(r.macro.info.levelName == "Lvl \"x\"");

    // GDR JSON
    write(root / "b.gdr.json", R"({"framerate":240,"level":{"id":5,"name":"N"},"bot":{"name":"x","version":1},
      "inputs":[{"frame":3,"btn":1,"2p":false,"down":true},{"frame":9,"btn":1,"2p":false,"down":false}]})");
    r = loadMacroFile(root / "b.gdr.json");
    assert(r.status == LoadStatus::Ok && r.macro.sourceFormat == "gdr-json" && !r.macro.complete);
    assert(r.macro.info.levelId == 5 && r.macro.durationFrames == 9);

    // corrupt / invalid
    write(root / "c.json", "{\"framerate\":240,\"inputs\":[{\"frame\":1,");
    assert(loadMacroFile(root / "c.json").status == LoadStatus::Corrupt);
    write(root / "d.json", R"({"framerate":240,"inputs":[{"frame":5,"btn":1,"down":true},{"frame":2,"btn":1,"down":false}]})");
    assert(loadMacroFile(root / "d.json").status == LoadStatus::Corrupt);
    write(root / "e.json", R"({"framerate":0,"inputs":[]})");
    assert(loadMacroFile(root / "e.json").status == LoadStatus::Corrupt);
    write(root / "f.json", R"({"foo":1})");
    assert(loadMacroFile(root / "f.json").status == LoadStatus::UnsupportedFormat);
    write(root / "g.gdr", std::string("\x93\x01\x02", 3));
    assert(loadMacroFile(root / "g.gdr").status == LoadStatus::BinaryNotSupported);
    write(root / "h.gdr2", std::string("GDR2\x01", 5));
    assert(loadMacroFile(root / "h.gdr2").status == LoadStatus::BinaryNotSupported);
    write(root / "i.fpm.json", R"({"format":"frame-preview-macro","schema":1,"tps":240,"eventCount":3,"inputs":[]})");
    assert(loadMacroFile(root / "i.fpm.json").status == LoadStatus::Corrupt);
    assert(loadMacroFile(root / "missing.json").status == LoadStatus::Unreadable);

    // playback
    Playback pb;
    assert(!pb.start(Macro{}));
    assert(pb.start(m));
    std::vector<InputEvent> fired;
    for (int f = 0; f < 15; ++f) pb.step([&](InputEvent const& e) { fired.push_back(e); });
    assert(fired.size() == 5 && fired[2].frame == 12 && fired[2].player2);
    pb.rewind();
    fired.clear();
    for (int f = 0; f < 11; ++f) pb.step([&](InputEvent const& e) { fired.push_back(e); }); // P1 pressed again? no: ends at frame 10
    pb.step({});
    fired.clear();
    pb.rewind();
    for (int f = 0; f < 15; ++f) pb.step({}); // last event: P1 press at 14 stays held
    pb.releaseAll([&](InputEvent const& e) { fired.push_back(e); });
    assert(fired.size() == 1 && !fired[0].down && !fired[0].player2);
    pb.stop();
    assert(!pb.active());

    // analyzer
    std::atomic<bool> cancel{false};
    auto s = analyze(m, cancel);
    assert(s.presses == 3 && s.releases == 2 && s.p2 == 2);
    assert(s.intervalCount == 1 && s.minInterval == 14); // P1 presses at 0 and 14
    assert(s.holdCount == 2 && s.minHold == 1 && s.maxHold == 10);
    cancel = true;
    assert(analyze(m, cancel).cancelled);
    std::cout << formatStats(s, 240.f) << "\nOK core\n";
    fs::remove_all(root);
    return 0;
}

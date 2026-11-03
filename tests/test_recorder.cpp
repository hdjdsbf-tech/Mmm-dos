#include "../src/MacroRecorder.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main() {
    auto root = std::filesystem::temp_directory_path() / "frame-preview-recorder-tests";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    fp::MacroRecorder recorder;
    fp::MacroInfo info{123, "level \"one\"\nline", 240.f, 1.f};
    auto pending = root / "pending.fpm.json";
    assert(recorder.begin(info, pending));
    assert(recorder.state() == fp::SessionState::Recording);
    assert(std::filesystem::exists(pending));

    assert(recorder.captureInput(1, true, false));
    assert(recorder.onGameFrame());
    assert(recorder.captureInput(1, false, false));
    assert(recorder.inputCount() == 2);
    assert(recorder.inputs()[0].frame == 0);
    assert(recorder.inputs()[1].frame == 1);
    assert(!recorder.captureInput(0, true, false));
    assert(recorder.lastError().size() > 0);

    auto partial = root / "macros" / "partial.fpm.json";
    assert(recorder.savePartial(partial));
    {
        std::ifstream in(partial);
        std::string json((std::istreambuf_iterator<char>(in)), {});
        assert(json.find("\"partial\": true") != std::string::npos);
        assert(json.find("\"complete\": false") != std::string::npos);
        assert(json.find("\"eventCount\": 2") != std::string::npos);
        assert(json.find("level \\\"one\\\"\\nline") != std::string::npos);
    }

    assert(recorder.interrupt());
    assert(recorder.state() == fp::SessionState::Interrupted);
    assert(recorder.resume());
    assert(recorder.onGameFrame());
    auto complete = root / "macros" / "complete.fpm.json";
    assert(recorder.complete(complete));
    assert(recorder.state() == fp::SessionState::Completed);
    assert(std::filesystem::exists(complete));
    assert(!std::filesystem::exists(pending));

    fp::MacroRecorder invalid;
    auto badInfo = info;
    badInfo.tps = 0.f;
    assert(!invalid.begin(badInfo, root / "invalid.json"));
    assert(invalid.state() == fp::SessionState::Idle);

    std::filesystem::remove_all(root, ec);
    return 0;
}

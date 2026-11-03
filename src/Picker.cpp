#include "Picker.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

// TEMPORARY FALLBACK: the Geode file-picker API of the SDK used in CI differs from
// what the first version assumed (Task<Result<path>> does not compile). Until the
// exact SDK declaration of file::pick is confirmed, import reads the newest macro
// from <mod save dir>/imports/. This is NOT the Android file picker.
namespace fp::picker {

void pickMacroToImport(std::function<void(std::filesystem::path)> onPicked) {
    auto dir = Mod::get()->getSaveDir() / "imports";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !e.is_regular_file()) continue;
        auto t = e.last_write_time(ec);
        if (newest.empty() || t > newestTime) { newest = e.path(); newestTime = t; }
    }
    if (newest.empty()) {
        FLAlertLayer::create("Import",
            "File picker is not available in this build.\nPut a macro file into:\n" + dir.string(), "OK")->show();
        return;
    }
    onPicked(newest);
}

void pickExportTarget(std::string const& suggestedName, std::function<void(std::filesystem::path)> onPicked) {
    auto dir = Mod::get()->getSaveDir() / "exports";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    onPicked(dir / suggestedName);
}

}

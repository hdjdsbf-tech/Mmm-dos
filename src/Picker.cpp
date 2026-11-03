#include "Picker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

using namespace geode::prelude;

namespace fp::picker {
namespace {
using PickTask = Task<Result<std::filesystem::path>>;
EventListener<PickTask> s_listener;

void run(file::PickMode mode, file::FilePickOptions options, std::function<void(std::filesystem::path)> cb) {
    s_listener.bind([cb = std::move(cb)](PickTask::Event* event) {
        if (auto* result = event->getValue()) {
            if (result->isOk()) cb(std::move(result->unwrap()));
        }
    });
    s_listener.setFilter(file::pick(mode, std::move(options)));
}
}

void pickMacroToImport(std::function<void(std::filesystem::path)> onPicked) {
    file::FilePickOptions options;
    options.filters.push_back({"Macros (.fpm.json, .gdr.json, .gdr, .gdr2)", {"*.json", "*.gdr", "*.gdr2"}});
    run(file::PickMode::OpenFile, std::move(options), std::move(onPicked));
}

void pickExportTarget(std::string const& suggestedName, std::function<void(std::filesystem::path)> onPicked) {
    file::FilePickOptions options;
    options.defaultPath = std::filesystem::path(suggestedName);
    options.filters.push_back({"Frame Preview macro", {"*.json"}});
    run(file::PickMode::SaveFile, std::move(options), std::move(onPicked));
}
}

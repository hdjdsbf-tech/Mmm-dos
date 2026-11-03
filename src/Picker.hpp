#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace fp::picker {
// Android-safe: uses Geode's file picker, so the user never has to copy files
// into the app's private folder by hand.
void pickMacroToImport(std::function<void(std::filesystem::path)> onPicked);
void pickExportTarget(std::string const& suggestedName, std::function<void(std::filesystem::path)> onPicked);
}

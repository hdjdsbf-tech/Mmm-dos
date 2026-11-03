#pragma once

#include "MacroRecorder.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fp {

struct Macro {
    MacroInfo info;
    bool complete = false;
    std::uint32_t durationFrames = 0;
    std::vector<InputEvent> inputs;
    std::string sourceFormat; // "fpm" or "gdr-json"
};

enum class LoadStatus { Ok, Unreadable, Corrupt, UnsupportedFormat, BinaryNotSupported };

struct LoadResult {
    LoadStatus status = LoadStatus::Corrupt;
    std::string message;
    Macro macro;
};

// Reads .fpm.json and .gdr.json. Binary .gdr / .gdr2 are detected and
// rejected with BinaryNotSupported (no fake support).
LoadResult loadMacroFile(std::filesystem::path const& path);

// Atomic write (temp file + rename) of the native .fpm.json format.
bool writeMacroFile(std::filesystem::path const& path, Macro const& macro);

std::string jsonEscape(std::string const& input);

} // namespace fp

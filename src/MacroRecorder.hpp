#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fp {

enum class SessionState { Idle, Recording, Interrupted, Completed, Error };

struct InputEvent {
    std::uint32_t frame = 0;
    int button = 1;
    bool down = false;
    bool player2 = false;
};

struct MacroInfo {
    std::uint32_t levelId = 0;
    std::string levelName;
    float tps = 240.f;
    float speedMultiplier = 1.f;
};

// Cross-platform recorder for the project's own .fpm.json format.
// It does not claim to parse GDR/GDR2.
class MacroRecorder {
public:
    static constexpr std::size_t MaxEvents = 5'000'000;

    bool begin(MacroInfo info, std::filesystem::path pendingPath);
    bool captureInput(int button, bool down, bool player2);
    bool onGameFrame();
    bool interrupt();
    bool resume();
    bool savePartial(std::filesystem::path outputPath);
    bool complete(std::filesystem::path completedPath);
    void reset();
    // Deletes the pending checkpoint file (if any) and resets the recorder.
    bool discard();

    SessionState state() const { return m_state; }
    std::uint32_t currentFrame() const { return m_frame; }
    std::size_t inputCount() const { return m_inputs.size(); }
    MacroInfo const& info() const { return m_info; }
    std::vector<InputEvent> const& inputs() const { return m_inputs; }
    std::string const& lastError() const { return m_lastError; }

private:
    bool writeJson(std::filesystem::path const& path, bool complete) const;
    bool checkpoint();
    bool fail(std::string message);

    SessionState m_state = SessionState::Idle;
    MacroInfo m_info;
    std::vector<InputEvent> m_inputs;
    std::uint32_t m_frame = 0;
    std::uint32_t m_framesSinceCheckpoint = 0;
    std::filesystem::path m_pendingPath;
    std::string m_lastError;
};

} // namespace fp

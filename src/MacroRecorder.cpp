#include "MacroRecorder.hpp"
#include "MacroFormat.hpp"

#include <cmath>
#include <fstream>
#include <system_error>
#include <utility>

namespace fp {
bool MacroRecorder::fail(std::string message) {
    m_lastError = std::move(message);
    return false;
}

bool MacroRecorder::begin(MacroInfo info, std::filesystem::path pendingPath) {
    if (m_state == SessionState::Recording) return fail("A recording is already active");
    if (pendingPath.empty()) return fail("Pending macro path is empty");
    if (!std::isfinite(info.tps) || info.tps <= 0.f)
        return fail("TPS must be a finite positive number");
    if (!std::isfinite(info.speedMultiplier) || info.speedMultiplier <= 0.f)
        return fail("Speed multiplier must be a finite positive number");

    m_info = std::move(info);
    m_pendingPath = std::move(pendingPath);
    m_inputs.clear();
    m_frame = 0;
    m_framesSinceCheckpoint = 0;
    m_lastError.clear();
    m_state = SessionState::Recording;
    if (!checkpoint()) {
        m_state = SessionState::Error;
        return false;
    }
    return true;
}

bool MacroRecorder::captureInput(int button, bool down, bool player2) {
    if (m_state != SessionState::Recording) return false;
    if (button < 1 || button > 8) return fail("Input button is outside supported range 1..8");
    if (m_inputs.size() >= MaxEvents) return fail("Macro event limit exceeded");
    m_inputs.push_back(InputEvent{m_frame, button, down, player2});
    return true;
}

bool MacroRecorder::onGameFrame() {
    if (m_state != SessionState::Recording) return false;
    if (m_frame == UINT32_MAX) return fail("Frame counter overflow");
    ++m_frame;
    if (++m_framesSinceCheckpoint >= 120) return checkpoint();
    return true;
}

bool MacroRecorder::interrupt() {
    if (m_state != SessionState::Recording) return false;
    m_state = SessionState::Interrupted;
    if (!checkpoint()) {
        m_state = SessionState::Error;
        return false;
    }
    return true;
}

bool MacroRecorder::resume() {
    if (m_state != SessionState::Interrupted) return false;
    m_lastError.clear();
    m_state = SessionState::Recording;
    return true;
}

bool MacroRecorder::savePartial(std::filesystem::path outputPath) {
    if (m_state != SessionState::Recording && m_state != SessionState::Interrupted)
        return fail("No active or interrupted recording to save");
    if (outputPath.empty()) return fail("Partial output path is empty");
    if (!writeJson(outputPath, false)) return fail("Failed to write partial macro: " + outputPath.string());
    return true;
}

bool MacroRecorder::complete(std::filesystem::path completedPath) {
    if (m_state != SessionState::Recording && m_state != SessionState::Interrupted)
        return fail("No active or interrupted recording to complete");
    if (completedPath.empty()) return fail("Completed output path is empty");
    if (!writeJson(completedPath, true)) return fail("Failed to write completed macro: " + completedPath.string());

    m_state = SessionState::Completed;
    std::error_code ec;
    if (!m_pendingPath.empty()) {
        std::filesystem::remove(m_pendingPath, ec);
        if (ec) m_lastError = "Macro saved, but pending checkpoint could not be removed: " + ec.message();
    }
    return true;
}

void MacroRecorder::reset() {
    m_state = SessionState::Idle;
    m_info = {};
    m_inputs.clear();
    m_frame = 0;
    m_framesSinceCheckpoint = 0;
    m_pendingPath.clear();
    m_lastError.clear();
}

bool MacroRecorder::discard() {
    std::error_code ec;
    bool ok = true;
    if (!m_pendingPath.empty()) {
        std::filesystem::remove(m_pendingPath, ec);
        ok = !ec;
    }
    reset();
    return ok;
}

bool MacroRecorder::checkpoint() {
    if (m_pendingPath.empty()) return fail("Pending macro path is empty");
    if (!writeJson(m_pendingPath, false)) return fail("Failed to write checkpoint: " + m_pendingPath.string());
    m_framesSinceCheckpoint = 0;
    return true;
}

bool MacroRecorder::writeJson(std::filesystem::path const& path, bool complete) const {
    Macro macro;
    macro.info = m_info;
    macro.complete = complete;
    macro.durationFrames = m_frame;
    macro.inputs = m_inputs;
    return writeMacroFile(path, macro);
}

} // namespace fp

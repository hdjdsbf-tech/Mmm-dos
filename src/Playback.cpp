#include "Playback.hpp"

namespace fp {

bool Playback::start(Macro macro) {
    if (macro.inputs.empty()) return false;
    m_macro = std::move(macro);
    m_active = true;
    m_paused = false;
    rewind();
    return true;
}

void Playback::rewind() {
    m_next = 0;
    m_frame = 0;
    m_held = {};
}

void Playback::stop() {
    m_active = false;
    m_paused = false;
    m_macro = {};
    rewind();
}

void Playback::step(Sink const& sink) {
    if (!m_active || m_paused) return;
    while (m_next < m_macro.inputs.size() && m_macro.inputs[m_next].frame <= m_frame) {
        auto const& e = m_macro.inputs[m_next++];
        m_held[e.player2 ? 1 : 0][e.button] = e.down;
        if (sink) sink(e);
    }
    ++m_frame;
}

void Playback::releaseAll(Sink const& sink) {
    for (int p = 0; p < 2; ++p) {
        for (int b = 1; b <= 8; ++b) {
            if (!m_held[p][b]) continue;
            m_held[p][b] = false;
            if (sink) sink(InputEvent{m_frame, b, false, p == 1});
        }
    }
}

} // namespace fp

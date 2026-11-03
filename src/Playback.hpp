#pragma once

#include "MacroFormat.hpp"

#include <array>
#include <functional>

namespace fp {

// Pure scheduler: decides which recorded events fire on which game tick.
// It never touches the game; the Geode glue calls step() once per tick on the
// main game thread.
class Playback {
public:
    using Sink = std::function<void(InputEvent const&)>;

    bool start(Macro macro);
    void stop();
    void pause(bool paused) { m_paused = paused; }
    void rewind();

    bool active() const { return m_active; }
    bool paused() const { return m_paused; }
    bool finished() const { return m_active && m_next >= m_macro.inputs.size() && m_frame > m_macro.durationFrames; }
    std::uint32_t frame() const { return m_frame; }
    Macro const& macro() const { return m_macro; }

    // Fire all events scheduled for the current frame, then advance the frame.
    void step(Sink const& sink);
    // Release every button still held (death, stop, error). Calls sink with down=false.
    void releaseAll(Sink const& sink);

private:
    Macro m_macro;
    std::size_t m_next = 0;
    std::uint32_t m_frame = 0;
    bool m_active = false;
    bool m_paused = false;
    std::array<std::array<bool, 9>, 2> m_held{};
};

} // namespace fp

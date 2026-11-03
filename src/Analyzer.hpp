#pragma once

#include "MacroFormat.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <string>

namespace fp {

struct Stats {
    std::size_t events = 0, presses = 0, releases = 0, p1 = 0, p2 = 0;
    std::uint32_t durationFrames = 0;
    double durationSec = 0, avgCps = 0, peakCps = 0;
    // Press-to-press intervals (same player), in ticks.
    std::size_t intervalCount = 0;
    std::uint32_t minInterval = 0, maxInterval = 0;
    double meanInterval = 0, medianInterval = 0;
    std::size_t tightIntervals = 0; // <= 3 ticks
    // Hold durations (press -> release), in ticks.
    std::size_t holdCount = 0;
    std::uint32_t minHold = 0, maxHold = 0;
    // Buckets: 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65+
    std::array<std::size_t, 8> histogram{};
    double tickMs = 0; // timing resolution of one tick
    bool cancelled = false;
};

// Runs on any thread: reads only the Macro, never the game.
Stats analyze(Macro const& macro, std::atomic<bool> const& cancel,
              std::function<void(float)> const& progress = {});

std::string formatStats(Stats const& s, float tps);

} // namespace fp

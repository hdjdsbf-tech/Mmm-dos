#include "Analyzer.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace fp {

namespace {
std::size_t bucket(std::uint32_t v) {
    if (v <= 1) return 0;
    if (v == 2) return 1;
    if (v <= 4) return 2;
    if (v <= 8) return 3;
    if (v <= 16) return 4;
    if (v <= 32) return 5;
    if (v <= 64) return 6;
    return 7;
}
}

Stats analyze(Macro const& m, std::atomic<bool> const& cancel, std::function<void(float)> const& progress) {
    Stats s;
    s.events = m.inputs.size();
    s.durationFrames = m.durationFrames;
    float tps = m.info.tps > 0.f ? m.info.tps : 240.f;
    s.durationSec = s.durationFrames / static_cast<double>(tps);
    s.tickMs = 1000.0 / tps;

    std::vector<std::uint32_t> intervals;
    std::vector<std::uint32_t> pressFrames;
    std::array<std::array<std::int64_t, 9>, 2> lastPress;
    std::array<std::array<std::int64_t, 9>, 2> downAt;
    for (auto& a : lastPress) a.fill(-1);
    for (auto& a : downAt) a.fill(-1);
    std::array<std::int64_t, 2> lastAnyPress{-1, -1};

    for (std::size_t i = 0; i < m.inputs.size(); ++i) {
        if ((i & 0xFFF) == 0) {
            if (cancel.load()) { s.cancelled = true; return s; }
            if (progress) progress(static_cast<float>(i) / static_cast<float>(m.inputs.size()));
        }
        auto const& e = m.inputs[i];
        int p = e.player2 ? 1 : 0;
        (e.player2 ? s.p2 : s.p1)++;
        if (e.down) {
            ++s.presses;
            pressFrames.push_back(e.frame);
            if (lastAnyPress[p] >= 0) {
                auto d = static_cast<std::uint32_t>(e.frame - lastAnyPress[p]);
                intervals.push_back(d);
                ++s.histogram[bucket(d)];
                if (d <= 3) ++s.tightIntervals;
            }
            lastAnyPress[p] = e.frame;
            lastPress[p][e.button] = e.frame;
            downAt[p][e.button] = e.frame;
        } else {
            ++s.releases;
            if (downAt[p][e.button] >= 0) {
                auto hold = static_cast<std::uint32_t>(e.frame - downAt[p][e.button]);
                if (s.holdCount == 0) s.minHold = s.maxHold = hold;
                s.minHold = std::min(s.minHold, hold);
                s.maxHold = std::max(s.maxHold, hold);
                ++s.holdCount;
                downAt[p][e.button] = -1;
            }
        }
    }

    if (s.durationSec > 0) s.avgCps = s.presses / s.durationSec;

    // Peak inputs-per-second: sliding window of one second (= tps ticks).
    std::size_t lo = 0;
    std::size_t best = 0;
    auto window = static_cast<std::uint32_t>(tps);
    for (std::size_t hi = 0; hi < pressFrames.size(); ++hi) {
        while (pressFrames[hi] - pressFrames[lo] >= window) ++lo;
        best = std::max(best, hi - lo + 1);
    }
    s.peakCps = static_cast<double>(best);

    s.intervalCount = intervals.size();
    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());
        s.minInterval = intervals.front();
        s.maxInterval = intervals.back();
        double sum = 0;
        for (auto v : intervals) sum += v;
        s.meanInterval = sum / intervals.size();
        auto n = intervals.size();
        s.medianInterval = (n % 2) ? intervals[n / 2] : (intervals[n / 2 - 1] + intervals[n / 2]) / 2.0;
    }
    if (progress) progress(1.f);
    return s;
}

std::string formatStats(Stats const& s, float tps) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "Events: %zu (press %zu / release %zu)\nP1: %zu  P2: %zu\n"
        "Duration: %u ticks (%.2f s) @ %.0f TPS\nTick = %.3f ms\n"
        "Avg inputs/s: %.2f  Peak: %.0f\n"
        "Press gaps: min %u  max %u  mean %.1f  median %.1f ticks\n"
        "Gaps <= 3 ticks: %zu of %zu\n"
        "Holds: %zu (min %u, max %u ticks)\n"
        "Gap histogram [1|2|3-4|5-8|9-16|17-32|33-64|65+]:\n%zu | %zu | %zu | %zu | %zu | %zu | %zu | %zu",
        s.events, s.presses, s.releases, s.p1, s.p2,
        s.durationFrames, s.durationSec, tps, s.tickMs,
        s.avgCps, s.peakCps,
        s.minInterval, s.maxInterval, s.meanInterval, s.medianInterval,
        s.tightIntervals, s.intervalCount,
        s.holdCount, s.minHold, s.maxHold,
        s.histogram[0], s.histogram[1], s.histogram[2], s.histogram[3],
        s.histogram[4], s.histogram[5], s.histogram[6], s.histogram[7]);
    return buf;
}

} // namespace fp

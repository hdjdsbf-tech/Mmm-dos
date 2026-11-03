#include "Overlay.hpp"

#include "Session.hpp"

#include <algorithm>
#include <cstdint>

using namespace geode::prelude;

namespace fp::session {

namespace {
constexpr auto HudId = "fp-hud";
constexpr auto TimelineId = "fp-timeline";
std::size_t g_lastMarkerKey = SIZE_MAX;
}

void createOverlay() {
    auto* pl = PlayLayer::get();
    if (!pl || !pl->m_uiLayer) return;
    g_lastMarkerKey = SIZE_MAX;
    if (!pl->m_uiLayer->getChildByID(HudId)) {
        auto* label = CCLabelBMFont::create("", "chatFont.fnt");
        label->setID(HudId);
        label->setAnchorPoint({0.f, 1.f});
        label->setScale(0.6f);
        label->setOpacity(200);
        label->setColor({255, 140, 0});
        auto win = CCDirector::get()->getWinSize();
        label->setPosition({win.width * 0.03f, win.height * 0.97f});
        pl->m_uiLayer->addChild(label, 100);
    }
    if (!pl->m_uiLayer->getChildByID(TimelineId)) {
        auto* node = CCDrawNode::create();
        node->setID(TimelineId);
        pl->m_uiLayer->addChild(node, 99);
    }
}

void refreshOverlay() {
    auto* pl = PlayLayer::get();
    if (!pl || !pl->m_uiLayer) return;
    auto* label = typeinfo_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByID(HudId));
    auto* draw = typeinfo_cast<CCDrawNode*>(pl->m_uiLayer->getChildByID(TimelineId));
    auto const status = statusText();
    bool busy = recorder().state() == SessionState::Recording || playback().active() || armed();
    if (label) {
        label->setVisible(Mod::get()->getSettingValue<bool>("show-hud") && busy);
        if (label->isVisible()) label->setString(status.c_str());
    }
    if (!draw) return;

    // Timeline markers for the selected macro (cheap: redraw only when inputs change).
    auto const* macro = selectedMacro();
    bool show = macro && Mod::get()->getSettingValue<bool>("show-markers");
    std::size_t key = show ? (macro->inputs.size() * 31 + selectedEvent() + 1) : 0;
    if (key == g_lastMarkerKey && !playback().active()) return;
    g_lastMarkerKey = key;
    draw->clear();
    if (!show || macro->inputs.empty()) return;

    auto win = CCDirector::get()->getWinSize();
    float x0 = win.width * 0.08f, x1 = win.width * 0.92f, y = win.height * 0.04f;
    float dur = static_cast<float>(std::max<std::uint32_t>(macro->durationFrames, 1));
    auto c = Mod::get()->getSettingValue<ccColor3B>("marker-color");
    float size = static_cast<float>(Mod::get()->getSettingValue<double>("marker-size"));
    ccColor4F col{c.r / 255.f, c.g / 255.f, c.b / 255.f, 0.9f};
    ccColor4F dim{1.f, 1.f, 1.f, 0.25f};
    draw->drawSegment({x0, y}, {x1, y}, 0.5f, dim);
    // Draw at most ~600 markers so huge macros stay cheap.
    std::size_t stride = std::max<std::size_t>(1, macro->inputs.size() / 600);
    for (std::size_t i = 0; i < macro->inputs.size(); i += stride) {
        auto const& e = macro->inputs[i];
        if (!e.down) continue;
        float x = x0 + (x1 - x0) * (e.frame / dur);
        draw->drawSegment({x, y - size * 2}, {x, y + size * 2}, size * 0.25f, col);
    }
    if (selectedEvent() < macro->inputs.size()) {
        auto const& e = macro->inputs[selectedEvent()];
        float x = x0 + (x1 - x0) * (e.frame / dur);
        draw->drawSegment({x, y - size * 4}, {x, y + size * 4}, size * 0.5f, {1.f, 1.f, 1.f, 1.f});
    }
    if (playback().active()) {
        float x = x0 + (x1 - x0) * (playback().frame() / dur);
        draw->drawSegment({x, y - size * 3}, {x, y + size * 3}, size * 0.3f, {0.3f, 0.8f, 1.f, 1.f});
    }
}

} // namespace fp::session

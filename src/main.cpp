#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Session.hpp"
#include "UI.hpp"

using namespace geode::prelude;


class $modify(FPBaseLayer, GJBaseGameLayer) {
    // Observe only: the original method is always called, input is never altered.
    void handleButton(bool down, int button, bool isPlayer1) {
        fp::session::onButton(down, button, isPlayer1);
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Called once per physics tick (not per player), so Dual Mode doesn't double-count.
    // NOTE: signature taken from the SDK compile error; half ticks are not counted as frames.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        fp::session::onStep(this, dt, true, !isHalfTick);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        fp::session::onStep(this, dt, false, !isHalfTick);
    }
};

class $modify(FPPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        fp::session::createOverlay();
        fp::session::onLevelEntered(this);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        fp::session::onReset();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        fp::session::onDeath();
    }

    void onQuit() {
        fp::session::onQuit();
        PlayLayer::onQuit();
    }

    void levelComplete() {
        fp::session::onLevelComplete();
        PlayLayer::levelComplete();
    }
};

class $modify(FPPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        fp::session::onPauseOpened();
        fp::ui::addPauseButton(this);
    }

    void onResume(CCObject* sender) {
        fp::session::onResumed();
        PauseLayer::onResume(sender);
    }
};

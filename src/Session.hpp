#pragma once

#include <Geode/Geode.hpp>

#include "Analyzer.hpp"
#include "MacroFormat.hpp"
#include "MacroRecorder.hpp"
#include "Playback.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fp::session {

// Everything below must be called on the main game thread only.

std::filesystem::path macrosDir();
std::filesystem::path pendingPath();
std::vector<std::filesystem::path> listMacros();

MacroRecorder& recorder();
Playback& playback();

float speedMultiplier();   // validated 0.10..3.00
float configuredTps();

// Selection (macros tab / playback / analysis)
std::filesystem::path const& selected();
void select(std::filesystem::path p);
std::size_t selectedEvent();
void setSelectedEvent(std::size_t i);
void setSelectedMacro(Macro m);          // cache of the selected macro for markers
Macro const* selectedMacro();

// Recording workflow
bool armed();
std::string startRecording();            // returns error text, empty on success
void stopAndSavePartial(std::string& out);
void discardRecording();
std::string resumeRecording();           // error text or empty
bool resumeAllowed();

// Playback workflow
std::string startPlayback(Macro macro);
void stopPlayback();

// Game events (called from hooks)
void onStep(GJBaseGameLayer* layer, float dt, bool beforeOriginal, bool countTick);
void onButton(bool down, int button, bool isPlayer1);
void onReset();
void onDeath();
void onQuit();
void onLevelComplete();
void onPauseOpened();
void onResumed();
void onLevelEntered(PlayLayer* layer);
bool isInjecting();
bool playerAllowed(bool player2);

// Speedhack scope
void applySpeed();
void restoreSpeed();

// Overlay
void refreshOverlay();
void createOverlay();

std::string statusText();

} // namespace fp::session

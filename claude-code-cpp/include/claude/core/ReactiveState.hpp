#pragma once

#include "../bootstrap/AppState.hpp"
#include <functional>
#include <vector>

namespace claude::reactive {

/// A computed state value that derives from AppState.
/// Called each frame by the FTXUI renderer (immediate mode).
template<typename T>
using StateAccessor = std::function<T(const AppState&)>;

/// Common state accessors — composable, testable functions
/// that derive display values from AppState.
namespace accessors {

// === Model & Provider ===
String currentModel(const AppState& state);
String currentProvider(const AppState& state);

// === Session State ===
bool isStreaming(const AppState& state);
bool isCompactRunning(const AppState& state);
int messageCount(const AppState& state);
int apiCallCount(const AppState& state);

// === Token & Cost ===
long inputTokens(const AppState& state);
long outputTokens(const AppState& state);
double tokenUsagePercent(const AppState& state);
bool shouldAutoCompact(const AppState& state);

// === Mode Flags ===
bool isVimMode(const AppState& state);
bool isDebugMode(const AppState& state);
bool isFastMode(const AppState& state);
String currentEffort(const AppState& state);
bool isBughunterMode(const AppState& state);

// === UI State ===
bool hasActivePermission(const AppState& state);
String permissionTarget(const AppState& state);
bool isModalOpen(const AppState& state);

} // namespace accessors

/// Compose multiple accessors into a single struct for efficient batch reads.
struct DisplayState {
    String model;
    String provider;
    bool streaming;
    long inputTokens;
    long outputTokens;
    double usagePercent;
    bool vimMode;
    String effort;
    int messages;
    int apiCalls;

    /// Compute all fields from AppState in one pass
    static DisplayState fromAppState(const AppState& state);
};

} // namespace claude::reactive

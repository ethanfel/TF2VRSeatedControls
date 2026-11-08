#pragma once
#include "Comfort.h"

namespace seated {
bool BridgeInitialize(float chestMultiplier, const ComfortConfig& comfort = {});
bool BridgeGameplay(bool active);
unsigned BridgeCues();
bool BridgeReady();
bool BridgeCanSwap();
void BridgeRequestSwap();
void BridgeCancel();
void BridgeShutdown();
}

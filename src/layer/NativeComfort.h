#pragma once
#include "Comfort.h"
#include "Profiles.h"
namespace seated {
bool ComfortInitialize(unsigned char* base, const Profile& profile, const ComfortConfig& config);
void ComfortShutdown();
void ComfortGameplay(bool active);
void ComfortCue(int hand, bool valid, bool grenade);
unsigned ComfortCues();
}

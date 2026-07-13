#pragma once

bool InitSounds();
void UnloadSounds();
bool IsSoundAvailable();
void SetSoundVolume(float volume);
void PlayPacketSend();
void PlayPacketArrive();
void PlayPacketFail();

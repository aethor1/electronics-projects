#pragma once
#include <Arduino.h>
#include "PlayerConfig.h"

// RELEASE stops playback and closes the song file (before USB drive mode).
// SET_VOLUME (0..100): the audio task is the only writer of the output gain.
enum Action : uint8_t { START, TOGGLE, PAUSE, RESUME, RELEASE, SET_VOLUME };
struct AudioCommand { Action action; bool sd; uint8_t volume; char path[player::kMaxPath]; };
struct AudioState {
    bool running=false, paused=false, sd=false, failed=false;
    uint32_t bytes=0, position=0, elapsed=0;
    uint32_t writes=0, frames=0, readCalls=0, slices=0;
    uint32_t releases=0; // incremented each time RELEASE has closed the file
    uint8_t volume=player::kDefaultVolume;
    uint16_t gainQ12=0;
    char path[player::kMaxPath]={};
    char error[80]={};
};
bool audioBegin(TaskHandle_t uiTask);
bool audioSend(const AudioCommand &command);
AudioState audioSnapshot();

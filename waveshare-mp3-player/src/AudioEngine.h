#pragma once
#include <Arduino.h>
#include "PlayerConfig.h"

enum Action : uint8_t { START, TOGGLE, PAUSE, RESUME };
struct AudioCommand { Action action; bool sd; char path[player::kMaxPath]; };
struct AudioState {
    bool running=false, paused=false, sd=false, failed=false;
    uint32_t bytes=0, position=0, elapsed=0;
    uint32_t writes=0, frames=0, readCalls=0, slices=0;
    char path[player::kMaxPath]={};
    char error[80]={};
};
bool audioBegin(TaskHandle_t uiTask);
bool audioSend(const AudioCommand &command);
AudioState audioSnapshot();

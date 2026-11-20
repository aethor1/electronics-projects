#pragma once
// "Add music" over Wi-Fi: the player becomes its own access point and serves an upload
// page at http://192.168.4.1/. The web server runs in its own task so uploads never
// block the UI; this module never touches LVGL or the playlist.
#include <Arduino.h>

namespace upload {

struct Status {
    bool active = false;
    bool receiving = false;       // a song is being received right now
    bool doneRequested = false;   // page/Mac app pressed Done
    uint8_t stations = 0;         // phones/Macs joined to the player's Wi-Fi
    uint32_t lastActivityMs = 0;  // last HTTP request or upload chunk
    uint32_t filesSaved = 0;
    uint32_t httpRequests = 0;    // every request this session (diagnostics)
    uint32_t bytesReceived = 0, bytesExpected = 0;
    char currentFile[64] = {};
    char lastMessage[80] = {};
};

constexpr char kUrl[] = "http://192.168.4.1/";

bool begin();      // start AP + server; false if Wi-Fi could not start
void end();        // stop server + radio completely
Status status();   // thread-safe snapshot

}  // namespace upload

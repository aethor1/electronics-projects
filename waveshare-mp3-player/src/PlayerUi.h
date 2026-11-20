#pragma once
// All LVGL drawing for the player. No Arduino, SD, audio or Wi-Fi code in here, so the
// same file builds into the firmware and into tools/ui_preview (PNG screenshots on a PC).
#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace ui {

enum Tab : uint8_t { kPlayer, kLibrary, kAdd, kInfo };

// Implemented by the firmware (main.cpp) or the preview. Called from LVGL event callbacks.
struct Actions {
    void (*previous)();
    void (*playPause)();
    void (*next)();
    void (*volume)(uint8_t level);                  // 0..100, while dragging and on release
    void (*openItem)(size_t index);                 // index into the last setFiles() list
    void (*folderUp)();
    void (*addWifi)();
    void (*addUsb)();
    void (*overlayDone)();                          // Wi-Fi "Done" / USB "Cancel"
};

struct NowPlaying {
    const char *title = "", *artist = "";
    const char *state = "";    // short, upper case: PLAYING / PAUSED / STOPPED / ERROR
    const char *detail = "";   // right of the progress bar (file size)
    bool playing = false;      // play/pause button shows pause
    uint32_t elapsed = 0;      // seconds
    uint16_t permille = 0;     // progress 0..1000
    int track = 0, tracks = 0; // 1-based; 0 = unknown
};
struct FileItem { std::string name; bool directory; bool current; };
struct StorageInfo {
    bool sdReady = false;
    uint64_t totalBytes = 0, usedBytes = 0;
    unsigned songs = 0;
    std::string note;          // import / scan result
    float psramMiB = 0;
};

void create(const Actions &actions, bool usbSupported, uint8_t volume);
void showTab(Tab tab);
Tab activeTab();
void setNowPlaying(const NowPlaying &now);  // only changed text/values are redrawn
void setMessage(const char *text);           // transient note in the artist line
void setVolume(uint8_t level);
void setFiles(const char *folder, const std::vector<FileItem> &items, const char *footer);
void setStorage(const StorageInfo &info);
void setAddHint(const char *text);
void showDetails(const char *title, const char *text);

// Add-music overlays (cover everything, swallow touches).
void openWifi(const char *ssid, const char *password, const char *url);
void showWifiPage(bool pageCode);  // false = QR 1 (join Wi-Fi), true = QR 2 (open page)
void stationJoined();              // moves to QR 2 unless the user flipped codes by hand
void setWifiStatus(const char *line, const char *corner);
void openUsb();
void setUsbStatus(const char *text);
void closeOverlay();

}  // namespace ui

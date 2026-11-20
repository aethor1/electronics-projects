// Renders the firmware's real UI code (src/PlayerUi.cpp) with LVGL on a PC and dumps each
// screen as raw RGB888 (tools/ui_preview/run.py turns them into PNGs). SD, audio and
// Wi-Fi are replaced by the sample data below; no hardware code is involved.
#include <lvgl.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "PlayerConfig.h"
#include "PlayerUi.h"
#include "WifiUpload.h"

extern "C" uint32_t millis(void) { static uint32_t now; return now += 5; }

static uint8_t frame[240 * 320 * 3];
static lv_color_t drawPixels[240 * 320];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            const uint32_t c = lv_color_to32(*pixels++);  // handles LV_COLOR_16_SWAP
            uint8_t *out = frame + (y * 240 + x) * 3;
            out[0] = (c >> 16) & 0xFF; out[1] = (c >> 8) & 0xFF; out[2] = c & 0xFF;
        }
    lv_disp_flush_ready(driver);
}
static std::string outDir;
static void shot(const char *name) {
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(nullptr);
    const std::string path = outDir + "/" + name + ".rgb";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f || fwrite(frame, 1, sizeof(frame), f) != sizeof(frame)) { perror(path.c_str()); exit(1); }
    fclose(f);
    printf("shot %s\n", name);
}
static size_t heapUsed() {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    return m.total_size - m.free_size;
}

int main(int argc, char **argv) {
    outDir = argc > 1 ? argv[1] : ".";
    lv_init();
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, drawPixels, nullptr, 240 * 320);
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = 240; display.ver_res = 320; display.draw_buf = &buffer; display.flush_cb = flush;
    lv_disp_t *disp = lv_disp_drv_register(&display);
    // Same theme call as the firmware's main.cpp.
    lv_disp_set_theme(disp, lv_theme_default_init(disp, lv_color_hex(0x9BBC0F), lv_color_hex(0xB0306A), true, &lv_font_montserrat_14));

    ui::create(ui::Actions{}, true, player::kDefaultVolume);
    const size_t afterCreate = heapUsed();

    ui::NowPlaying now;
    now.title = "No Scrubs"; now.artist = "TLC"; now.state = "PLAYING"; now.detail = "4.01 MB";
    now.playing = true; now.elapsed = 83; now.permille = 370; now.track = 3; now.tracks = 12;
    ui::setNowPlaying(now);
    shot("1-now-playing");

    now.title = "Malcolm Todd - Chest Pain (I Love) [Extended]"; now.artist = "Music"; now.state = "PAUSED";
    now.playing = false; now.elapsed = 201; now.permille = 815; now.track = 7;
    ui::setNowPlaying(now);
    ui::setVolume(100);
    shot("2-now-playing-paused");

    // Worst case for the 96 KB LVGL heap: a full 128-entry folder with long names.
    std::vector<ui::FileItem> many;
    for (int i = 0; i < 128; ++i)
        many.push_back({"Some Artist - A fairly long song title number " + std::to_string(i) + ".mp3", false, false});
    ui::setFiles("/Music", many, "First 128 items shown");
    const size_t fullFolder = heapUsed();

    std::vector<ui::FileItem> items = {
        {"Old Phone Rips", true, false}, {"Road Trip", true, false}, {"Billie Eilish - Birds of a Feather.mp3", false, false},
        {"Malcolm Todd - Chest Pain.mp3", false, false}, {"TLC - No Scrubs.mp3", false, true},
        {"Tame Impala - The Less I Know the Better.mp3", false, false}, {"The Strokes - Reptilia.mp3", false, false}};
    ui::setFiles("/Music", items, "7 ITEMS - TAP A SONG TO PLAY");
    ui::showTab(ui::kLibrary);
    shot("3-library");

    ui::showTab(ui::kAdd);
    shot("4-add-music");

    ui::StorageInfo storage;
    storage.sdReady = true; storage.totalBytes = 15634268160ULL; storage.usedBytes = 1083179008ULL;
    storage.songs = 12; storage.note = "Songs loaded from microSD."; storage.psramMiB = 8;
    ui::setStorage(storage);
    ui::showTab(ui::kInfo);
    shot("5-info");

    ui::showTab(ui::kPlayer);
    ui::openWifi(player::kWifiName, player::kWifiPassword, upload::kUrl);
    ui::setWifiStatus("", "4:59");
    shot("6-wifi-join");
    const size_t withQr = heapUsed();
    ui::stationJoined();
    ui::setWifiStatus("37%  Billie Eilish - Birds of a Feather.mp3", "4:59");
    shot("7-wifi-upload-page");
    ui::closeOverlay();

    ui::openUsb();
    ui::setUsbStatus("Computer connected. Eject it there when done.");
    shot("8-usb-drive");
    ui::closeOverlay();

    printf("lvgl_heap_used after_create=%zu full_128_folder=%zu wifi_overlay=%zu of %u\n",
           afterCreate, fullFolder, withQr, unsigned(LV_MEM_SIZE));
    return 0;
}

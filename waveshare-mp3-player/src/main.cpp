// Scoped touchscreen/SD/audio test. Original factory image and speaker test are
// preserved in backups/. No battery/charging/headphone circuit is changed.
#include <Arduino.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <lvgl.h>
#include <mbedtls/sha256.h>
#include <vector>
#include <algorithm>
#include <string>
#include "AudioEngine.h"
#include "ButtonCore.h"
#include "PlayerCore.h"
#include "TransferCore.h"
#include "UsbDrive.h"
#include "WifiUpload.h"
#include "vendor/Display_ST7789.h"

static bool sdReady, flashReady, copiedSong;
static uint64_t sdTotal, sdUsed;
static String importPath, importResult;
static constexpr char expectedHash[] = "700c4d7edc15c1d1a1fd8d69c93a7ae282ffd17c1aa626594aa25329b99adf18";
// Imports and hashing are boot-only, so they can share one scratch block.
static uint8_t fileScratch[4096];
static lv_obj_t *tabs, *titleLabel, *pathLabel, *stateLabel, *positionBar;
static lv_obj_t *playText, *browserList, *folderLabel, *fileCountLabel, *storageLabel;
static lv_indev_t *touchDevice;
static uint16_t *screenPixels;
static uint32_t flushCount, touchCount, lastActivity, lastTouchProbe, readyMs;
static bool physicalTouch, uiReady, booting=true, audioReady, scanLimited;
static player::ScreenMode screenMode=player::ScreenMode::Awake;
static player::WakeGesture wakeGesture;
static lv_disp_draw_buf_t drawBuffer;
static lv_color_t drawPixels[240 * 24];
struct Track { String path; bool sd; };
struct Entry { String path; bool directory; uint32_t size; };
static std::vector<Track> tracks;
static std::vector<Entry> entries;
static int selectedTrack = 0;
static String folder = "/";
// Add music: Wi-Fi upload or USB drive. While either is active, playback controls,
// the file browser and the buttons are ignored, and the screen stays awake.
enum class Transfer : uint8_t { None, Wifi, Usb };
static Transfer transfer=Transfer::None, transferRequest=Transfer::None;
static bool transferExitRequested, usbCancelArmed, showingUrlQr, userPickedQr;
static uint32_t transferStartedAt, transferUiAt, usbEjectedAt;
static lv_obj_t *overlay, *qrCode, *overlayInfo, *overlayStatus, *qrToggleLabel, *addInfoLabel;
static TaskHandle_t uiTaskHandle;
static String basenameOf(const String &path) { return path.substring(path.lastIndexOf('/') + 1); }
static bool isMp3(const String &path) {
    const unsigned n=path.length();
    return n>=4 && path[n-4]=='.' && (path[n-3]=='m'||path[n-3]=='M') &&
           (path[n-2]=='p'||path[n-2]=='P') && path[n-1]=='3';
}
static AudioState snapshot() { return audioSnapshot(); }
static void bootService() { if(uiReady) lv_timer_handler(); delay(1); }
static String hashFile(fs::FS &fs, const char *path) {
    File file = fs.open(path, FILE_READ);
    if (!file || file.isDirectory()) return "";
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    uint8_t digest[32];
    size_t count = 0;
    while (file.available()) {
        int n = file.read(fileScratch, sizeof(fileScratch));
        if (n <= 0) break;
        count += n;
        mbedtls_sha256_update_ret(&ctx, fileScratch, n);
        bootService();
    }
    bool complete = count == file.size();
    file.close();
    mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    if (!complete) return "";
    char result[65];
    for (int i = 0; i < 32; ++i) snprintf(result + 2*i, 3, "%02x", digest[i]);
    return String(result);
}
static void importSong() {
    // Never format the card, replace a pre-existing file, or modify the source.
    if (!sdReady || !flashReady || !LittleFS.exists("/test.mp3")) {
        importResult = "Test song not copied: storage unavailable."; return;
    }
    if (hashFile(LittleFS, "/test.mp3") != expectedHash) {
        importResult = "Internal song checksum mismatch; no copy made."; return;
    }
    if (!SD_MMC.exists("/Music") && !SD_MMC.mkdir("/Music")) {
        importResult = "Could not create Music folder."; return;
    }
    File dir = SD_MMC.open("/Music");
    bool isDirectory = dir && dir.isDirectory(); dir.close();
    if (!isDirectory) { importResult = "/Music is not a directory."; return; }
    String target = "/Music/TLC - No Scrubs.mp3";
    for (int i = 2; SD_MMC.exists(target); ++i) {
        if (hashFile(SD_MMC, target.c_str()) == expectedHash) {
            importPath = target; copiedSong = true;
            importResult = "Existing SD copy: SHA-256 verified.";
            Serial.printf("SD_COPY_ALREADY_VERIFIED path=%s sha256=%s\n", target.c_str(), expectedHash);
            return;
        }
        target = "/Music/TLC - No Scrubs (test " + String(i) + ").mp3";
        if (i > 100) { importResult = "Too many existing test names."; return; }
    }
    File source = LittleFS.open("/test.mp3", FILE_READ);
    if (sdTotal - sdUsed < source.size() + 65536ULL) {
        source.close(); importResult = "Card has insufficient free space."; return;
    }
    String temp = "/Music/.song-import.tmp";
    for (int i=1; SD_MMC.exists(temp); ++i) temp = "/Music/.song-import-" + String(i) + ".tmp";
    File dest = SD_MMC.open(temp, FILE_WRITE);
    if (!dest) { source.close(); importResult = "Card is not writable."; return; }
    size_t expected = source.size(), written = 0;
    while (source.available()) {
        int n = source.read(fileScratch, sizeof(fileScratch));
        if (n <= 0 || dest.write(fileScratch, n) != size_t(n)) break;
        written += n; bootService();
    }
    dest.flush(); dest.close(); source.close();
    if (written != expected || hashFile(SD_MMC, temp.c_str()) != expectedHash) {
        SD_MMC.remove(temp); importResult = "SD copy failed verification; original retained."; return;
    }
    if (SD_MMC.exists(target) || !SD_MMC.rename(temp, target)) {
        SD_MMC.remove(temp); importResult = "Could not finish SD copy."; return;
    }
    importPath = target; copiedSong = true;
    importResult = "Song copied to SD; SHA-256 verified.";
    Serial.printf("SD_COPY_VERIFIED path=%s bytes=%u sha256=%s\n", target.c_str(), unsigned(written), expectedHash);
}
static uint32_t scanStartedAt, scannedEntries;
static bool musicScanned;
static void scanMusic(const String &path, int depth=0) {
    if (depth > int(player::kMaxScanDepth) || tracks.size() >= player::kMaxTracks) return;
    if(path.equalsIgnoreCase("/Music")) {
        if(musicScanned) return;
        musicScanned=true;
    }
    File directory = SD_MMC.open(path);
    if (!directory || !directory.isDirectory()) return;
    File file;
    while (tracks.size() < player::kMaxTracks && (file = directory.openNextFile())) {
        if (++scannedEntries > player::kMaxScanEntries || millis()-scanStartedAt >= player::kMaxScanMs) {
            scanLimited=true; file.close(); break;
        }
        String full = path == "/" ? "/" + basenameOf(file.name()) : path + "/" + basenameOf(file.name());
        bool subdir = file.isDirectory(); file.close();
        // Skip dot entries: macOS copies leave "._Song.mp3" metadata files that are not MP3s.
        const bool hidden = basenameOf(full).startsWith(".");
        if (subdir && !hidden) scanMusic(full, depth+1);
        else if (!subdir && !hidden && isMp3(full)) tracks.push_back({full, true});
        if (scannedEntries%16==0) bootService();
    }
    directory.close();
}
static void sendAction(Action action) {
    if (!audioReady || booting || transfer!=Transfer::None) return;
    lastActivity=millis();
    AudioCommand c = {}; c.action = action;
    if (!audioSend(c)) lv_label_set_text(stateLabel,"Busy; try again.");
}
static bool playPath(const String &path,bool sd) {
    if (!audioReady || booting || transfer!=Transfer::None || path.length()>=player::kMaxPath) return false;
    AudioCommand c = {}; c.action=START; c.sd=sd;
    strlcpy(c.path,path.c_str(),sizeof(c.path));
    lastActivity=millis();
    return audioSend(c);
}
static void playTrack(int index) {
    if (booting || tracks.empty()) return;
    selectedTrack = (index + int(tracks.size())) % tracks.size();
    if (!playPath(tracks[selectedTrack].path,tracks[selectedTrack].sd) && uiReady)
        lv_label_set_text(stateLabel,"Cannot queue this song.");
}
static void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    if (screenPixels) {
        int width = area->x2 - area->x1 + 1;
        for (int y=area->y1; y<=area->y2; ++y)
            memcpy(screenPixels + y*240 + area->x1, pixels + (y-area->y1)*width, width*2);
    }
    LCD_addWindow(area->x1, area->y1, area->x2, area->y2, reinterpret_cast<uint16_t*>(pixels));
    ++flushCount; lv_disp_flush_ready(driver);
}
static void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
    const uint32_t now=millis();
    const bool wasOff=screenMode==player::ScreenMode::Off;
    const bool pending=Touch_ConsumeInterrupt();
    if (!pending && !physicalTouch && now-lastTouchProbe<player::kTouchFallbackMs) {
        data->state=LV_INDEV_STATE_REL; return;
    }
    lastTouchProbe=now;
    uint16_t x[5]={}, y[5]={}, strength[5]={}; uint8_t points=0;
    const bool read=Touch_Read_Data();
    bool pressed = read && Touch_Get_XY(x, y, strength, &points, 5) && points && x[0]<240 && y[0]<320;
    physicalTouch=pressed;
    static bool wasPressed = false;
    if (pressed) {
        lastActivity=now;
        if (screenMode!=player::ScreenMode::Awake) {
            if(wasOff) LCD_SetSleep(false);
            screenMode=player::ScreenMode::Awake;
            if(wasOff && uiReady) lv_obj_invalidate(lv_scr_act());
            Set_Backlight(player::kBrightness);
        }
    }
    const bool deliver=wakeGesture.filter(pressed,wasOff);
    if (deliver) {
        data->point.x=x[0]; data->point.y=y[0]; data->state=LV_INDEV_STATE_PR;
        if (!wasPressed) ++touchCount;
    } else data->state=LV_INDEV_STATE_REL;
    wasPressed = pressed;
    if(touchDevice) lv_timer_set_period(touchDevice->driver->read_timer,pressed?15:player::kTouchFallbackMs);
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width, uint32_t color=0xE7EBF2) {
    lv_obj_t *obj=lv_label_create(parent); lv_label_set_text(obj,text);
    lv_obj_set_pos(obj,x,y); lv_obj_set_width(obj,width); lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width, lv_event_cb_t cb) {
    lv_obj_t *obj=lv_btn_create(parent); lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,width,44);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x294766),0); lv_obj_add_event_cb(obj,cb,LV_EVENT_CLICKED,nullptr);
    lv_obj_t *caption=lv_label_create(obj); lv_label_set_text(caption,text); lv_obj_center(caption); return caption;
}
static void populateBrowser();
static void showDetails(const Entry &entry) {
    String text=String("microSD: ")+entry.path+"\n"+String(entry.size)+" bytes";
    if (!isMp3(entry.path)) text += "\n\nThis test plays MP3 files.";
    lv_obj_t *box=lv_msgbox_create(nullptr,"File details",text.c_str(),nullptr,true);
    lv_obj_set_width(box,220); lv_obj_center(box);
}
static void fileClicked(lv_event_t *e) {
    size_t index=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (index>=entries.size()) return;
    Entry entry=entries[index];
    if (entry.directory) { folder=entry.path; populateBrowser(); return; }
    if (isMp3(entry.path)) {
        auto found=std::find_if(tracks.begin(),tracks.end(),[&](const Track &t){return t.sd&&t.path==entry.path;});
        if (found==tracks.end() && tracks.size()<player::kMaxTracks) {
            tracks.push_back({entry.path,true}); found=tracks.end()-1;
        }
        if (found!=tracks.end()) playTrack(found-tracks.begin());
        else if (!playPath(entry.path,true)) lv_label_set_text(stateLabel,"Song path too long or player busy.");
        lv_tabview_set_act(tabs,0,LV_ANIM_OFF);
    } else showDetails(entry);
}
static void populateBrowser() {
    lv_obj_clean(browserList); entries.clear(); lv_label_set_text(folderLabel,folder.c_str());
    if (!sdReady) {
        lv_list_add_text(browserList,booting?"Reading microSD...":"microSD could not be mounted.");
        lv_label_set_text(fileCountLabel,booting?"Please wait...":"No card formatting performed."); return;
    }
    File dir=SD_MMC.open(folder);
    if (!dir || !dir.isDirectory()) { lv_list_add_text(browserList,"Folder unavailable"); return; }
    File f;
    while (entries.size()<128 && (f=dir.openNextFile())) {
        String name=basenameOf(f.name()); String path=folder=="/" ? "/"+name : folder+"/"+name;
        if (name.startsWith(".")) { f.close(); continue; } // macOS/upload metadata and temp files
        entries.push_back({path,f.isDirectory(),unsigned(f.size())}); f.close();
    }
    dir.close();
    std::sort(entries.begin(),entries.end(),[](const Entry &a,const Entry &b){
        if(a.directory!=b.directory) return a.directory>b.directory; return a.path<b.path;
    });
    for (size_t i=0;i<entries.size();++i) {
        const Entry &e=entries[i]; String text=basenameOf(e.path);
        if(!e.directory) text+="\n"+String(e.size)+" bytes";
        lv_obj_t *b=lv_list_add_btn(browserList,e.directory?LV_SYMBOL_DIRECTORY:LV_SYMBOL_FILE,text.c_str());
        lv_obj_set_style_pad_ver(b,12,0); lv_obj_t *caption=lv_obj_get_child(b,-1);
        lv_label_set_long_mode(caption,LV_LABEL_LONG_WRAP);
        lv_obj_add_event_cb(b,fileClicked,LV_EVENT_CLICKED,reinterpret_cast<void*>(i));
    }
    if(entries.empty()) lv_list_add_text(browserList,"This folder is empty.");
    String count=String(entries.size())+" items | tap an MP3 to play";
    if(entries.size()==128) count="First 128 items shown";
    lv_label_set_text(fileCountLabel,count.c_str());
}
static void createUI() {
    lv_init(); lv_disp_draw_buf_init(&drawBuffer,drawPixels,nullptr,240*24);
    static lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res=240; display.ver_res=320; display.draw_buf=&drawBuffer; display.flush_cb=flushDisplay;
    lv_disp_t *disp=lv_disp_drv_register(&display);
    lv_theme_t *theme=lv_theme_default_init(disp,lv_palette_main(LV_PALETTE_BLUE),lv_palette_main(LV_PALETTE_TEAL),true,&lv_font_montserrat_14);
    lv_disp_set_theme(disp,theme);
    static lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type=LV_INDEV_TYPE_POINTER; input.read_cb=readTouch; touchDevice=lv_indev_drv_register(&input);
    lv_timer_set_period(input.read_timer,player::kTouchFallbackMs);
    tabs=lv_tabview_create(lv_scr_act(),LV_DIR_BOTTOM,42);
    lv_obj_t *player=lv_tabview_add_tab(tabs,"Player");
    lv_obj_t *files=lv_tabview_add_tab(tabs,"Files");
    lv_obj_t *add=lv_tabview_add_tab(tabs,"Add");
    lv_obj_t *storage=lv_tabview_add_tab(tabs,"Info");
    for(lv_obj_t *page : {player,files,add,storage}) {
        lv_obj_set_style_pad_all(page,12,0); lv_obj_set_style_bg_color(page,lv_color_hex(0x101923),0);
        lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
    }
    lv_obj_clear_flag(player,LV_OBJ_FLAG_SCROLLABLE);
    label(player,"NOW PLAYING",0,0,210,0x72BEDD);
    titleLabel=label(player,"Loading music...",0,27,210);
    lv_obj_set_style_text_font(titleLabel,&lv_font_montserrat_20,0);
    lv_obj_set_height(titleLabel,52); lv_label_set_long_mode(titleLabel,LV_LABEL_LONG_WRAP);
    pathLabel=label(player,"",0,84,210,0xA4B5C8);
    lv_obj_set_height(pathLabel,48); lv_label_set_long_mode(pathLabel,LV_LABEL_LONG_WRAP);
    positionBar=lv_bar_create(player); lv_obj_set_pos(positionBar,0,139); lv_obj_set_size(positionBar,210,5);
    lv_bar_set_range(positionBar,0,1000); stateLabel=label(player,"Starting...",0,154,210);
    lv_obj_set_height(stateLabel,20); lv_label_set_long_mode(stateLabel,LV_LABEL_LONG_DOT);
    button(player,LV_SYMBOL_PREV,0,187,56,[](lv_event_t*){playTrack(selectedTrack-1);});
    playText=button(player,LV_SYMBOL_PAUSE,65,187,80,[](lv_event_t*){sendAction(TOGGLE);});
    button(player,LV_SYMBOL_NEXT,154,187,56,[](lv_event_t*){playTrack(selectedTrack+1);});
    label(player,"Volume: onboard knob",0,236,212,0xA4B5C8);
    folderLabel=label(files,"/",0,0,150,0x72BEDD); lv_label_set_long_mode(folderLabel,LV_LABEL_LONG_DOT);
    lv_obj_t *up=lv_btn_create(files); lv_obj_set_pos(up,166,-3); lv_obj_set_size(up,44,28);
    lv_obj_t *upLabel=lv_label_create(up); lv_label_set_text(upLabel,LV_SYMBOL_UP); lv_obj_center(upLabel);
    lv_obj_add_event_cb(up,[](lv_event_t*){
        int slash=folder.lastIndexOf('/'); folder=slash<=0?"/":folder.substring(0,slash); populateBrowser();
    },LV_EVENT_CLICKED,nullptr);
    browserList=lv_list_create(files); lv_obj_set_pos(browserList,0,34); lv_obj_set_size(browserList,212,191);
    fileCountLabel=label(files,"",0,236,212,0xA4B5C8); populateBrowser();
    lv_obj_clear_flag(add,LV_OBJ_FLAG_SCROLLABLE);
    label(add,"ADD MUSIC",0,0,210,0x72BEDD);
    button(add,LV_SYMBOL_WIFI "  Wi-Fi (phone / Mac)",0,28,212,[](lv_event_t*){transferRequest=Transfer::Wifi;});
    if(usbdrive::supported())
        button(add,LV_SYMBOL_USB "  USB drive (computer)",0,82,212,[](lv_event_t*){transferRequest=Transfer::Usb;});
    addInfoLabel=label(add,usbdrive::supported() ?
        "Wi-Fi: scan the code with the iPhone camera, then pick songs in Safari.\n\nUSB: copy MP3s into the Music folder, then eject." :
        "Wi-Fi: scan the code with the iPhone camera, then pick songs in Safari.",0,138,212,0xA4B5C8);
    lv_label_set_long_mode(addInfoLabel,LV_LABEL_LONG_WRAP);
    label(storage,"STORAGE",0,0,210,0x72BEDD);
    storageLabel=label(storage,"Reading microSD...",0,29,210);
    uiReady=true;
    lv_refr_now(nullptr);
}
static void updateStorage() {
    String info=sdReady ? "microSD mounted\n"+String(double(sdTotal)/1e9,2)+" GB usable\n"+
        String(double(sdUsed)/1e6,2)+" MB used\n"+String(double(sdTotal-sdUsed)/1e9,2)+" GB free" :
        "microSD unavailable\nNo format performed";
    info+="\n\n"+importResult;
    if(copiedSong) info+="\n"+importPath+"\n4,007,763 bytes";
    if(scanLimited) info+="\nMore files available in Files.";
    info+="\n\nInternal flash: 16 MiB\nPSRAM: "+String(ESP.getPsramSize()/1048576.0,1)+" MiB";
    lv_label_set_text(storageLabel,info.c_str());
}
static void setTextIfChanged(lv_obj_t *object,const char *text) {
    if(strcmp(lv_label_get_text(object),text)) lv_label_set_text(object,text);
}
static void refreshPlayer() {
    if(booting || screenMode==player::ScreenMode::Off || lv_tabview_get_tab_act(tabs)!=0) return;
    const AudioState state=snapshot();
    static char oldPath[player::kMaxPath]={}; static bool oldSd=false, cached=false;
    if(!cached || strcmp(oldPath,state.path) || oldSd!=state.sd) {
        cached=true;
        strlcpy(oldPath,state.path,sizeof(oldPath)); oldSd=state.sd;
        String name=basenameOf(String(state.path));
        if(isMp3(name)) name.remove(name.length()-4);
        setTextIfChanged(titleLabel,name.length()?name.c_str():"No MP3 selected");
        String origin=String(state.sd?"microSD: ":"Internal flash: ")+state.path;
        setTextIfChanged(pathLabel,origin.c_str());
    }
    char text[100];
    if(state.failed) strlcpy(text,state.error,sizeof(text));
    else snprintf(text,sizeof(text),"%s  %u:%02u  |  %.2f MB",state.paused?"Paused":state.running?"Playing":"Stopped",
                  state.elapsed/60,state.elapsed%60,state.bytes/1e6);
    setTextIfChanged(stateLabel,text);
    setTextIfChanged(playText,state.running&&!state.paused?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
    const int value=state.bytes?uint64_t(state.position)*1000/state.bytes:0;
    if(lv_bar_get_value(positionBar)!=value) lv_bar_set_value(positionBar,value,LV_ANIM_OFF);
}
static void wakeScreen() {
    lastActivity=millis();
    if(screenMode==player::ScreenMode::Off) {
        LCD_SetSleep(false); lv_obj_invalidate(lv_scr_act());
    }
    screenMode=player::ScreenMode::Awake;
    Set_Backlight(player::kBrightness);
}
static void applyScreenPolicy() {
    if(booting) return;
    const auto mode=player::screenMode(millis(),lastActivity,player::kDimAfterMs,player::kScreenOffAfterMs);
    if(mode==screenMode) return;
    if(mode==player::ScreenMode::Off) {
        LCD_SetSleep(true); screenMode=mode;
    } else {
        if(screenMode==player::ScreenMode::Off) { LCD_SetSleep(false); lv_obj_invalidate(lv_scr_act()); }
        screenMode=mode;
        Set_Backlight(mode==player::ScreenMode::Dim?player::kDimBrightness:player::kBrightness);
    }
}
// ---- Library rescan (after songs were added) ------------------------------------
static void rescanLibrary() {
    const String current=selectedTrack>=0 && selectedTrack<int(tracks.size()) ? tracks[selectedTrack].path : "";
    tracks.clear(); musicScanned=false; scannedEntries=0; scanLimited=false; scanStartedAt=millis();
    if(sdReady) {
        scanMusic(player::kMusicFolder); scanMusic("/");
        sdTotal=SD_MMC.totalBytes(); sdUsed=SD_MMC.usedBytes();
    }
    if(tracks.empty() && flashReady && LittleFS.exists("/test.mp3")) tracks.push_back({"/test.mp3",false});
    std::sort(tracks.begin(),tracks.end(),[](const Track &a,const Track &b){return a.path.compareTo(b.path)<0;});
    selectedTrack=0;
    for(size_t i=0;i<tracks.size();++i) if(tracks[i].path==current) selectedTrack=int(i);
    importResult=String(tracks.size())+" songs in the library.";
    populateBrowser(); updateStorage();
    Serial.printf("LIBRARY_RESCANNED songs=%u\n",unsigned(tracks.size()));
}

// ---- Add music overlay (Wi-Fi QR codes / USB drive) -----------------------------
static void openOverlay(const char *title) {
    overlay=lv_obj_create(lv_layer_top()); lv_obj_set_size(overlay,240,320); lv_obj_set_pos(overlay,0,0);
    lv_obj_set_style_bg_color(overlay,lv_color_hex(0x101923),0); lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(overlay,0,0); lv_obj_set_style_radius(overlay,0,0);
    lv_obj_set_style_pad_all(overlay,0,0); lv_obj_clear_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE); // swallow touches meant for the tabs below
    label(overlay,title,12,6,216,0x72BEDD);
}
static void closeOverlay() {
    if(overlay) lv_obj_del(overlay);
    overlay=qrCode=overlayInfo=overlayStatus=qrToggleLabel=nullptr;
    lv_obj_invalidate(lv_scr_act());
}
static lv_obj_t *overlayButton(const char *text,int x,int width,lv_event_cb_t cb) {
    lv_obj_t *b=lv_btn_create(overlay); lv_obj_set_pos(b,x,280); lv_obj_set_size(b,width,34);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x294766),0); lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
    lv_obj_t *caption=lv_label_create(b); lv_label_set_text(caption,text); lv_obj_center(caption); return caption;
}
static void showQr(bool url) {
    if(!qrCode) return;
    showingUrlQr=url;
    const std::string data=url ? std::string(upload::kUrl) : player::wifiQrPayload(player::kWifiName,player::kWifiPassword);
    lv_qrcode_update(qrCode,data.data(),data.size());
    const String info=url ? String("2. Scan to open the upload page\n")+upload::kUrl :
        String("1. Scan with the iPhone camera to join\nWi-Fi: ")+player::kWifiName+"\nPassword: "+player::kWifiPassword;
    lv_label_set_text(overlayInfo,info.c_str());
    lv_label_set_text(qrToggleLabel,url?"Wi-Fi code":"Page code");
}
static void openWifiOverlay() {
    openOverlay("ADD MUSIC OVER WI-FI");
    lv_obj_t *panel=lv_obj_create(overlay); lv_obj_set_size(panel,172,172); lv_obj_set_pos(panel,34,28);
    lv_obj_set_style_bg_color(panel,lv_color_white(),0); lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
    lv_obj_set_style_radius(panel,8,0); lv_obj_set_style_border_width(panel,0,0);
    lv_obj_set_style_pad_all(panel,0,0); lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    // The white panel doubles as the quiet zone the widget itself doesn't draw.
    qrCode=lv_qrcode_create(panel,156,lv_color_black(),lv_color_white()); lv_obj_center(qrCode);
    overlayInfo=label(overlay,"",12,204,216); lv_obj_set_style_text_font(overlayInfo,&lv_font_montserrat_12,0);
    lv_label_set_long_mode(overlayInfo,LV_LABEL_LONG_WRAP);
    overlayStatus=label(overlay,"Starting Wi-Fi...",12,258,216,0xA4B5C8);
    lv_obj_set_height(overlayStatus,18); lv_label_set_long_mode(overlayStatus,LV_LABEL_LONG_DOT);
    qrToggleLabel=overlayButton("Page code",12,104,[](lv_event_t*){ userPickedQr=true; showQr(!showingUrlQr); });
    overlayButton("Done",124,104,[](lv_event_t*){ transferExitRequested=true; });
    showQr(false);
}
static void updateWifiOverlay(const upload::Status &s) {
    char text[112];
    const uint32_t left=player::sessionSecondsLeft(millis(),s.lastActivityMs,player::kUploadIdleTimeoutMs);
    if(s.receiving && s.bytesExpected)
        snprintf(text,sizeof(text),"%u%%  %s",unsigned(uint64_t(s.bytesReceived)*100/s.bytesExpected),s.currentFile);
    else if(s.filesSaved)
        snprintf(text,sizeof(text),"%u added. Off in %u:%02u if idle",unsigned(s.filesSaved),unsigned(left/60),unsigned(left%60));
    else if(s.lastMessage[0]) snprintf(text,sizeof(text),"%s",s.lastMessage);
    else if(s.stations) snprintf(text,sizeof(text),"Connected. Off in %u:%02u if idle",unsigned(left/60),unsigned(left%60));
    else snprintf(text,sizeof(text),"Waiting for a phone... %u:%02u",unsigned(left/60),unsigned(left%60));
    setTextIfChanged(overlayStatus,text);
    // Once a phone has joined, move on to the page code (unless someone flipped it by hand).
    if(s.stations && !showingUrlQr && !userPickedQr) showQr(true);
}
static void startWifi() {
    if(!sdReady) { lv_label_set_text(addInfoLabel,"No microSD card, so there is nowhere to save songs."); return; }
    sendAction(PAUSE); // Wi-Fi runs on the audio core; pause instead of risking dropouts.
    transfer=Transfer::Wifi; userPickedQr=false; transferExitRequested=false;
    transferStartedAt=transferUiAt=millis();
    openWifiOverlay(); lv_refr_now(nullptr);
    const bool ok=upload::begin();
    if(!ok) { upload::end(); lv_label_set_text(overlayStatus,"Wi-Fi could not start. Tap Done."); }
    Serial.printf("WIFI_UPLOAD %s heap=%u\n",ok?"ON":"FAILED",ESP.getFreeHeap());
}
static void stopWifi() {
    const upload::Status s=upload::status();
    upload::end();
    closeOverlay(); transfer=Transfer::None; transferExitRequested=false;
    if(s.filesSaved) rescanLibrary();
    else { sdUsed=SD_MMC.usedBytes(); updateStorage(); }
    lastActivity=millis();
    Serial.printf("WIFI_UPLOAD OFF added=%u\n",unsigned(s.filesSaved));
}
static void openUsbOverlay() {
    openOverlay("USB DRIVE MODE");
    lv_obj_t *info=label(overlay,
        "1. Plug into a Mac or PC with USB-C.\n"
        "2. Copy MP3s into the Music folder, or use the Mac app.\n"
        "3. Eject the drive on the computer.\n\n"
        "The player restarts by itself after the eject.",12,32,216);
    lv_label_set_long_mode(info,LV_LABEL_LONG_WRAP);
    overlayStatus=label(overlay,"Starting USB...",12,240,216,0xA4B5C8);
    lv_obj_set_height(overlayStatus,36); lv_label_set_long_mode(overlayStatus,LV_LABEL_LONG_WRAP);
    overlayButton("Cancel",12,216,[](lv_event_t*){ transferExitRequested=true; });
}
static void startUsb() {
    if(!usbdrive::supported()) return;
    if(!sdReady) { lv_label_set_text(addInfoLabel,"No microSD card to share over USB."); return; }
    // The computer will write raw sectors: the audio task must close its song file first.
    if(audioReady) {
        const uint32_t before=snapshot().releases;
        AudioCommand c={}; c.action=RELEASE;
        bool released=audioSend(c);
        for(int i=0; released && i<200 && snapshot().releases==before; ++i) delay(10);
        if(!released || snapshot().releases==before) { lv_label_set_text(addInfoLabel,"Player busy; try USB again."); return; }
    }
    transfer=Transfer::Usb; usbCancelArmed=false; transferExitRequested=false; usbEjectedAt=0;
    transferStartedAt=transferUiAt=millis();
    openUsbOverlay(); lv_refr_now(nullptr);
    Serial.println("USB_DRIVE ON (this serial port disappears until the player restarts)");
    Serial.flush(); delay(50);
    const char *error=nullptr;
    if(!usbdrive::begin(&error)) lv_label_set_text(overlayStatus,error?error:"USB drive could not start.");
}
static void serviceUsb() {
    const uint32_t now=millis();
    if(usbdrive::ejected()) {
        if(!usbEjectedAt) { usbEjectedAt=now; setTextIfChanged(overlayStatus,"Ejected. Restarting with the new songs..."); }
        else if(now-usbEjectedAt>=1500) usbdrive::restartPlayer();
        return;
    }
    if(transferExitRequested) {
        transferExitRequested=false;
        if(usbdrive::hostAccessed() && !usbCancelArmed) {
            usbCancelArmed=true;
            setTextIfChanged(overlayStatus,"Eject it on the computer first! Tap Cancel again to restart anyway.");
        } else usbdrive::restartPlayer();
        return;
    }
    if(!usbdrive::hostAccessed() && player::sessionExpired(now,transferStartedAt,player::kUploadIdleTimeoutMs))
        usbdrive::restartPlayer(); // tapped by accident, never plugged in
    if(!usbCancelArmed)
        setTextIfChanged(overlayStatus,usbdrive::hostAccessed() ? "Computer connected. Eject it there when done." :
                                                                   "Waiting for a computer...");
}
static void serviceTransfer() {
    if(transferRequest!=Transfer::None) {
        const Transfer request=transferRequest; transferRequest=Transfer::None;
        if(transfer==Transfer::None && !booting) {
            if(request==Transfer::Wifi) startWifi(); else startUsb();
        }
    }
    if(transfer==Transfer::None) return;
    lastActivity=millis(); // keep the screen on while a code or instructions are shown
    if(transfer==Transfer::Usb) { serviceUsb(); return; }
    const uint32_t now=millis();
    if(transferExitRequested) { stopWifi(); return; }
    if(now-transferUiAt<500) return;
    transferUiAt=now;
    const upload::Status s=upload::status();
    if(s.doneRequested || (!s.receiving && player::sessionExpired(now,s.lastActivityMs,player::kUploadIdleTimeoutMs))) {
        stopWifi(); return;
    }
    updateWifiOverlay(s);
}

// ---- Physical buttons --------------------------------------------------------------
static constexpr player::ButtonTiming kButtonTiming{player::kButtonDebounceMs,player::kButtonDoubleGapMs,player::kButtonLongMs};
// A button with no double/long mapping reacts on release instead of waiting out the double-click window.
static player::ButtonGesture buttonA(kButtonTiming,
    player::kButtonADouble==player::ButtonAction::None && player::kButtonALong==player::ButtonAction::None);
static player::ButtonGesture buttonB(kButtonTiming,
    player::kButtonBDouble==player::ButtonAction::None && player::kButtonBLong==player::ButtonAction::None);
static void IRAM_ATTR buttonInterrupt() {
    BaseType_t woken=pdFALSE;
    if(uiTaskHandle) vTaskNotifyGiveFromISR(uiTaskHandle,&woken);
    if(woken) portYIELD_FROM_ISR();
}
static void runButtonAction(player::ButtonAction action) {
    using A=player::ButtonAction;
    if(action==A::None || booting || transfer!=Transfer::None) return;
    if(action==A::WakeScreen) { wakeScreen(); return; }
    const uint32_t keep=lastActivity; // media buttons work with the screen off and don't wake it
    if(action==A::PlayPause) sendAction(TOGGLE);
    else if(action==A::Next) playTrack(selectedTrack+1);
    else if(action==A::Previous) playTrack(selectedTrack-1);
    else if(action==A::Restart) playTrack(selectedTrack);
    lastActivity=keep;
}
static void dispatchButton(player::ButtonEvent event,player::ButtonAction single,player::ButtonAction twice,player::ButtonAction hold) {
    if(event==player::ButtonEvent::Single) runButtonAction(single);
    else if(event==player::ButtonEvent::Double) runButtonAction(twice);
    else if(event==player::ButtonEvent::Long) runButtonAction(hold);
}
static void setupButtons() {
    for(int pin : {player::kButtonAPin,player::kButtonBPin}) {
        if(pin<0) continue;
        pinMode(pin,INPUT_PULLUP); // replaces UART0 TX/RX on these pads
        attachInterrupt(pin,buttonInterrupt,CHANGE);
    }
}
// Returns true while a click decision still depends on time passing.
static bool pollButtons() {
    const uint32_t now=millis();
    bool pending=false;
    if(player::kButtonAPin>=0) {
        dispatchButton(buttonA.update(digitalRead(player::kButtonAPin)==LOW,now),
                       player::kButtonASingle,player::kButtonADouble,player::kButtonALong);
        pending|=buttonA.pending();
    }
    if(player::kButtonBPin>=0) {
        dispatchButton(buttonB.update(digitalRead(player::kButtonBPin)==LOW,now),
                       player::kButtonBSingle,player::kButtonBDouble,player::kButtonBLong);
        pending|=buttonB.pending();
    }
    return pending;
}

static void serialCommand(const String &line) {
    if(line=="status" || line=="metrics") {
        const AudioState state=snapshot();
        Serial.printf("STATUS storage=%s path=%s state=%s position=%u/%u elapsed=%u flushes=%u touches=%u heap=%u boot_ms=%u screen=%u i2s_writes=%u frames=%u read_refills=%u audio_slices=%u transfer=%u error=%s\n",
            state.sd?"microSD":"internal",state.path,state.failed?"failed":state.paused?"paused":state.running?"playing":"stopped",
            state.position,state.bytes,state.elapsed,flushCount,touchCount,ESP.getFreeHeap(),readyMs,unsigned(screenMode),
            state.writes,state.frames,state.readCalls,state.slices,unsigned(transfer),state.error);
        return;
    }
    if(transfer==Transfer::Usb) return; // the card belongs to the computer until restart
    if(line=="wifi-on") { transferRequest=Transfer::Wifi; return; }
    if(line=="wifi-off") { if(transfer==Transfer::Wifi) transferExitRequested=true; return; }
    if(line=="usb-drive") { transferRequest=Transfer::Usb; return; }
    if(line=="rescan") { if(transfer==Transfer::None) rescanLibrary(); return; }
    if(line=="screen-off") {
        lastActivity=millis()-player::kScreenOffAfterMs; applyScreenPolicy(); return;
    }
    wakeScreen();
    if(line=="pause") sendAction(PAUSE);
    else if(line=="play") sendAction(RESUME);
    else if(line=="next") playTrack(selectedTrack+1);
    else if(line=="prev") playTrack(selectedTrack-1);
    else if(line=="files") lv_tabview_set_act(tabs,1,LV_ANIM_OFF);
    else if(line=="add") lv_tabview_set_act(tabs,2,LV_ANIM_OFF);
    else if(line=="storage" || line=="info") lv_tabview_set_act(tabs,3,LV_ANIM_OFF);
    else if(line=="player") lv_tabview_set_act(tabs,0,LV_ANIM_OFF);
    else if(line=="music" && transfer==Transfer::None) { folder="/Music"; populateBrowser(); lv_tabview_set_act(tabs,1,LV_ANIM_OFF); }
    else if(line=="shot") {
        // Allocate/copy the diagnostic framebuffer only while taking a shot.
        screenPixels=static_cast<uint16_t*>(ps_calloc(240*320,2));
        if(!screenPixels) { Serial.println("FRAME_ERROR no framebuffer memory"); return; }
        refreshPlayer(); lv_obj_invalidate(lv_scr_act()); lv_refr_now(nullptr);
        Serial.println("FRAME_BEGIN 240 320 RGB565BE");
        Serial.write(reinterpret_cast<uint8_t*>(screenPixels),240*320*2); Serial.println("\nFRAME_END");
        free(screenPixels); screenPixels=nullptr;
    }
}
void setup() {
    pinMode(7,OUTPUT); digitalWrite(7,LOW); // Preserve the USB-only test's power behavior.
    pinMode(5,OUTPUT); digitalWrite(5,LOW); Serial.begin(115200);
    lastActivity=millis();
    uiTaskHandle=xTaskGetCurrentTaskHandle();
    audioReady=audioBegin(uiTaskHandle);
    LCD_Init(); Touch_SetWakeTask(xTaskGetCurrentTaskHandle());
    Backlight_Init(); Set_Backlight(player::kBrightness);
    tracks.reserve(player::kMaxTracks); entries.reserve(128);
    createUI(); // A visible UI before card scans or imports; no fixed USB wait.
    setupButtons();
    pinMode(21,OUTPUT); digitalWrite(21,HIGH); delay(10); SD_MMC.setPins(14,17,16);
    sdReady=SD_MMC.begin("/sdcard",true,false,20000,8);
    if(sdReady && SD_MMC.cardType()!=CARD_NONE) {
        scanStartedAt=millis();
        scanMusic("/Music"); // Prioritize songs before unrelated game/save folders.
        scanMusic("/");
        // One filesystem-stat pass; no repeated song hashes on normal boots.
        sdTotal=SD_MMC.totalBytes(); sdUsed=SD_MMC.usedBytes();
        if(tracks.empty()) {
            flashReady=LittleFS.begin(false); importSong();
            if(copiedSong) { tracks.push_back({importPath,true}); sdUsed=SD_MMC.usedBytes(); }
        } else importResult="Songs loaded from microSD.";
    } else { sdReady=false; importResult="microSD unavailable; no formatting performed."; }
    if(tracks.empty()) {
        if(!flashReady) flashReady=LittleFS.begin(false);
        if(flashReady && LittleFS.exists("/test.mp3")) tracks.push_back({"/test.mp3",false});
    }
    std::sort(tracks.begin(),tracks.end(),[](const Track &a,const Track &b){return a.path.compareTo(b.path)<0;});
    const auto found=std::find_if(tracks.begin(),tracks.end(),[](const Track &track){return track.path=="/Music/TLC - No Scrubs.mp3";});
    if(found!=tracks.end()) selectedTrack=found-tracks.begin();
    booting=false; populateBrowser(); updateStorage(); playTrack(selectedTrack);
    if(!audioReady) lv_label_set_text(stateLabel,"Audio task could not start.");
    readyMs=millis(); lastActivity=millis();
    if(Serial) Serial.printf("UI_READY boot_ms=%u songs=%u cpu_mhz=%u\n",readyMs,unsigned(tracks.size()),getCpuFrequencyMhz());
}
void loop() {
    static uint32_t refreshedAt=0; static String command;
    static const bool reserved=command.reserve(128); (void)reserved;
    // Bound host input processing, so a noisy serial client cannot starve touch.
    for(unsigned read=0; read<64 && Serial.available(); ++read) {
        const char c=Serial.read();
        if(c=='\n') { command.trim(); serialCommand(command); command=""; }
        else if(c!='\r' && command.length()<128) command+=c;
    }
    serviceTransfer();
    const bool buttonsPending=pollButtons();
    applyScreenPolicy();
    uint32_t waitMs=player::kTouchFallbackMs;
    if(screenMode==player::ScreenMode::Off) {
        lv_indev_data_t ignored={}; readTouch(nullptr,&ignored);
    }
    if(screenMode!=player::ScreenMode::Off) {
        if(Touch_interrupts && touchDevice) lv_timer_ready(touchDevice->driver->read_timer);
        if(millis()-refreshedAt>=player::kUiUpdateMs) { refreshedAt=millis(); refreshPlayer(); }
        lv_disp_t *display=lv_disp_get_default();
        if(display->inv_p || physicalTouch || lv_anim_count_running()) {
            lv_timer_resume(display->refr_timer);
            if(display->inv_p) lv_timer_ready(display->refr_timer);
        }
        waitMs=lv_timer_handler();
        if(display->inv_p) { // Input callbacks may just have created a dirty region.
            lv_timer_resume(display->refr_timer); lv_timer_ready(display->refr_timer);
            waitMs=lv_timer_handler();
        }
        if(!display->inv_p && !physicalTouch && !lv_anim_count_running()) lv_timer_pause(display->refr_timer);
        const uint32_t elapsed=millis()-refreshedAt;
        const uint32_t uiDelay=elapsed>=player::kUiUpdateMs?0:player::kUiUpdateMs-elapsed;
        if(waitMs>uiDelay) waitMs=uiDelay;
    }
    if(waitMs>player::kTouchFallbackMs) waitMs=player::kTouchFallbackMs;
    if(transfer!=Transfer::None && waitMs>100) waitMs=100; // progress text + timeouts
    if(buttonsPending && waitMs>10) waitMs=10;            // debounce / double-click timing
    if(waitMs<1 || Serial.available()) waitMs=1;
    // Touch ISR wakes this wait immediately; no fixed 3 ms GUI polling loop.
    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(waitMs));
}

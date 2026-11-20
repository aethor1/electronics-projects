// Scoped touchscreen/SD/audio test. Original factory image and speaker test are
// preserved in backups/. No battery/charging/headphone circuit is changed.
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <lvgl.h>
#include <mbedtls/sha256.h>
#include <vector>
#include <algorithm>
#include <string>
#include "AudioEngine.h"
#include "ButtonCore.h"
#include "PlayerCore.h"
#include "PlayerUi.h"
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
static bool transferExitRequested, usbCancelArmed;
static uint32_t transferStartedAt, transferUiAt, usbEjectedAt;
static TaskHandle_t uiTaskHandle;
// On-screen volume, saved in NVS 1.5 s after the last change (not on every slider step).
// Earbuds use their own key, so a speaker-build level never carries over.
static Preferences prefs;
static const char *const kVolumeKey = PLAYER_EARBUDS ? "vol_ear" : "vol";
static uint8_t volumeLevel = player::kDefaultVolume;
static bool volumeDirty;
static uint32_t volumeChangedAt;
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
    if (!audioSend(c)) ui::setMessage("Busy; try again.");
}
static bool playPath(const String &path,bool sd) {
    if (!audioReady || booting || transfer!=Transfer::None || path.length()>=player::kMaxPath) return false;
    AudioCommand c = {}; c.action=START; c.sd=sd;
    strlcpy(c.path,path.c_str(),sizeof(c.path));
    lastActivity=millis();
    return audioSend(c);
}
// The audio task is the only writer of the gain; this just queues the level.
static void setVolumeLevel(uint8_t level) {
    volumeLevel=level>100?100:level;
    AudioCommand c={}; c.action=SET_VOLUME; c.volume=volumeLevel;
    audioSend(c); // if the queue is full, the next change (or the slider release) resends
    volumeDirty=true; volumeChangedAt=millis();
}
static void saveVolumeIfIdle() {
    if(volumeDirty && millis()-volumeChangedAt>=1500) { volumeDirty=false; prefs.putUChar(kVolumeKey,volumeLevel); }
}
static void loadVolume() {
    prefs.begin("player",false);
    uint8_t level=prefs.getUChar(kVolumeKey,player::kDefaultVolume);
    if(PLAYER_EARBUDS && level>player::kDefaultVolume) level=player::kDefaultVolume; // never boot louder in earbuds
    setVolumeLevel(level); volumeDirty=false;
}
static void playTrack(int index) {
    if (booting || tracks.empty()) return;
    selectedTrack = (index + int(tracks.size())) % tracks.size();
    if (!playPath(tracks[selectedTrack].path,tracks[selectedTrack].sd) && uiReady)
        ui::setMessage("Cannot queue this song.");
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
static void populateBrowser();
static void openItem(size_t index) {
    if (index>=entries.size() || transfer!=Transfer::None) return;
    Entry entry=entries[index];
    if (entry.directory) { folder=entry.path; populateBrowser(); return; }
    if (isMp3(entry.path)) {
        auto found=std::find_if(tracks.begin(),tracks.end(),[&](const Track &t){return t.sd&&t.path==entry.path;});
        if (found==tracks.end() && tracks.size()<player::kMaxTracks) {
            tracks.push_back({entry.path,true}); found=tracks.end()-1;
        }
        if (found!=tracks.end()) playTrack(found-tracks.begin());
        else if (!playPath(entry.path,true)) ui::setMessage("Song path too long or player busy.");
        ui::showTab(ui::kPlayer);
    } else {
        String text=String("microSD: ")+entry.path+"\n"+String(entry.size)+" bytes";
        text += "\n\nThis player plays MP3 files.";
        ui::showDetails("File details",text.c_str());
    }
}
static void populateBrowser() {
    entries.clear();
    if (!sdReady) { ui::setFiles(folder.c_str(),{},booting?"Reading microSD...":"microSD could not be mounted."); return; }
    File dir=SD_MMC.open(folder);
    if (!dir || !dir.isDirectory()) { ui::setFiles(folder.c_str(),{},"Folder unavailable"); return; }
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
    const String playing=snapshot().path;
    std::vector<ui::FileItem> items; items.reserve(entries.size());
    for (const Entry &e : entries) items.push_back({basenameOf(e.path).c_str(),e.directory,!e.directory && e.path==playing});
    String count=String(entries.size())+" ITEMS - TAP A SONG TO PLAY";
    if(entries.size()==128) count="FIRST 128 ITEMS SHOWN";
    ui::setFiles(folder.c_str(),items,entries.empty()?"This folder is empty.":count.c_str());
}
static void createUI() {
    lv_init(); lv_disp_draw_buf_init(&drawBuffer,drawPixels,nullptr,240*24);
    static lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res=240; display.ver_res=320; display.draw_buf=&drawBuffer; display.flush_cb=flushDisplay;
    lv_disp_t *disp=lv_disp_drv_register(&display);
    // Same theme call as tools/ui_preview; PlayerUi.cpp styles everything on top of it.
    lv_disp_set_theme(disp,lv_theme_default_init(disp,lv_color_hex(0x9BBC0F),lv_color_hex(0xB0306A),true,&lv_font_montserrat_14));
    static lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type=LV_INDEV_TYPE_POINTER; input.read_cb=readTouch; touchDevice=lv_indev_drv_register(&input);
    lv_timer_set_period(input.read_timer,player::kTouchFallbackMs);
    ui::Actions actions{};
    actions.previous=[]{ playTrack(selectedTrack-1); };
    actions.playPause=[]{ sendAction(TOGGLE); };
    actions.next=[]{ playTrack(selectedTrack+1); };
    actions.volume=[](uint8_t level){ lastActivity=millis(); setVolumeLevel(level); };
    actions.openItem=openItem;
    actions.folderUp=[]{
        int slash=folder.lastIndexOf('/'); folder=slash<=0?"/":folder.substring(0,slash); populateBrowser();
    };
    actions.addWifi=[]{ transferRequest=Transfer::Wifi; };
    actions.addUsb=[]{ transferRequest=Transfer::Usb; };
    actions.overlayDone=[]{ transferExitRequested=true; };
    ui::create(actions,usbdrive::supported(),volumeLevel);
    populateBrowser();
    uiReady=true;
    lv_refr_now(nullptr);
}
static void updateStorage() {
    ui::StorageInfo info;
    info.sdReady=sdReady; info.totalBytes=sdTotal; info.usedBytes=sdUsed; info.songs=tracks.size();
    info.note=importResult.c_str();
    if(copiedSong) info.note+=std::string(" ")+importPath.c_str();
    if(scanLimited) info.note+=" More files in Library.";
    info.psramMiB=ESP.getPsramSize()/1048576.0f;
    ui::setStorage(info);
}
static void refreshPlayer() {
    if(booting || screenMode==player::ScreenMode::Off || ui::activeTab()!=ui::kPlayer) return;
    const AudioState state=snapshot();
    static char oldPath[player::kMaxPath]={}, title[128]="", artist[96]="";
    static bool oldSd=false, cached=false;
    if(!cached || strcmp(oldPath,state.path) || oldSd!=state.sd) {
        cached=true;
        strlcpy(oldPath,state.path,sizeof(oldPath)); oldSd=state.sd;
        String name=basenameOf(String(state.path));
        if(isMp3(name)) name.remove(name.length()-4);
        if(!name.length()) { strlcpy(title,"No MP3 selected",sizeof(title)); artist[0]=0; }
        else player::splitTrackName(name.c_str(),artist,sizeof(artist),title,sizeof(title));
        if(!artist[0] && name.length()) { // no "Artist - Title": show where it lives
            const String path=state.path; const int slash=path.lastIndexOf('/');
            const String parent=slash>0 ? basenameOf(path.substring(0,slash)) : "";
            strlcpy(artist,!state.sd?"Internal flash":parent.length()?parent.c_str():"microSD",sizeof(artist));
        }
    }
    ui::NowPlaying now;
    now.title=title; now.artist=state.failed?state.error:artist;
    now.state=state.failed?"ERROR":state.paused?"PAUSED":state.running?"PLAYING":"STOPPED";
    char detail[16]; snprintf(detail,sizeof(detail),state.bytes?"%.2f MB":"",state.bytes/1e6); now.detail=detail;
    now.playing=state.running&&!state.paused; now.elapsed=state.elapsed;
    now.permille=state.bytes?uint64_t(state.position)*1000/state.bytes:0;
    now.tracks=tracks.size();
    now.track=selectedTrack>=0 && selectedTrack<int(tracks.size()) && tracks[selectedTrack].path==state.path ? selectedTrack+1 : 0;
    if(!now.track) now.tracks=0;
    ui::setNowPlaying(now);
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
static void stopWifi();
static void updateWifiOverlay(const upload::Status &s) {
    char line[112]="", corner[16];
    const uint32_t left=player::sessionSecondsLeft(millis(),s.lastActivityMs,player::kUploadIdleTimeoutMs);
    snprintf(corner,sizeof(corner),"%u:%02u",unsigned(left/60),unsigned(left%60));
    if(s.receiving && s.bytesExpected)
        snprintf(line,sizeof(line),"%u%%  %s",unsigned(uint64_t(s.bytesReceived)*100/s.bytesExpected),s.currentFile);
    else if(s.lastMessage[0]) snprintf(line,sizeof(line),"%s",s.lastMessage);
    else if(s.filesSaved) snprintf(line,sizeof(line),"%u added",unsigned(s.filesSaved));
    ui::setWifiStatus(line,corner);
    // Once a phone has joined, move on to the page code (unless someone flipped it by hand).
    if(s.stations) ui::stationJoined();
}
static void startWifi() {
    if(!sdReady) { ui::setAddHint("No microSD card, so there is nowhere to save songs."); return; }
    sendAction(PAUSE); // Wi-Fi runs on the audio core; pause instead of risking dropouts.
    transfer=Transfer::Wifi; transferExitRequested=false;
    transferStartedAt=transferUiAt=millis();
    ui::openWifi(player::kWifiName,player::kWifiPassword,upload::kUrl); lv_refr_now(nullptr);
    const bool ok=upload::begin();
    Serial.printf("WIFI_UPLOAD %s heap=%u\n",ok?"ON":"FAILED",ESP.getFreeHeap());
    if(!ok) { stopWifi(); ui::setAddHint("Wi-Fi could not start. Try again."); }
}
static void stopWifi() {
    const upload::Status s=upload::status();
    upload::end();
    ui::closeOverlay(); transfer=Transfer::None; transferExitRequested=false;
    if(s.filesSaved) rescanLibrary();
    else { sdUsed=SD_MMC.usedBytes(); updateStorage(); }
    lastActivity=millis();
    Serial.printf("WIFI_UPLOAD OFF added=%u\n",unsigned(s.filesSaved));
}
static void startUsb() {
    if(!usbdrive::supported()) return;
    if(!sdReady) { ui::setAddHint("No microSD card to share over USB."); return; }
    // The computer will write raw sectors: the audio task must close its song file first.
    if(audioReady) {
        const uint32_t before=snapshot().releases;
        AudioCommand c={}; c.action=RELEASE;
        bool released=audioSend(c);
        for(int i=0; released && i<200 && snapshot().releases==before; ++i) delay(10);
        if(!released || snapshot().releases==before) { ui::setAddHint("Player busy; try USB again."); return; }
    }
    transfer=Transfer::Usb; usbCancelArmed=false; transferExitRequested=false; usbEjectedAt=0;
    transferStartedAt=transferUiAt=millis();
    ui::openUsb(); lv_refr_now(nullptr);
    Serial.println("USB_DRIVE ON (this serial port disappears until the player restarts)");
    Serial.flush(); delay(50);
    const char *error=nullptr;
    if(!usbdrive::begin(&error)) ui::setUsbStatus(error?error:"USB drive could not start.");
}
static void serviceUsb() {
    const uint32_t now=millis();
    if(usbdrive::ejected()) {
        if(!usbEjectedAt) { usbEjectedAt=now; ui::setUsbStatus("Ejected. Restarting with the new songs..."); }
        else if(now-usbEjectedAt>=1500) usbdrive::restartPlayer();
        return;
    }
    if(transferExitRequested) {
        transferExitRequested=false;
        if(usbdrive::hostAccessed() && !usbCancelArmed) {
            usbCancelArmed=true;
            ui::setUsbStatus("Eject it on the computer first! Tap Cancel again to restart anyway.");
        } else usbdrive::restartPlayer();
        return;
    }
    if(!usbdrive::hostAccessed() && player::sessionExpired(now,transferStartedAt,player::kUploadIdleTimeoutMs))
        usbdrive::restartPlayer(); // tapped by accident, never plugged in
    if(!usbCancelArmed)
        ui::setUsbStatus(usbdrive::hostAccessed() ? "Computer connected. Eject it there when done." :
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
    else if(action==A::VolumeUp || action==A::VolumeDown) {
        const int step=action==A::VolumeUp ? player::kVolumeStep : -int(player::kVolumeStep);
        setVolumeLevel(uint8_t(std::max(0,std::min(100,int(volumeLevel)+step)))); ui::setVolume(volumeLevel);
    }
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
        const upload::Status wifi=upload::status();
        Serial.printf("STATUS storage=%s path=%s state=%s position=%u/%u elapsed=%u flushes=%u touches=%u heap=%u boot_ms=%u screen=%u i2s_writes=%u frames=%u read_refills=%u audio_slices=%u transfer=%u volume=%u gain_q12=%u http_requests=%u stations=%u error=%s\n",
            state.sd?"microSD":"internal",state.path,state.failed?"failed":state.paused?"paused":state.running?"playing":"stopped",
            state.position,state.bytes,state.elapsed,flushCount,touchCount,ESP.getFreeHeap(),readyMs,unsigned(screenMode),
            state.writes,state.frames,state.readCalls,state.slices,unsigned(transfer),unsigned(state.volume),unsigned(state.gainQ12),
            unsigned(wifi.httpRequests),unsigned(wifi.stations),state.error);
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
    else if(line=="files") ui::showTab(ui::kLibrary);
    else if(line=="add") ui::showTab(ui::kAdd);
    else if(line=="storage" || line=="info") ui::showTab(ui::kInfo);
    else if(line=="player") ui::showTab(ui::kPlayer);
    else if(line=="music" && transfer==Transfer::None) { folder="/Music"; populateBrowser(); ui::showTab(ui::kLibrary); }
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
    loadVolume();
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
    if(!audioReady) ui::setMessage("Audio task could not start.");
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
    saveVolumeIfIdle();
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
    if(volumeDirty && waitMs>100) waitMs=100;              // save the volume once it settles
    if(waitMs<1 || Serial.available()) waitMs=1;
    // Touch ISR wakes this wait immediately; no fixed 3 ms GUI polling loop.
    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(waitMs));
}

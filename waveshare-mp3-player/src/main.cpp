// Scoped touchscreen/SD/audio test. Original factory image and speaker test are
// preserved in backups/. No battery/charging/headphone circuit is changed.
#include <Arduino.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <lvgl.h>
#include <AudioFileSourceFS.h>
#include <AudioFileSourceID3.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <mbedtls/sha256.h>
#include <driver/i2s.h>
#include <vector>
#include <algorithm>
#include "vendor/Display_ST7789.h"

static bool sdReady, flashReady, copiedSong;
static uint64_t sdTotal, sdUsed;
static String importPath, importResult;
static constexpr char expectedHash[] = "700c4d7edc15c1d1a1fd8d69c93a7ae282ffd17c1aa626594aa25329b99adf18";
static lv_obj_t *tabs, *titleLabel, *pathLabel, *stateLabel, *positionBar;
static lv_obj_t *playText, *browserList, *folderLabel, *fileCountLabel;
static uint16_t *screenPixels;
static uint32_t flushCount, touchCount;
static lv_disp_draw_buf_t drawBuffer;
static lv_color_t drawPixels[240 * 24];
struct Track { String path; bool sd; };
struct Entry { String path; bool directory; uint32_t size; };
static std::vector<Track> tracks;
static std::vector<Entry> entries;
static int selectedTrack = 0;
static String folder = "/";
enum Action : uint8_t { START, TOGGLE, PAUSE, RESUME };
struct AudioCommand { Action action; bool sd; char path[256]; };
struct AudioState {
    bool running, paused, sd, failed;
    uint32_t bytes, position, elapsed;
    char path[256];
};
static AudioState audioState = {};
static SemaphoreHandle_t stateLock;
static QueueHandle_t commands;
static String basenameOf(String path) { return path.substring(path.lastIndexOf('/') + 1); }
static bool isMp3(String path) { path.toLowerCase(); return path.endsWith(".mp3"); }
static AudioState snapshot() {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    AudioState s = audioState;
    xSemaphoreGive(stateLock);
    return s;
}
static String hashFile(fs::FS &fs, const char *path) {
    File file = fs.open(path, FILE_READ);
    if (!file || file.isDirectory()) return "";
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    static uint8_t data[4096];
    uint8_t digest[32];
    size_t count = 0;
    while (file.available()) {
        int n = file.read(data, sizeof(data));
        if (n <= 0) break;
        count += n;
        mbedtls_sha256_update_ret(&ctx, data, n);
        delay(0);
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
    static uint8_t buffer[4096];
    while (source.available()) {
        int n = source.read(buffer, sizeof(buffer));
        if (n <= 0 || dest.write(buffer, n) != size_t(n)) break;
        written += n; delay(0);
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
static void scanMusic(const String &path, int depth=0) {
    if (depth > 4 || tracks.size() >= 128) return;
    File directory = SD_MMC.open(path);
    if (!directory || !directory.isDirectory()) return;
    File file;
    while ((file = directory.openNextFile()) && tracks.size() < 128) {
        String full = path == "/" ? "/" + basenameOf(file.name()) : path + "/" + basenameOf(file.name());
        bool subdir = file.isDirectory(); file.close();
        if (subdir && !basenameOf(full).startsWith(".")) scanMusic(full, depth+1);
        else if (!subdir && isMp3(full)) tracks.push_back({full, true});
    }
    directory.close();
}
static void audioTask(void *) {
    AudioFileSourceFS *source = nullptr;
    AudioFileSourceID3 *tags = nullptr;
    AudioFileSourceBuffer *buffer = nullptr;
    AudioGeneratorMP3 *decoder = nullptr;
    AudioOutputI2S output(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_DISABLE);
    output.SetPinout(48, 38, 47); output.SetGain(0.08f); output.SetOutputModeMono(true);
    AudioState state = {};
    uint32_t lastTick = millis(), activeMs = 0, publishAt = 0;
    auto stop = [&]() {
        if (decoder) {
            decoder->stop();
            // The pinned output library stops DMA but retains the driver.
            // Release it before restarting another track on the same I2S port.
            i2s_driver_uninstall(I2S_NUM_0);
        }
        delete decoder; decoder = nullptr;
        delete buffer; buffer = nullptr;
        delete tags; tags = nullptr;
        delete source; source = nullptr;
    };
    auto start = [&](const char *path, bool sd) {
        stop(); state = {}; state.sd = sd;
        strlcpy(state.path, path, sizeof(state.path));
        fs::FS &fs = sd ? static_cast<fs::FS&>(SD_MMC) : static_cast<fs::FS&>(LittleFS);
        source = new AudioFileSourceFS(fs, path);
        if (source->isOpen()) {
            state.bytes = source->getSize();
            tags = new AudioFileSourceID3(source);
            buffer = new AudioFileSourceBuffer(tags, 16384);
            decoder = new AudioGeneratorMP3();
            state.running = decoder->begin(buffer, &output);
        }
        state.failed = !state.running; activeMs = 0; lastTick = millis();
        Serial.printf("AUDIO_START storage=%s path=%s bytes=%u decoder=%s\n", sd ? "microSD" : "internal", path, state.bytes, state.running ? "OK" : "FAILED");
    };
    for (;;) {
        AudioCommand c;
        while (xQueueReceive(commands, &c, 0) == pdTRUE) {
            if (c.action == START) start(c.path, c.sd);
            else if (!state.running && state.path[0] && c.action != PAUSE) {
                char path[256]; strlcpy(path, state.path, sizeof(path)); bool sd = state.sd; start(path, sd);
            } else if (state.running) {
                if (c.action == TOGGLE) state.paused = !state.paused;
                if (c.action == PAUSE) state.paused = true;
                if (c.action == RESUME) state.paused = false;
                Serial.printf("AUDIO_CONTROL %s\n", state.paused ? "paused" : "playing");
            }
        }
        uint32_t now = millis();
        if (state.running && !state.paused) activeMs += now - lastTick;
        lastTick = now;
        if (state.running && !state.paused && decoder && !decoder->loop()) {
            decoder->stop(); state.running = false; Serial.println("AUDIO_FINISHED");
        }
        if (source && source->isOpen()) state.position = source->getPos();
        if (!state.running && !state.failed) state.position = state.bytes;
        state.elapsed = activeMs / 1000;
        if (millis() - publishAt >= 50) {
            publishAt = millis();
            xSemaphoreTake(stateLock, portMAX_DELAY); audioState = state; xSemaphoreGive(stateLock);
        }
        vTaskDelay(pdMS_TO_TICKS(state.running && !state.paused ? 1 : 5));
    }
}
static void sendAction(Action action) {
    AudioCommand c = {}; c.action = action; xQueueSend(commands, &c, 0);
}
static void playTrack(int index) {
    if (tracks.empty()) return;
    selectedTrack = (index + int(tracks.size())) % tracks.size();
    AudioCommand c = {}; c.action = START; c.sd = tracks[selectedTrack].sd;
    strlcpy(c.path, tracks[selectedTrack].path.c_str(), sizeof(c.path)); xQueueSend(commands, &c, 0);
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
    uint16_t x[5]={}, y[5]={}, strength[5]={}; uint8_t points=0;
    Touch_Read_Data();
    bool pressed = Touch_Get_XY(x, y, strength, &points, 5) && points && x[0]<240 && y[0]<320;
    static bool wasPressed = false;
    if (pressed) {
        data->point.x=x[0]; data->point.y=y[0]; data->state=LV_INDEV_STATE_PR;
        if (!wasPressed) { ++touchCount; Serial.printf("TOUCH x=%u y=%u\n", x[0], y[0]); }
    } else data->state=LV_INDEV_STATE_REL;
    wasPressed = pressed;
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
        if (found==tracks.end()) { tracks.push_back({entry.path,true}); found=tracks.end()-1; }
        playTrack(found-tracks.begin()); lv_tabview_set_act(tabs,0,LV_ANIM_OFF);
        Serial.printf("UI_FILE_PLAY %s\n",entry.path.c_str());
    } else showDetails(entry);
}
static void populateBrowser() {
    lv_obj_clean(browserList); entries.clear(); lv_label_set_text(folderLabel,folder.c_str());
    if (!sdReady) {
        lv_list_add_text(browserList,"microSD could not be mounted.");
        lv_label_set_text(fileCountLabel,"No files changed; no format performed."); return;
    }
    File dir=SD_MMC.open(folder);
    if (!dir || !dir.isDirectory()) { lv_list_add_text(browserList,"Folder unavailable"); return; }
    File f;
    while ((f=dir.openNextFile()) && entries.size()<128) {
        String name=basenameOf(f.name()); String path=folder=="/" ? "/"+name : folder+"/"+name;
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
    input.type=LV_INDEV_TYPE_POINTER; input.read_cb=readTouch; lv_indev_drv_register(&input);
    tabs=lv_tabview_create(lv_scr_act(),LV_DIR_BOTTOM,42);
    lv_obj_t *player=lv_tabview_add_tab(tabs,"Player");
    lv_obj_t *files=lv_tabview_add_tab(tabs,"Files");
    lv_obj_t *storage=lv_tabview_add_tab(tabs,"Storage");
    for(lv_obj_t *page : {player,files,storage}) {
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
    label(storage,"STORAGE",0,0,210,0x72BEDD);
    String info=sdReady ? "microSD mounted\n"+String(double(sdTotal)/1e9,2)+" GB usable\n"+String(double(sdUsed)/1e6,2)+" MB used\n"+String(double(sdTotal-sdUsed)/1e9,2)+" GB free" : "microSD unavailable\nNo format performed";
    info+="\n\n"+importResult;
    if(copiedSong) info+="\n"+importPath+"\n4,007,763 bytes";
    info+="\n\nInternal flash: 16 MiB\nPSRAM: "+String(ESP.getPsramSize()/1048576.0,1)+" MiB\nUSB-powered speaker test";
    label(storage,info.c_str(),0,29,210); lv_refr_now(nullptr);
}
static void refreshPlayer() {
    AudioState s=snapshot(); String name=basenameOf(s.path);
    if(name.endsWith(".mp3")) name.remove(name.length()-4);
    lv_label_set_text(titleLabel,name.length()?name.c_str():"No MP3 selected");
    String origin=String(s.sd?"microSD: ":"Internal flash: ")+s.path;
    lv_label_set_text(pathLabel,origin.c_str());
    char text[90];
    snprintf(text,sizeof(text),"%s  %u:%02u  |  %.2f MB",s.failed?"Failed":s.paused?"Paused":s.running?"Playing":"Stopped",s.elapsed/60,s.elapsed%60,s.bytes/1e6);
    lv_label_set_text(stateLabel,text); lv_label_set_text(playText,s.running&&!s.paused?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
    lv_bar_set_value(positionBar,s.bytes?uint64_t(s.position)*1000/s.bytes:0,LV_ANIM_OFF);
}
static void serialCommand(const String &line) {
    if(line=="pause") sendAction(PAUSE);
    else if(line=="play") sendAction(RESUME);
    else if(line=="next") playTrack(selectedTrack+1);
    else if(line=="prev") playTrack(selectedTrack-1);
    else if(line=="files") lv_tabview_set_act(tabs,1,LV_ANIM_OFF);
    else if(line=="storage") lv_tabview_set_act(tabs,2,LV_ANIM_OFF);
    else if(line=="player") lv_tabview_set_act(tabs,0,LV_ANIM_OFF);
    else if(line=="music") { folder="/Music"; populateBrowser(); lv_tabview_set_act(tabs,1,LV_ANIM_OFF); }
    else if(line=="shot" && screenPixels) {
        lv_refr_now(nullptr); Serial.println("FRAME_BEGIN 240 320 RGB565BE");
        Serial.write(reinterpret_cast<uint8_t*>(screenPixels),240*320*2); Serial.println("\nFRAME_END");
    }
    else if(line=="status") {
        AudioState s=snapshot();
        Serial.printf("STATUS storage=%s path=%s state=%s position=%u/%u elapsed=%u flushes=%u touches=%u heap=%u\n",s.sd?"microSD":"internal",s.path,s.paused?"paused":s.running?"playing":"stopped",s.position,s.bytes,s.elapsed,flushCount,touchCount,ESP.getFreeHeap());
    }
}
void setup() {
    pinMode(7,OUTPUT); digitalWrite(7,LOW); // Preserve the USB-only test's power behavior.
    pinMode(5,OUTPUT); digitalWrite(5,LOW); Serial.begin(115200); delay(1500);
    Serial.printf("\nWAVESHARE SD PLAYER TEST | flash=%u PSRAM=%u\n",ESP.getFlashChipSize(),ESP.getPsramSize());
    screenPixels=static_cast<uint16_t*>(ps_calloc(240*320,2));
    LCD_Init(); Backlight_Init(); Set_Backlight(50);
    flashReady=LittleFS.begin(false);
    pinMode(21,OUTPUT); digitalWrite(21,HIGH); delay(10); SD_MMC.setPins(14,17,16);
    sdReady=SD_MMC.begin("/sdcard",true,false,20000,8);
    if(sdReady && SD_MMC.cardType()!=CARD_NONE) {
        sdTotal=SD_MMC.totalBytes(); sdUsed=SD_MMC.usedBytes();
        Serial.printf("SD_MOUNT_OK card_bytes=%llu filesystem_bytes=%llu used_bytes=%llu\n",SD_MMC.cardSize(),sdTotal,sdUsed);
        File root=SD_MMC.open("/"); File f;
        while((f=root.openNextFile())) {
            Serial.printf("SD_EXISTING %s %s bytes=%u\n",f.isDirectory()?"DIR":"FILE",f.name(),unsigned(f.size())); f.close();
        }
        root.close(); importSong(); sdUsed=SD_MMC.usedBytes(); scanMusic("/");
    } else { sdReady=false; importResult="microSD mount failed. Card not formatted."; Serial.println("SD_MOUNT_FAILED no formatting performed"); }
    if(copiedSong) {
        auto found=std::find_if(tracks.begin(),tracks.end(),[](const Track &t){return t.path==importPath;});
        if(found!=tracks.end()) selectedTrack=found-tracks.begin();
    }
    if(tracks.empty() && flashReady && LittleFS.exists("/test.mp3")) tracks.push_back({"/test.mp3",false});
    Serial.printf("PLAYLIST mp3_count=%u import=%s\n",unsigned(tracks.size()),importResult.c_str());
    commands=xQueueCreate(6,sizeof(AudioCommand)); stateLock=xSemaphoreCreateMutex();
    createUI(); xTaskCreatePinnedToCore(audioTask,"mp3",12288,nullptr,2,nullptr,0); playTrack(selectedTrack);
    Serial.println("UI_READY tabs=Player,Files,Storage backlight=50% commands=status,pause,play,next,prev,files,music,storage,player,shot");
}
void loop() {
    static uint32_t refreshedAt=0, reportedAt=0; static String command;
    while(Serial.available()) {
        char c=Serial.read();
        if(c=='\n') { command.trim(); serialCommand(command); command=""; }
        else if(c!='\r' && command.length()<128) command+=c;
    }
    if(millis()-refreshedAt>=300) { refreshedAt=millis(); refreshPlayer(); }
    lv_timer_handler();
    if(millis()-reportedAt>=5000) { reportedAt=millis(); serialCommand("status"); }
    delay(3);
}

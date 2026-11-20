#include "PlayerUi.h"
#include <stdio.h>
#include <string.h>
#include "TransferCore.h"

// Look: a small handheld. Dark body, a pea-green "LCD" window for Now Playing, magenta
// A/B-style play button, pixel font (unscii) for labels. Fixed 240x320 layout.
namespace ui {
namespace {

constexpr uint32_t kBody = 0x111513, kPanel = 0x1C2220, kPanelHi = 0x283029, kLine = 0x2D3631;
constexpr uint32_t kInk = 0xE9EEE6, kMuted = 0x8E9A92;
constexpr uint32_t kLcd = 0x9BBC0F, kLcdMid = 0x8BAC0F, kLcdDark = 0x306230, kLcdInk = 0x0F380F;
constexpr uint32_t kAccent = 0xB0306A, kAccentDown = 0x8A2453;
constexpr int kW = 240, kHeaderH = 26, kNavY = 272;

Actions act;
lv_obj_t *header, *headerState, *pages[4], *navPills[4], *navIcons[4], *navTexts[4];
lv_obj_t *lcdState, *lcdTrack, *titleLabel, *artistLabel, *progress, *elapsedLabel, *detailLabel, *playIcon;
lv_obj_t *volumeSlider, *volumeValue;
lv_obj_t *folderLabel, *upButton, *fileList, *fileFooter;
lv_obj_t *freeLabel, *storageBar, *usedLabel, *songsLabel, *noteLabel, *deviceLabel;
lv_obj_t *addHint;
lv_obj_t *overlay, *qr, *qrLine1, *qrLine2, *overlayCorner, *overlayTitle, *qrToggle, *usbStatus;
Tab current = kPlayer;
bool wifiPage, userPickedQr;
std::string wifiSsid, wifiPassword, wifiUrl;
lv_style_t rowStyle, rowPressed, rowText;

const char *const kTitles[] = {"NOW PLAYING", "LIBRARY", "ADD MUSIC", "INFO"};
const char *const kNavIcons[] = {LV_SYMBOL_AUDIO, LV_SYMBOL_LIST, LV_SYMBOL_DOWNLOAD, LV_SYMBOL_SD_CARD};
const char *const kNavTexts[] = {"PLAY", "LIBRARY", "ADD", "INFO"};

lv_color_t hex(uint32_t c) { return lv_color_hex(c); }
void setText(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
lv_obj_t *text(lv_obj_t *parent, const char *t, const lv_font_t *font, uint32_t color, int x, int y, int w,
               lv_label_long_mode_t mode = LV_LABEL_LONG_WRAP) {
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_long_mode(o, mode);
    lv_label_set_text(o, t);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, hex(color), 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    return o;
}
lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, uint32_t down, int radius, lv_event_cb_t cb) {
    lv_obj_t *b = box(parent, x, y, w, h, bg, radius);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, hex(down), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}
lv_obj_t *centeredText(lv_obj_t *parent, const char *t, const lv_font_t *font, uint32_t color) {
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, t);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, hex(color), 0);
    lv_obj_center(o);
    return o;
}
// Pill button with a caption; used at the bottom of the overlays.
lv_obj_t *pillButton(lv_obj_t *parent, const char *caption, int x, int y, int w, bool primary, lv_event_cb_t cb) {
    lv_obj_t *b = button(parent, x, y, w, 34, primary ? kAccent : kPanelHi, primary ? kAccentDown : kLine, 17, cb);
    return centeredText(b, caption, &lv_font_montserrat_14, kInk);
}

void formatTime(char *out, size_t size, uint32_t seconds) {
    snprintf(out, size, "%u:%02u", unsigned(seconds / 60), unsigned(seconds % 60));
}

// ---- Chrome: header + bottom navigation ---------------------------------------------
void createChrome() {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, hex(kBody), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    header = text(screen, kTitles[0], &lv_font_unscii_16, kLcd, 10, 6, 180, LV_LABEL_LONG_CLIP);
    headerState = text(screen, "", &lv_font_montserrat_14, kMuted, 190, 5, 40, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(headerState, LV_TEXT_ALIGN_RIGHT, 0);
    box(screen, 0, kHeaderH - 1, kW, 1, kLine, 0);
    lv_obj_t *nav = box(screen, 0, kNavY, kW, 320 - kNavY, kPanel, 0);
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *item = button(nav, i * 60, 0, 60, 48, kPanel, kPanel, 0, nullptr);
        lv_obj_add_event_cb(item, [](lv_event_t *e) {
            showTab(Tab(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
        }, LV_EVENT_CLICKED, reinterpret_cast<void *>(uintptr_t(i)));
        navPills[i] = box(item, 12, 5, 36, 22, kPanel, 11);
        lv_obj_clear_flag(navPills[i], LV_OBJ_FLAG_CLICKABLE);
        navIcons[i] = centeredText(navPills[i], kNavIcons[i], &lv_font_montserrat_16, kMuted);
        navTexts[i] = text(item, kNavTexts[i], &lv_font_unscii_8, kMuted, 0, 32, 60, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(navTexts[i], LV_TEXT_ALIGN_CENTER, 0);
    }
}
lv_obj_t *page() {
    lv_obj_t *p = box(lv_scr_act(), 0, kHeaderH, kW, kNavY - kHeaderH, kBody, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

// ---- Now Playing ------------------------------------------------------------------
void volumeEvent(lv_event_t *) {
    const uint8_t level = uint8_t(lv_slider_get_value(volumeSlider));
    char value[8];
    snprintf(value, sizeof(value), "%u", unsigned(level));
    setText(volumeValue, value);
    if (act.volume) act.volume(level);
}
void createPlayer(lv_obj_t *p, uint8_t volume) {
    // Bezel + green LCD window.
    lv_obj_t *bezel = box(p, 8, 6, 224, 130, kPanel, 12);
    lv_obj_t *lcd = box(bezel, 8, 8, 208, 114, kLcd, 4);
    lcdState = text(lcd, "", &lv_font_unscii_8, kLcdDark, 10, 8, 120, LV_LABEL_LONG_CLIP);
    lcdTrack = text(lcd, "", &lv_font_unscii_8, kLcdDark, 118, 8, 80, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(lcdTrack, LV_TEXT_ALIGN_RIGHT, 0);
    titleLabel = text(lcd, "Loading music...", &lv_font_montserrat_20, kLcdInk, 10, 22, 188, LV_LABEL_LONG_DOT);
    lv_obj_set_height(titleLabel, 48);
    lv_obj_set_style_text_line_space(titleLabel, -2, 0);
    artistLabel = text(lcd, "", &lv_font_montserrat_14, kLcdDark, 10, 70, 188, LV_LABEL_LONG_DOT);
    lv_obj_set_height(artistLabel, 17);
    progress = lv_bar_create(lcd);
    lv_obj_remove_style_all(progress);
    lv_obj_set_pos(progress, 10, 91);
    lv_obj_set_size(progress, 188, 5);
    lv_obj_set_style_bg_color(progress, hex(kLcdMid), 0);
    lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(progress, hex(kLcdInk), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(progress, 0, 1000);
    elapsedLabel = text(lcd, "0:00", &lv_font_unscii_8, kLcdInk, 10, 100, 90, LV_LABEL_LONG_CLIP);
    detailLabel = text(lcd, "", &lv_font_unscii_8, kLcdDark, 98, 100, 100, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(detailLabel, LV_TEXT_ALIGN_RIGHT, 0);

    // Transport: dark round prev/next, magenta play (the "A" button).
    lv_obj_t *prev = button(p, 30, 158, 48, 48, kPanelHi, kLine, 24, [](lv_event_t *) { if (act.previous) act.previous(); });
    centeredText(prev, LV_SYMBOL_PREV, &lv_font_montserrat_20, kInk);
    lv_obj_t *play = button(p, 88, 150, 64, 64, kAccent, kAccentDown, 32, [](lv_event_t *) { if (act.playPause) act.playPause(); });
    lv_obj_set_style_shadow_color(play, hex(0x000000), 0);
    lv_obj_set_style_shadow_width(play, 0, 0);
    playIcon = centeredText(play, LV_SYMBOL_PLAY, &lv_font_montserrat_28, kInk);
    lv_obj_t *next = button(p, 162, 158, 48, 48, kPanelHi, kLine, 24, [](lv_event_t *) { if (act.next) act.next(); });
    centeredText(next, LV_SYMBOL_NEXT, &lv_font_montserrat_20, kInk);

    // Volume: icon, slider, number. Big invisible touch margin around the thin track.
    text(p, LV_SYMBOL_VOLUME_MID, &lv_font_montserrat_16, kMuted, 12, 222, 24, LV_LABEL_LONG_CLIP);
    volumeSlider = lv_slider_create(p);
    lv_obj_remove_style_all(volumeSlider);
    lv_obj_set_pos(volumeSlider, 42, 228);
    lv_obj_set_size(volumeSlider, 152, 6);
    lv_obj_set_ext_click_area(volumeSlider, 14);
    lv_obj_set_style_bg_color(volumeSlider, hex(kPanelHi), 0);
    lv_obj_set_style_bg_opa(volumeSlider, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(volumeSlider, 3, 0);
    lv_obj_set_style_bg_color(volumeSlider, hex(kLcd), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(volumeSlider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(volumeSlider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volumeSlider, hex(kInk), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(volumeSlider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(volumeSlider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(volumeSlider, 6, LV_PART_KNOB);
    lv_slider_set_range(volumeSlider, 0, 100);
    lv_obj_add_event_cb(volumeSlider, volumeEvent, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(volumeSlider, volumeEvent, LV_EVENT_RELEASED, nullptr);
    volumeValue = text(p, "", &lv_font_unscii_8, kMuted, 202, 227, 30, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(volumeValue, LV_TEXT_ALIGN_RIGHT, 0);
    setVolume(volume);
}

// ---- Library ------------------------------------------------------------------------
void createLibrary(lv_obj_t *p) {
    upButton = button(p, 8, 6, 44, 26, kPanelHi, kLine, 13, [](lv_event_t *) { if (act.folderUp) act.folderUp(); });
    centeredText(upButton, LV_SYMBOL_LEFT, &lv_font_montserrat_14, kInk);
    folderLabel = text(p, "/", &lv_font_montserrat_14, kInk, 60, 10, 172, LV_LABEL_LONG_DOT);
    fileList = lv_obj_create(p);
    lv_obj_remove_style_all(fileList);
    lv_obj_set_pos(fileList, 0, 38);
    lv_obj_set_size(fileList, kW, 186);
    lv_obj_set_flex_flow(fileList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(fileList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(fileList, LV_SCROLLBAR_MODE_ACTIVE);
    fileFooter = text(p, "", &lv_font_unscii_8, kMuted, 10, 232, 220, LV_LABEL_LONG_CLIP);
    // Shared row styles: 128 rows must fit the 96 KB LVGL heap, so no per-row local styles.
    lv_style_init(&rowStyle);
    lv_style_set_width(&rowStyle, kW);
    lv_style_set_height(&rowStyle, 38);
    lv_style_set_pad_left(&rowStyle, 12);
    lv_style_set_pad_right(&rowStyle, 12);
    lv_style_set_border_side(&rowStyle, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&rowStyle, 1);
    lv_style_set_border_color(&rowStyle, hex(kLine));
    lv_style_init(&rowPressed);
    lv_style_set_bg_color(&rowPressed, hex(kPanelHi));
    lv_style_set_bg_opa(&rowPressed, LV_OPA_COVER);
    lv_style_init(&rowText);
    lv_style_set_text_font(&rowText, &lv_font_montserrat_14);
    lv_style_set_text_color(&rowText, hex(kInk));
    lv_style_set_align(&rowText, LV_ALIGN_LEFT_MID);
    lv_style_set_width(&rowText, kW - 24);
    lv_style_set_height(&rowText, 17);  // one line; LONG_DOT ends it with "..."
}

// ---- Add music ------------------------------------------------------------------------
lv_obj_t *addCard(lv_obj_t *p, int y, const char *icon, const char *title, const char *sub, lv_event_cb_t cb) {
    lv_obj_t *card = button(p, 8, y, 224, 66, kPanel, kPanelHi, 12, cb);
    lv_obj_t *badge = box(card, 12, 13, 40, 40, kLcd, 20);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    centeredText(badge, icon, &lv_font_montserrat_20, kLcdInk);
    text(card, title, &lv_font_montserrat_16, kInk, 64, 14, 150, LV_LABEL_LONG_CLIP);
    text(card, sub, &lv_font_montserrat_12, kMuted, 64, 36, 150, LV_LABEL_LONG_CLIP);
    centeredText(card, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, kMuted);
    lv_obj_align(lv_obj_get_child(card, -1), LV_ALIGN_RIGHT_MID, -12, 0);
    return card;
}
void createAdd(lv_obj_t *p, bool usb) {
    addCard(p, 8, LV_SYMBOL_WIFI, "Wi-Fi", "Phone or Mac", [](lv_event_t *) { if (act.addWifi) act.addWifi(); });
    if (usb) addCard(p, 82, LV_SYMBOL_USB, "USB drive", "Mac or PC", [](lv_event_t *) { if (act.addUsb) act.addUsb(); });
    addHint = text(p, "", &lv_font_montserrat_12, kMuted, 12, usb ? 160 : 86, 216);
    setAddHint("Wi-Fi: scan the codes with the phone camera, then pick songs.\n"
               "If the page won't load, turn off mobile data.");
}

// ---- Info ---------------------------------------------------------------------------
void createInfo(lv_obj_t *p) {
    lv_obj_t *card = box(p, 8, 8, 224, 112, kPanel, 12);
    text(card, "MICROSD", &lv_font_unscii_8, kMuted, 14, 12, 120, LV_LABEL_LONG_CLIP);
    freeLabel = text(card, "Reading...", &lv_font_montserrat_28, kInk, 14, 26, 196, LV_LABEL_LONG_CLIP);
    storageBar = lv_bar_create(card);
    lv_obj_remove_style_all(storageBar);
    lv_obj_set_pos(storageBar, 14, 66);
    lv_obj_set_size(storageBar, 196, 8);
    lv_obj_set_style_radius(storageBar, 4, 0);
    lv_obj_set_style_radius(storageBar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(storageBar, hex(kPanelHi), 0);
    lv_obj_set_style_bg_opa(storageBar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(storageBar, hex(kLcd), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(storageBar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(storageBar, 0, 1000);
    usedLabel = text(card, "", &lv_font_montserrat_12, kMuted, 14, 84, 196, LV_LABEL_LONG_CLIP);
    lv_obj_t *songs = box(p, 8, 128, 224, 40, kPanel, 12);
    text(songs, LV_SYMBOL_AUDIO, &lv_font_montserrat_16, kLcd, 14, 11, 20, LV_LABEL_LONG_CLIP);
    songsLabel = text(songs, "", &lv_font_montserrat_14, kInk, 42, 12, 170, LV_LABEL_LONG_DOT);
    noteLabel = text(p, "", &lv_font_montserrat_12, kMuted, 12, 178, 216, LV_LABEL_LONG_DOT);
    lv_obj_set_height(noteLabel, 32);
    deviceLabel = text(p, "", &lv_font_unscii_8, kMuted, 12, 222, 216, LV_LABEL_LONG_CLIP);
}

// ---- Overlays -----------------------------------------------------------------------
lv_obj_t *openOverlay(const char *title) {
    closeOverlay();
    overlay = box(lv_layer_top(), 0, 0, kW, 320, kBody, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);  // swallow touches meant for the pages below
    overlayTitle = text(overlay, title, &lv_font_unscii_16, kLcd, 10, 6, 150, LV_LABEL_LONG_CLIP);
    overlayCorner = text(overlay, "", &lv_font_unscii_8, kMuted, 150, 10, 80, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(overlayCorner, LV_TEXT_ALIGN_RIGHT, 0);
    box(overlay, 0, kHeaderH - 1, kW, 1, kLine, 0);
    return overlay;
}

}  // namespace

void create(const Actions &actions, bool usbSupported, uint8_t volume) {
    act = actions;
    createChrome();
    for (auto &p : pages) p = page();
    createPlayer(pages[kPlayer], volume);
    createLibrary(pages[kLibrary]);
    createAdd(pages[kAdd], usbSupported);
    createInfo(pages[kInfo]);
    showTab(kPlayer);
}

void showTab(Tab tab) {
    current = tab;
    for (int i = 0; i < 4; ++i) {
        const bool on = i == tab;
        if (on) lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(navPills[i], hex(on ? kLcd : kPanel), 0);
        lv_obj_set_style_text_color(navIcons[i], hex(on ? kLcdInk : kMuted), 0);
        lv_obj_set_style_text_color(navTexts[i], hex(on ? kLcd : kMuted), 0);
    }
    setText(header, kTitles[tab]);
}
Tab activeTab() { return current; }

void setNowPlaying(const NowPlaying &now) {
    if (strcmp(lv_label_get_text(titleLabel), now.title)) {
        lv_label_set_text(titleLabel, now.title);
        lv_point_t size;
        lv_txt_get_size(&size, now.title, &lv_font_montserrat_20, 0, -2, 188, LV_TEXT_FLAG_NONE);
        lv_obj_set_y(artistLabel, size.y > 26 ? 70 : 46);  // artist sits right under a one-line title
    }
    setText(artistLabel, now.artist);
    setText(lcdState, now.state);
    char buffer[24];
    if (now.tracks) snprintf(buffer, sizeof(buffer), "%02d/%02d", now.track, now.tracks);
    else buffer[0] = 0;
    setText(lcdTrack, buffer);
    formatTime(buffer, sizeof(buffer), now.elapsed);
    setText(elapsedLabel, buffer);
    setText(detailLabel, now.detail);
    setText(playIcon, now.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    setText(headerState, now.playing ? LV_SYMBOL_PLAY : !strcmp(now.state, "PAUSED") ? LV_SYMBOL_PAUSE : "");
    if (lv_bar_get_value(progress) != now.permille) lv_bar_set_value(progress, now.permille, LV_ANIM_OFF);
}
void setMessage(const char *message) { setText(artistLabel, message); }

void setVolume(uint8_t level) {
    if (lv_slider_get_value(volumeSlider) != level) lv_slider_set_value(volumeSlider, level, LV_ANIM_OFF);
    char value[8];
    snprintf(value, sizeof(value), "%u", unsigned(level));
    setText(volumeValue, value);
}

void setFiles(const char *folder, const std::vector<FileItem> &items, const char *footer) {
    lv_obj_clean(fileList);
    setText(folderLabel, folder);
    if (strcmp(folder, "/")) lv_obj_clear_flag(upButton, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(upButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(folderLabel, strcmp(folder, "/") ? 60 : 12);
    for (size_t i = 0; i < items.size(); ++i) {
        const FileItem &item = items[i];
        lv_obj_t *row = lv_obj_create(fileList);
        lv_obj_remove_style_all(row);
        lv_obj_add_style(row, &rowStyle, 0);
        lv_obj_add_style(row, &rowPressed, LV_STATE_PRESSED);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, [](lv_event_t *e) {
            if (act.openItem) act.openItem(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
        }, LV_EVENT_CLICKED, reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_t *label = lv_label_create(row);
        lv_obj_add_style(label, &rowText, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        const std::string line = std::string(item.directory ? LV_SYMBOL_DIRECTORY : item.current ? LV_SYMBOL_PLAY : LV_SYMBOL_AUDIO) +
                                 "  " + item.name;
        lv_label_set_text(label, line.c_str());
        if (item.directory || item.current) lv_obj_set_style_text_color(label, hex(kLcd), 0);
    }
    if (items.empty()) {
        lv_obj_t *empty = lv_label_create(fileList);
        lv_obj_set_style_text_color(empty, hex(kMuted), 0);
        lv_obj_set_style_pad_all(empty, 12, 0);
        lv_label_set_text(empty, footer);
        setText(fileFooter, "");
    } else setText(fileFooter, footer);
}

void setStorage(const StorageInfo &info) {
    char buffer[96];
    if (info.sdReady && info.totalBytes) {
        const uint64_t free = info.totalBytes > info.usedBytes ? info.totalBytes - info.usedBytes : 0;
        snprintf(buffer, sizeof(buffer), "%.2f GB free", double(free) / 1e9);
        setText(freeLabel, buffer);
        lv_bar_set_value(storageBar, int(info.usedBytes * 1000 / info.totalBytes), LV_ANIM_OFF);
        snprintf(buffer, sizeof(buffer), "%.2f GB used of %.2f GB", double(info.usedBytes) / 1e9, double(info.totalBytes) / 1e9);
        setText(usedLabel, buffer);
    } else {
        setText(freeLabel, "No card");
        lv_bar_set_value(storageBar, 0, LV_ANIM_OFF);
        setText(usedLabel, "microSD unavailable. Nothing was formatted.");
    }
    snprintf(buffer, sizeof(buffer), "%u song%s in the library", info.songs, info.songs == 1 ? "" : "s");
    setText(songsLabel, buffer);
    setText(noteLabel, info.note.c_str());
    snprintf(buffer, sizeof(buffer), "FLASH 16 MIB   PSRAM %.0f MIB", double(info.psramMiB));
    setText(deviceLabel, buffer);
}

void setAddHint(const char *t) { setText(addHint, t); }

void showDetails(const char *title, const char *body) {
    lv_obj_t *box = lv_msgbox_create(nullptr, title, body, nullptr, true);
    lv_obj_set_width(box, 220);
    lv_obj_set_style_bg_color(box, hex(kPanel), 0);
    lv_obj_set_style_text_color(box, hex(kInk), 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_center(box);
}

// ---- Wi-Fi ----------------------------------------------------------------------------
void openWifi(const char *ssid, const char *password, const char *url) {
    wifiSsid = ssid; wifiPassword = password; wifiUrl = url;
    userPickedQr = false;
    openOverlay("WI-FI 1/2");
    // White panel = the quiet zone the widget doesn't draw. QR size is chosen so LVGL keeps
    // the minimum QR version: 5 px modules (v4, Wi-Fi) / 6 px (v2, URL), >= 4 modules of white.
    lv_obj_t *panel = box(overlay, 16, 32, 208, 208, 0xFFFFFF, 10);
    qr = lv_qrcode_create(panel, 168, lv_color_black(), lv_color_white());
    lv_obj_center(qr);
    qrLine1 = text(overlay, "", &lv_font_montserrat_14, kInk, 12, 246, 216, LV_LABEL_LONG_DOT);
    qrLine2 = text(overlay, "", &lv_font_montserrat_12, kMuted, 12, 264, 216, LV_LABEL_LONG_DOT);
    qrToggle = pillButton(overlay, "Page code", 12, 282, 104, false, [](lv_event_t *) {
        userPickedQr = true;
        showWifiPage(!wifiPage);
    });
    pillButton(overlay, "Done", 124, 282, 104, true, [](lv_event_t *) { if (act.overlayDone) act.overlayDone(); });
    showWifiPage(false);
}
void showWifiPage(bool pageCode) {
    if (!qr) return;
    wifiPage = pageCode;
    const std::string data = pageCode ? wifiUrl : player::wifiQrPayload(wifiSsid, wifiPassword);
    lv_qrcode_update(qr, data.data(), data.size());
    setText(overlayTitle, pageCode ? "WI-FI 2/2" : "WI-FI 1/2");
    if (pageCode) {
        setText(qrLine1, ("Open " + wifiUrl).c_str());
        setText(qrLine2, "Won't load? Turn off mobile data.");
    } else {
        setText(qrLine1, ("Join  " + wifiSsid).c_str());
        setText(qrLine2, ("Password  " + wifiPassword).c_str());
    }
    setText(qrToggle, pageCode ? "Wi-Fi code" : "Page code");
}
void stationJoined() {
    if (qr && !wifiPage && !userPickedQr) showWifiPage(true);
}
void setWifiStatus(const char *line, const char *corner) {
    if (!overlay) return;
    if (wifiPage && line && line[0]) setText(qrLine2, line);
    setText(overlayCorner, corner);
}

// ---- USB ------------------------------------------------------------------------------
void openUsb() {
    openOverlay("USB DRIVE");
    lv_obj_t *badge = box(overlay, 92, 40, 56, 56, kLcd, 28);
    centeredText(badge, LV_SYMBOL_USB, &lv_font_montserrat_28, kLcdInk);
    const char *steps[] = {"Plug into a Mac or PC with USB-C.", "Copy MP3s into Music, or use the Mac app.",
                           "Eject the drive on the computer."};
    for (int i = 0; i < 3; ++i) {
        const int y = 106 + i * 40;
        lv_obj_t *n = box(overlay, 14, y + 2, 22, 22, kPanelHi, 11);
        char digit[2] = {char('1' + i), 0};
        centeredText(n, digit, &lv_font_unscii_8, kLcd);
        text(overlay, steps[i], &lv_font_montserrat_14, kInk, 46, y, 182);
    }
    text(overlay, "Restarts by itself after the eject.", &lv_font_montserrat_12, kMuted, 14, 224, 212);
    usbStatus = text(overlay, "Starting USB...", &lv_font_montserrat_12, kLcd, 14, 246, 212, LV_LABEL_LONG_DOT);
    lv_obj_set_height(usbStatus, 30);
    pillButton(overlay, "Cancel", 12, 282, 216, false, [](lv_event_t *) { if (act.overlayDone) act.overlayDone(); });
}
void setUsbStatus(const char *t) { if (usbStatus) setText(usbStatus, t); }

void closeOverlay() {
    if (overlay) lv_obj_del(overlay);
    overlay = qr = qrLine1 = qrLine2 = overlayCorner = overlayTitle = qrToggle = usbStatus = nullptr;
    lv_obj_invalidate(lv_scr_act());
}

}  // namespace ui

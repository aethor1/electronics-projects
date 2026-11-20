#include "UsbDrive.h"
#include "PlayerConfig.h"

#if PLAYER_USB_DRIVE
#include <SD_MMC.h>
#include <USB.h>
#include <USBMSC.h>
#include <sdmmc_cmd.h>
#include <driver/periph_ctrl.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/usb_serial_jtag_reg.h>

namespace usbdrive {
namespace {

USBMSC msc;
sdmmc_card_t *card = nullptr;
volatile bool accessed = false, ejectedFlag = false, started = false;

// SD_MMC keeps the initialized card in a protected member; a pointer-to-member taken
// through a derived class reads it without changing the library.
struct CardAccess : fs::SDMMCFS {
    static sdmmc_card_t *get(fs::SDMMCFS &fs) { return fs.*(&CardAccess::_card); }
};

int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bytes) {
    if (!card || offset || bytes % 512) return -1;
    accessed = true;
    return sdmmc_read_sectors(card, buffer, lba, bytes / 512) == ESP_OK ? int32_t(bytes) : -1;
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bytes) {
    if (!card || offset || bytes % 512) return -1;
    accessed = true;
    return sdmmc_write_sectors(card, buffer, lba, bytes / 512) == ESP_OK ? int32_t(bytes) : -1;
}

bool onStartStop(uint8_t, bool start, bool loadEject) {
    if (loadEject && !start) {  // Finder "Eject" / Windows "Safely remove"
        ejectedFlag = true;
        msc.mediaPresent(false);
    }
    return true;
}

}  // namespace

bool supported() { return true; }

bool begin(const char **error) {
    static const char *unused;
    const char **err = error ? error : &unused;
    if (started) return true;

    // Marker + Music folder through the normal filesystem, closed (synced) before export.
    if (!SD_MMC.exists(player::kMusicFolder)) SD_MMC.mkdir(player::kMusicFolder);
    File marker = SD_MMC.open(player::kDriveMarkerFile, FILE_WRITE);
    if (marker) {
        marker.print("Addy's MP3 Player. The Mac app copies songs into /Music.\n");
        marker.close();
    }

    card = CardAccess::get(SD_MMC);
    if (!card || card->csd.capacity <= 0 || card->csd.sector_size != 512) {
        *err = "microSD not ready for USB.";
        return false;
    }
    msc.vendorID("Addy");
    msc.productID("MP3 Player");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);
    if (!msc.begin(uint32_t(card->csd.capacity), uint16_t(card->csd.sector_size))) {
        *err = "USB drive could not start.";
        return false;
    }
    USB.productName("Addy's MP3 Player");
    if (!USB.begin()) {
        *err = "USB could not start.";
        return false;
    }
    started = true;
    return true;
}

bool hostAccessed() { return accessed; }
bool ejected() { return ejectedFlag; }

void restartPlayer() {
    if (started) {
        // Same register changes as the core's usb_switch_to_cdc_jtag() (esp32-hal-tinyusb.c).
        periph_module_disable(PERIPH_USB_MODULE);
        CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                            RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
        CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
        delay(50);
    }
    esp_restart();
}

}  // namespace usbdrive

#else

namespace usbdrive {
bool supported() { return false; }
bool begin(const char **error) {
    if (error) *error = "This build has no USB drive mode.";
    return false;
}
bool hostAccessed() { return false; }
bool ejected() { return false; }
void restartPlayer() { esp_restart(); }
}  // namespace usbdrive

#endif

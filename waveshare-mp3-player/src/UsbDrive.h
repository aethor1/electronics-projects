#pragma once
// "USB drive" mode: exposes the raw microSD card to a computer over the board's USB-C.
//
// The board normally uses the chip's USB-Serial/JTAG port (flashing, serial logs).
// Entering drive mode starts TinyUSB, which takes the USB PHY away from that port, so
// the COM port disappears. There is no way back without a reset: once the computer
// ejects the drive (or the user cancels), the firmware restarts and remounts the card.
// Nothing else may touch the card while it is exported.
#include <Arduino.h>

namespace usbdrive {

bool supported();   // false when built without PLAYER_USB_DRIVE
// Writes the marker file the Mac app looks for, then exports the card. Call only after
// audio has released its file and the UI has stopped reading the card.
bool begin(const char **error);
bool hostAccessed();  // the computer has read the card at least once
bool ejected();       // the computer sent an eject
// Hands the USB pins back to the USB-Serial/JTAG port (the PHY choice lives in an RTC
// register that survives a software reset), then restarts the firmware.
[[noreturn]] void restartPlayer();

}  // namespace usbdrive

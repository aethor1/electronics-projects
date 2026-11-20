/* Host stand-in for the one Arduino symbol LVGL uses (LV_TICK_CUSTOM in lv_conf.h). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
#ifdef __cplusplus
}
#endif

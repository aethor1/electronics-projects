#pragma once
#include "Arduino.h"
#include <string.h>
#include <algorithm>
using esp_err_t=int;
using i2s_mode_t=int;
constexpr int ESP_OK=0, ESP_INTR_FLAG_LEVEL1=1, I2S_MODE_MASTER=1, I2S_MODE_TX=2;
constexpr int I2S_BITS_PER_SAMPLE_16BIT=16, I2S_CHANNEL_FMT_RIGHT_LEFT=0, I2S_COMM_FORMAT_STAND_I2S=0;
constexpr int I2S_CHANNEL_STEREO=2, I2S_NUM_0=0, I2S_PIN_NO_CHANGE=-1;
struct i2s_config_t {
    int mode,sample_rate,bits_per_sample,channel_format,communication_format,intr_alloc_flags;
    int dma_buf_count,dma_buf_len; bool use_apll,tx_desc_auto_clear; int fixed_mclk;
};
struct i2s_pin_config_t { int mck_io_num,bck_io_num,ws_io_num,data_out_num,data_in_num; };
inline unsigned installs=0, uninstalls=0, starts=0, stops=0, writeCalls=0;
inline bool i2sActive=false, mockWriteError=false;
inline size_t writeLimit=SIZE_MAX;
inline std::vector<uint8_t> pcmBytes;
inline i2s_pin_config_t lastPins={};
inline i2s_config_t lastConfig={};
inline esp_err_t i2s_driver_install(int,const i2s_config_t *config,int,void*) {
    ++installs; lastConfig=*config; i2sActive=true; return ESP_OK;
}
inline esp_err_t i2s_driver_uninstall(int) { ++uninstalls; i2sActive=false; return ESP_OK; }
inline esp_err_t i2s_set_pin(int,const i2s_pin_config_t *pins) { lastPins=*pins; return ESP_OK; }
inline esp_err_t i2s_set_clk(int,int,int,int) { return ESP_OK; }
inline esp_err_t i2s_zero_dma_buffer(int) { return ESP_OK; }
inline esp_err_t i2s_start(int) { ++starts; i2sActive=true; return ESP_OK; }
inline esp_err_t i2s_stop(int) { ++stops; i2sActive=false; return ESP_OK; }
inline esp_err_t i2s_write(int,const void *data,size_t bytes,size_t *written,TickType_t) {
    ++writeCalls;
    if(mockWriteError || !i2sActive) { *written=0; return -1; }
    *written=std::min(bytes,writeLimit);
    auto *p=static_cast<const uint8_t*>(data);
    pcmBytes.insert(pcmBytes.end(),p,p+*written);
    return ESP_OK;
}

#include "BatchedI2S.h"

bool BatchedI2S::begin() {
    batch_.reset(); failed_=false; frames_=writes_=0;
    if(!installed_) {
        i2s_config_t config={};
        config.mode=i2s_mode_t(I2S_MODE_MASTER|I2S_MODE_TX);
        config.sample_rate=hertz;
        config.bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT;
        config.channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT;
        config.communication_format=I2S_COMM_FORMAT_STAND_I2S;
        config.intr_alloc_flags=ESP_INTR_FLAG_LEVEL1;
        config.dma_buf_count=player::kDmaBuffers;
        config.dma_buf_len=player::kDmaFrames;
        config.use_apll=false;
        config.tx_desc_auto_clear=true;
        config.fixed_mclk=0;
        if(i2s_driver_install(I2S_NUM_0,&config,0,nullptr)!=ESP_OK) { failed_=true; return false; }
        installed_=true;
        i2s_pin_config_t pins={};
        pins.mck_io_num=I2S_PIN_NO_CHANGE;
        pins.bck_io_num=48; pins.ws_io_num=38; pins.data_out_num=47;
        pins.data_in_num=I2S_PIN_NO_CHANGE;
        if(i2s_set_pin(I2S_NUM_0,&pins)!=ESP_OK) {
            i2s_driver_uninstall(I2S_NUM_0); installed_=false;
            failed_=true; return false;
        }
    }
    if(i2s_set_clk(I2S_NUM_0,hertz,I2S_BITS_PER_SAMPLE_16BIT,I2S_CHANNEL_STEREO)!=ESP_OK ||
       i2s_zero_dma_buffer(I2S_NUM_0)!=ESP_OK || i2s_start(I2S_NUM_0)!=ESP_OK) {
        stop(); failed_=true; return false;
    }
    active_=true;
    return true;
}
bool BatchedI2S::SetRate(int hz) {
    if(hz<8000 || hz>48000) return false;
    if(hertz==hz) return true;
    // MPEG streams may change rate. Finish any staged samples at their old rate.
    if(installed_ && active_) flush();
    hertz=hz;
    if(installed_ && i2s_set_clk(I2S_NUM_0,hertz,I2S_BITS_PER_SAMPLE_16BIT,I2S_CHANNEL_STEREO)!=ESP_OK) {
        failed_=true; return false;
    }
    return true;
}
size_t BatchedI2S::write(const uint8_t *data, size_t bytes) {
    size_t written=0; ++writes_;
    if(i2s_write(I2S_NUM_0,data,bytes,&written,pdMS_TO_TICKS(player::kI2sWaitMs))!=ESP_OK) failed_=true;
    return written;
}
bool BatchedI2S::ConsumeSample(int16_t sample[2]) {
    if(!active_ || failed() || !budget_) return false;
    const int16_t right=channels==1 ? sample[0] : sample[1];
    const uint32_t packed=player::packPcm(sample[0],right,gainQ12_,player::kMonoOutput);
    if(!batch_.push(packed,[this](const uint8_t *data,size_t bytes){return write(data,bytes);})) return false;
    --budget_; ++frames_;
    return true;
}
void BatchedI2S::flush() {
    if(!installed_ || !active_) return;
    // Bound retries if a device error prevents the DMA sink from progressing.
    for(unsigned tries=0; batch_.pendingBytes() && !failed() && tries<16; ++tries) {
        if(!batch_.drain([this](const uint8_t *data,size_t bytes){return write(data,bytes);})) vTaskDelay(1);
    }
    if(batch_.pendingBytes()) failed_=true;
}
bool BatchedI2S::pause(bool paused) {
    if(!installed_) return false;
    const esp_err_t err=paused ? i2s_stop(I2S_NUM_0) : i2s_start(I2S_NUM_0);
    if(err!=ESP_OK) { failed_=true; return false; }
    active_=!paused;
    return true;
}
bool BatchedI2S::stop() {
    batch_.reset();
    if(installed_) { i2s_stop(I2S_NUM_0); i2s_zero_dma_buffer(I2S_NUM_0); }
    active_=false;
    return true;
}

#pragma once
#include <AudioOutput.h>
#include <driver/i2s.h>
#include "PlayerConfig.h"
#include "PlayerCore.h"

class BatchedI2S final : public AudioOutput {
public:
    BatchedI2S() { hertz=44100; bps=16; channels=2; gainF2P6=player::kGainQ6; }
    ~BatchedI2S() override { stop(); if (installed_) i2s_driver_uninstall(I2S_NUM_0); }
    bool begin() override;
    bool SetRate(int hz) override;
    bool SetBitsPerSample(int bits) override { if(bits!=16) return false; bps=bits; return true; }
    bool SetChannels(int count) override { if(count!=1 && count!=2) return false; channels=count; return true; }
    bool ConsumeSample(int16_t sample[2]) override;
    bool stop() override;
    void flush() override;
    bool pause(bool paused);
    void beginSlice() { budget_=player::kPcmSliceFrames; }
    bool failed() const { return failed_ || batch_.failed(); }
    uint32_t writes() const { return writes_; }
    uint32_t frames() const { return frames_; }
    uint16_t sampleRate() const { return hertz; }
private:
    size_t write(const uint8_t *data, size_t bytes);
    player::PcmBatch<player::kPcmBatchFrames> batch_;
    bool installed_=false, active_=false, failed_=false;
    unsigned budget_=0;
    uint32_t writes_=0, frames_=0;
};

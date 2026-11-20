#pragma once
#include <AudioFileSource.h>
#include "PlayerCore.h"

class BufferedSource final : public AudioFileSource {
public:
    BufferedSource(AudioFileSource &source,uint8_t *storage,size_t bytes)
        : source_(source), reader_(source,storage,bytes) {}
    uint32_t read(void *data,uint32_t bytes) override { return reader_.read(data,bytes); }
    uint32_t readNonBlock(void *data,uint32_t bytes) override { return read(data,bytes); }
    bool seek(int32_t offset,int origin) override { return reader_.seek(offset,origin); }
    bool close() override { return source_.close(); }
    bool isOpen() override { return source_.isOpen(); }
    uint32_t getSize() override { return source_.getSize(); }
    uint32_t getPos() override { return reader_.position(); }
    bool loop() override { return !reader_.failed() && source_.loop(); }
    uint32_t readCalls() const { return reader_.reads(); }
private:
    AudioFileSource &source_;
    player::ReadAhead<AudioFileSource> reader_;
};

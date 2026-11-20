#pragma once
#include "Arduino.h"
#define FSPI 2
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings { SPISettings(int,int,int) {} };
class SPIClass {
public:
    SPIClass(int) {}
    void begin(int,int,int) {}
    void beginTransaction(SPISettings) { ++transactions; }
    void endTransaction() {}
    void write(uint8_t value) { assert(chipSelected); wireBytes.push_back({value,dataMode}); }
    uint8_t transfer(uint8_t value) { write(value); return 0; }
    void transfer16(uint16_t value) { write(value>>8); write(value); }
    void writeBytes(const uint8_t *data,uint32_t bytes) { for(uint32_t i=0;i<bytes;++i) write(data[i]); }
    void transferBytes(const uint8_t *data,uint8_t*,uint32_t bytes) { writeBytes(data,bytes); }
};

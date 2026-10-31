#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <vector>

#define IRAM_ATTR
#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1
#define RISING 0x01
using TaskHandle_t=void*;
using BaseType_t=int;
using TickType_t=uint32_t;
#define pdMS_TO_TICKS(ms) (ms)
struct WireByte { uint8_t value; bool data; };
inline std::vector<WireByte> wireBytes;
inline uint32_t mockTime=0, pwmDuty=0, transactions=0;
inline bool chipSelected=false, dataMode=false;
inline uint32_t millis() { return mockTime; }
inline void delay(uint32_t ms) { mockTime+=ms; }
inline void vTaskDelay(uint32_t ms) { mockTime+=ms; }
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int state) {
    if(pin==42) chipSelected=state==LOW;
    if(pin==41) dataMode=state==HIGH;
}
inline void ledcSetup(int,int,int) {}
inline void ledcAttachPin(int,int) {}
inline void ledcWrite(int,uint32_t duty) { pwmDuty=duty; }
class Print {
public:
    virtual ~Print()=default;
    virtual size_t write(uint8_t) { return 1; }
    template<class... A> int printf(const char*,A...) { return 0; }
    template<class... A> int printf_P(const char*,A...) { return 0; }
    template<class T> void println(T) {}
    void flush() {}
};
inline Print Serial;

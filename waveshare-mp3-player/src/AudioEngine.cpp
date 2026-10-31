#include "AudioEngine.h"
#include "BatchedI2S.h"
#include "BufferedSource.h"
#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <memory>
#include <new>

namespace {
QueueHandle_t commands=nullptr;
SemaphoreHandle_t stateLock=nullptr;
TaskHandle_t uiTask=nullptr;
AudioState shared;
alignas(8) uint8_t decoderMemory[AudioGeneratorMP3::preAllocSize()];
alignas(4) uint8_t compressedMemory[player::kAudioReadAhead];

bool skipTags(AudioFileSource &source) {
    for(unsigned tags=0; tags<8; ++tags) {
        const uint32_t at=source.getPos();
        uint8_t header[10]={};
        const uint32_t bytes=source.read(header,sizeof(header));
        uint32_t next=at;
        const auto result=player::leadingTag(header,bytes,source.getSize(),at,next);
        if(result==player::TagResult::Invalid) return false;
        if(result==player::TagResult::None) return source.seek(int32_t(at),SEEK_SET);
        if(!source.seek(int32_t(next),SEEK_SET)) return false;
    }
    return false; // A bounded number of consecutive tags, even on malformed files.
}

void worker(void *) {
    AudioFileSourceFS sdSource(SD_MMC), flashSource(LittleFS);
    AudioFileSourceFS *source=nullptr;
    std::unique_ptr<BufferedSource> buffered;
    AudioGeneratorMP3 decoder(decoderMemory,sizeof(decoderMemory));
    BatchedI2S output;
    AudioState state;
    bool attached=false;
    uint32_t lastTick=millis(), publishedAt=0;
    uint64_t activeMs=0;
    auto publish=[&](bool notify) {
        xSemaphoreTake(stateLock,portMAX_DELAY); shared=state; xSemaphoreGive(stateLock);
        publishedAt=millis();
        if(notify && uiTask) xTaskNotifyGive(uiTask);
    };
    auto stop=[&]() {
        if(attached) { decoder.stop(); attached=false; }
        output.stop();
        buffered.reset();
        if(source) source->close();
    };
    auto fail=[&](const char *message) {
        stop(); state.running=false; state.paused=false; state.failed=true;
        strlcpy(state.error,message,sizeof(state.error));
    };
    auto start=[&](const char *path,bool sd) {
        stop(); state=AudioState{}; state.sd=sd;
        strlcpy(state.path,path,sizeof(state.path));
        source=sd ? &sdSource : &flashSource;
        activeMs=0; lastTick=millis();
        if(!source->open(path)) { fail("Cannot open this song."); return; }
        state.bytes=source->getSize();
        if(!state.bytes || !skipTags(*source)) { fail("Invalid or truncated MP3 tag."); return; }
        buffered.reset(new(std::nothrow) BufferedSource(*source,compressedMemory,sizeof(compressedMemory)));
        if(!buffered) { fail("Not enough memory to start audio."); return; }
        attached=true;
        if(!decoder.begin(buffered.get(),&output)) { fail("Audio decoder or I2S could not start."); return; }
        state.running=true;
    };
    for(;;) {
        // Inactive playback blocks here indefinitely instead of polling at 200 Hz.
        AudioCommand command;
        const TickType_t wait=state.running && !state.paused ? 0 : portMAX_DELAY;
        const bool received=xQueueReceive(commands,&command,wait)==pdTRUE;
        uint32_t now=millis();
        if(state.running && !state.paused) activeMs+=uint32_t(now-lastTick);
        lastTick=now;
        if(received) {
            do {
                if(command.action==START) start(command.path,command.sd);
                else if(!state.running && state.path[0] && command.action!=PAUSE) {
                    char path[player::kMaxPath]; strlcpy(path,state.path,sizeof(path));
                    const bool sd=state.sd; start(path,sd);
                } else if(state.running) {
                    const bool paused=command.action==TOGGLE ? !state.paused : command.action==PAUSE;
                    if(paused!=state.paused) {
                        if(!output.pause(paused)) fail("I2S pause/resume failed.");
                        else state.paused=paused;
                    }
                }
            } while(xQueueReceive(commands,&command,0)==pdTRUE);
            state.elapsed=uint32_t(activeMs/1000);
            publish(true);
        }
        if(state.running && !state.paused) {
            output.beginSlice(); ++state.slices;
            const bool more=decoder.loop();
            state.position=buffered->getPos(); state.readCalls=buffered->readCalls();
            state.frames=output.frames(); state.writes=output.writes();
            if(output.failed()) fail("I2S write failed.");
            else if(!more) {
                output.flush();
                if(output.failed()) fail("Could not drain audio output.");
                else if(state.frames<=1 || state.position<state.bytes) fail("Song ended early or could not be decoded.");
                else {
                    // Let queued DMA samples play before stopping clocks at EOF.
                    const uint32_t rate=output.sampleRate();
                    const uint32_t drainMs=(player::kDmaBuffers*player::kDmaFrames*1000UL+rate-1)/rate+2;
                    vTaskDelay(pdMS_TO_TICKS(drainMs));
                    stop(); state.running=false; state.paused=false; state.position=state.bytes;
                }
                publish(true);
            }
            // One yield per 512-frame slice rather than one per sample-buffer retry.
            vTaskDelay(1);
        }
        state.elapsed=uint32_t(activeMs/1000);
        if(millis()-publishedAt>=player::kStatePublishMs) publish(false);
    }
}
}

AudioState audioSnapshot() {
    if(!stateLock) return AudioState{};
    xSemaphoreTake(stateLock,portMAX_DELAY); AudioState state=shared; xSemaphoreGive(stateLock);
    return state;
}
bool audioSend(const AudioCommand &command) {
    return commands && xQueueSend(commands,&command,0)==pdTRUE;
}
bool audioBegin(TaskHandle_t target) {
    uiTask=target;
    stateLock=xSemaphoreCreateMutex(); commands=xQueueCreate(6,sizeof(AudioCommand));
    if(stateLock && commands && xTaskCreatePinnedToCore(worker,"mp3",12288,nullptr,2,nullptr,0)==pdPASS) return true;
    if(commands) { vQueueDelete(commands); commands=nullptr; }
    if(stateLock) { vSemaphoreDelete(stateLock); stateLock=nullptr; }
    return false;
}

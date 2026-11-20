#include "PlayerCore.h"
#include "TransferCore.h"
#include "ButtonCore.h"
#include "BatchedI2S.h"
#include "vendor/Display_ST7789.h"
#include <algorithm>
#include <assert.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

uint8_t Touch_Init() { return 1; }
struct MemorySource {
    std::vector<uint8_t> data;
    uint32_t pos=0, reads=0, seeks=0;
    uint32_t getPos() const { return pos; }
    uint32_t getSize() const { return data.size(); }
    uint32_t read(void *out,uint32_t size) {
        ++reads; uint32_t n=std::min<uint32_t>(size,data.size()-pos);
        memcpy(out,data.data()+pos,n); pos+=n; return n;
    }
    bool seek(int32_t where,int origin) {
        ++seeks; if(origin!=SEEK_SET || where<0 || uint32_t(where)>data.size()) return false;
        pos=where; return true;
    }
};
static void check(bool condition,const char *message) {
    if(!condition) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
static void testPolicy() {
    using namespace player;
    check(screenMode(14999,0,15000,30000)==ScreenMode::Awake,"screen remains awake before dim deadline");
    check(screenMode(15000,0,15000,30000)==ScreenMode::Dim,"screen dims at deadline");
    check(screenMode(30000,0,15000,30000)==ScreenMode::Off,"screen turns off at deadline");
    check(screenMode(999,UINT32_MAX-1000,1500,3000)==ScreenMode::Dim,"inactivity handles millis wrap");
    for(unsigned p=0;p<=255;++p) check(backlightDuty(p)<=1023,"PWM never overflows 10-bit range");
    check(backlightDuty(0)==0 && backlightDuty(100)==1023,"PWM endpoints");
    WakeGesture gate;
    check(!gate.filter(true,true),"wake press suppressed");
    check(!gate.filter(true,false),"held wake press suppressed");
    check(!gate.filter(false,false),"wake release suppressed");
    check(gate.filter(true,false),"next independent press delivered");
}
static void testTags() {
    using namespace player;
    uint8_t h[10]={'I','D','3',4,0,0,0,0,1,0}; uint32_t next=0;
    check(leadingTag(h,10,1000,0,next)==TagResult::Valid && next==138,"ID3v2.4 synchsafe size");
    h[5]=0x10;
    check(leadingTag(h,10,1000,20,next)==TagResult::Valid && next==168,"footer and nonzero tag position");
    h[6]=0x80; check(leadingTag(h,10,1000,0,next)==TagResult::Invalid,"non-synchsafe tag rejected");
    h[6]=0; check(leadingTag(h,10,100,0,next)==TagResult::Invalid,"truncated tag rejected");
    check(leadingTag(h,5,1000,0,next)==TagResult::Invalid,"short ID3 header rejected");
    h[3]=3; h[5]=0;
    check(leadingTag(h,10,1000,0,next)==TagResult::Valid && next==138,"ID3v2.3 size");
    h[3]=2; check(leadingTag(h,10,1000,0,next)==TagResult::Valid,"ID3v2.2 header");
    h[0]='x'; check(leadingTag(h,10,1000,0,next)==TagResult::None,"untagged MP3 untouched");
}
static void testReadAhead() {
    std::mt19937 random(7241);
    MemorySource source; source.data.resize(100003);
    for(auto &byte:source.data) byte=random();
    uint8_t storage[16384]; player::ReadAhead<MemorySource> reader(source,storage,sizeof(storage));
    for(unsigned i=0;i<12000;++i) {
        uint32_t target=random()%(source.data.size()+1);
        check(reader.seek(target,SEEK_SET),"random seek accepted");
        const uint32_t amount=random()%3000; std::vector<uint8_t> out(amount);
        const uint32_t count=reader.read(out.data(),amount);
        check(count==std::min<uint32_t>(amount,source.data.size()-target),"read length at EOF");
        check(std::equal(out.begin(),out.begin()+count,source.data.begin()+target),"random reads preserve bytes");
        check(reader.position()==target+count,"logical position ignores prefetch");
    }
    check(!reader.seek(-1,SEEK_SET),"negative seek rejected");
    check(!reader.seek(1,12345),"invalid seek origin rejected");
    check(reader.seek(-7,SEEK_END),"relative end seek");
    check(reader.seek(-3,SEEK_CUR),"relative current seek");
    source.pos=900;
    player::ReadAhead<MemorySource> afterTag(source,storage,sizeof(storage));
    check(afterTag.seek(0,SEEK_SET) && source.pos==0,"empty cache must not fake a seek");
    source.pos=0; source.reads=0;
    player::ReadAhead<MemorySource> sequential(source,storage,sizeof(storage));
    std::vector<uint8_t> out(source.data.size());
    for(size_t i=0;i<out.size();++i) check(sequential.read(&out[i],1)==1,"byte-wise reader");
    check(out==source.data && source.reads==7,"100003 one-byte reads use seven source reads");
    std::cout<<"read_ahead_source_calls="<<source.reads<<" for_bytes="<<out.size()<<'\n';
}
static void testBatch() {
    std::mt19937 random(3999);
    player::PcmBatch<256> batch; std::vector<uint8_t> result,expected;
    size_t calls=0;
    auto writer=[&](const uint8_t *data,size_t bytes) {
        ++calls; size_t n=std::min<size_t>(bytes,(random()%80)*4);
        result.insert(result.end(),data,data+n); return n;
    };
    for(unsigned i=0;i<200003;++i) {
        const uint32_t frame=random();
        const auto *bytes=reinterpret_cast<const uint8_t*>(&frame);
        expected.insert(expected.end(),bytes,bytes+4);
        while(!batch.push(frame,writer)) check(!batch.failed(),"valid short writes do not fail");
    }
    while(!batch.drain(writer)) check(!batch.failed(),"partial tail makes progress");
    check(result==expected,"PCM bytes identical under random short writes and backpressure");
    player::PcmBatch<1> bad;
    check(bad.push(1,writer),"first sample staged");
    check(!bad.push(2,[](const uint8_t*,size_t){return size_t(3);}) && bad.failed(),"misaligned sink write rejected");
    std::cout<<"partial_pcm_write_calls="<<calls<<" frames="<<expected.size()/4<<'\n';
}
static void testPcm() {
    std::mt19937 random(491);
    for(unsigned i=0;i<300000;++i) {
        int16_t l=random(),r=random(); const int16_t originalLeft=l, originalRight=r;
        uint8_t gain=random(); bool mono=random()&1;
        auto ref=[&](int32_t sample) { int32_t n=(sample*gain)>>6; return uint16_t(std::max(-32767,std::min(32767,n))); };
        if(mono) { const int16_t m=(int32_t(l)+r)>>1; l=r=m; }
        const uint32_t expected=(uint32_t(ref(r))<<16)|ref(l);
        // Old Q6 gain g == Q12 gain g*64: same bits, same saturation, same mono mix.
        check(player::packPcm(originalLeft,originalRight,uint16_t(gain*64),mono)==expected,"Q12 gain matches the Q6 baseline bit for bit");
    }
    for(unsigned i=0;i<100000;++i) {
        const int16_t s=random(); const uint16_t g=random()%(player::kUnityGainQ12+1);
        const int32_t a=player::amplify(s,g);
        check(std::abs(a)<=std::abs(int32_t(s)) && std::abs(a)<=32767,"gain <= unity never boosts or clips");
    }
    check(player::amplify(-32768,player::kUnityGainQ12)==-32767 && player::amplify(12345,player::kUnityGainQ12)==12345,"unity is transparent (symmetric clamp)");
}
static void testVolume() {
    using namespace player;
    for(uint16_t max : {kSpeakerMaxGainQ12,kEarbudMaxGainQ12,kUnityGainQ12}) {
        check(volumeGainQ12(0,max)==0 && volumeGainQ12(100,max)==max,"curve endpoints");
        for(unsigned v=1;v<=100;++v) check(volumeGainQ12(v,max)>=volumeGainQ12(v-1,max),"curve never decreases");
        for(unsigned v=101;v<=255;++v) check(volumeGainQ12(v,max)==max,"levels above 100 clamp to the max");
    }
    auto dB=[](double a,double b){ return 20*std::log10(a/b); };
    check(std::fabs(dB(volumeGainQ12(kSpeakerDefaultVolume,kSpeakerMaxGainQ12),5*64))<0.25,"speaker default = old 5/64 loudness");
    check(std::fabs(dB(volumeGainQ12(kEarbudDefaultVolume,kEarbudMaxGainQ12),2*64))<0.25,"earbud default = old 2/64 loudness");
    check(kEarbudMaxGainQ12==kEarbudMaxGainQ6*64 && kEarbudMaxGainQ12==320,"earbud cap unchanged (5/64)");
    check(kSpeakerMaxGainQ12<=kUnityGainQ12,"speaker never above unity");
    check(std::fabs(dB(volumeGainQ12(50,4096),4096)+18.06)<0.1,"half volume is about -18 dB");
    BatchedI2S output;
    check(output.gainQ12()==volumeGainQ12(kDefaultVolume,kMaxGainQ12),"adapter starts at the build default");
    output.setVolume(255); check(output.gainQ12()==kMaxGainQ12,"adapter clamps to this build's cap");
    check(!output.SetGain(4.0f) && output.gainQ12()==kMaxGainQ12,"library SetGain cannot bypass the cap");
    char artist[32], title[32];
    splitTrackName("TLC - No Scrubs",artist,sizeof(artist),title,sizeof(title));
    check(!strcmp(artist,"TLC") && !strcmp(title,"No Scrubs"),"artist - title split");
    splitTrackName("Intro",artist,sizeof(artist),title,sizeof(title));
    check(!artist[0] && !strcmp(title,"Intro"),"no dash: all title");
    splitTrackName(" - x",artist,sizeof(artist),title,sizeof(title));
    check(!artist[0] && !strcmp(title," - x"),"leading dash is not an artist");
    std::cout<<"volume_default_gain_q12="<<volumeGainQ12(kDefaultVolume,kMaxGainQ12)<<" max_q12="<<kMaxGainQ12<<'\n';
}
static void testActualAudioOutput() {
    pcmBytes.clear(); writeCalls=installs=uninstalls=0; writeLimit=SIZE_MAX;
    std::vector<uint8_t> expected;
    {
        BatchedI2S output; check(output.begin(),"actual I2S adapter starts");
        check(lastPins.mck_io_num==I2S_PIN_NO_CHANGE && lastPins.bck_io_num==48 && lastPins.ws_io_num==38 && lastPins.data_out_num==47,"known pinout and no MCLK");
        for(unsigned i=0;i<44100;++i) {
            if(i%player::kPcmSliceFrames==0) output.beginSlice();
            int16_t sample[2]={int16_t(i*17),int16_t(-int(i*11))};
            const uint32_t packed=player::packPcm(sample[0],sample[1],output.gainQ12(),player::kMonoOutput);
            const auto *bytes=reinterpret_cast<const uint8_t*>(&packed); expected.insert(expected.end(),bytes,bytes+4);
            check(output.ConsumeSample(sample),"actual audio accepts a valid slice");
        }
        output.flush(); check(pcmBytes==expected,"actual I2S adapter emits exact expected PCM");
        check(writeCalls==173,"one second at 44.1 kHz uses 173 batched writes rather than 44100");
        check(output.pause(true) && !i2sActive,"pause stops I2S clocks");
        check(output.pause(false) && i2sActive,"resume restarts I2S clocks");
        output.stop(); check(output.begin() && installs==1,"track restart reuses driver allocation");
        pcmBytes.clear(); output.beginSlice();
        int16_t quantumSample[2]={1024,-512};
        for(unsigned i=0;i<player::kPcmSliceFrames;++i)
            check(output.ConsumeSample(quantumSample),"slice accepts its exact frame budget");
        check(!output.ConsumeSample(quantumSample),"full slice yields to command handling");
        output.beginSlice(); check(output.ConsumeSample(quantumSample),"unaccepted sample retried in next slice");
        output.flush(); check(pcmBytes.size()==(player::kPcmSliceFrames+1)*4,"slice boundary loses or duplicates no PCM");
        output.stop(); check(output.begin(),"output restart before error injection");
        output.beginSlice(); mockWriteError=true;
        int16_t sample[2]={1,2};
        for(unsigned i=0;i<257;++i) output.ConsumeSample(sample);
        check(output.failed(),"write failures propagated"); mockWriteError=false;
    }
    check(uninstalls==1,"driver released once at shutdown");
    std::cout<<"i2s_writes_per_44100_frames=173 baseline=44100\n";
}
static void testActualLcd() {
    LCD_Init();
    bool endianFixed=false;
    for(size_t i=0;i+2<wireBytes.size();++i)
        if(!wireBytes[i].data && wireBytes[i].value==0xB0 && wireBytes[i+2].value==0xE0) endianFixed=true;
    check(endianFixed,"verified RGB565 byte-order correction retained");
    wireBytes.clear(); transactions=0;
    uint16_t pixels[4]={0x3412,0x7856,0xbc9a,0xf0de};
    LCD_addWindow(12,300,13,301,pixels);
    const uint8_t expected[]={0x2A,0,12,0,13,0x2B,1,44,1,45,0x2C,0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
    check(transactions==1 && wireBytes.size()==sizeof(expected),"one SPI transaction per rectangle");
    for(size_t i=0;i<sizeof(expected);++i) check(wireBytes[i].value==expected[i],"LCD window coordinates and pixel byte order");
    check(!wireBytes[0].data && !wireBytes[5].data && !wireBytes[10].data,"LCD commands use command mode");
    const size_t before=wireBytes.size(); LCD_addWindow(0,0,240,319,pixels);
    check(wireBytes.size()==before,"invalid rectangles emit nothing");
    Set_Backlight(100); check(pwmDuty==1023,"actual backlight full-scale value");
    LCD_SetSleep(true); check(pwmDuty==0,"sleep turns backlight fully off");
    const uint32_t at=mockTime; LCD_SetSleep(false);
    check(mockTime-at>=120,"LCD wake honors stabilization delay");
    std::cout<<"lcd_transactions_per_rect=1 baseline=12\n";
}
static void testTransfer() {
    using namespace player;
    check(sanitizeUploadName("Malcolm Todd - Chest Pain.mp3")=="Malcolm Todd - Chest Pain.mp3","plain names unchanged");
    check(sanitizeUploadName("a/b\\c:d*e?.MP3")=="c_d_e_.mp3","paths stripped, FAT-illegal chars replaced");
    check(sanitizeUploadName("Beyonc\xc3\xa9.mp3")=="Beyonc__.mp3","each non-ASCII byte becomes one underscore");
    check(sanitizeUploadName("  ..song.. .mp3")=="song.mp3","leading/trailing spaces and dots trimmed");
    check(sanitizeUploadName(".mp3")=="song.mp3" && sanitizeUploadName("")=="song.mp3","empty names get a default");
    check(sanitizeUploadName(std::string(200,'x')+".mp3").size()==100,"long names capped at 96+4 bytes");
    check(sanitizeUploadName("track.wav")=="track.wav.mp3","non-MP3 extension kept inside the stem");
    const std::string once=sanitizeUploadName("We\xe2\x80\x99re \"Live\" | 2024.mp3");
    check(sanitizeUploadName(once)==once,"sanitizing is idempotent (Mac and player agree)");
    check(hasMp3Extension("x.Mp3") && !hasMp3Extension("x.mp4") && !hasMp3Extension("mp3"),"extension check");
    check(numberedName("Song.mp3",1)=="Song.mp3" && numberedName("Song.mp3",3)=="Song (3).mp3","collision names");
    check(wifiQrPayload("Addy's MP3-Player","whyisadityasupercool")=="WIFI:T:WPA;S:Addy's MP3-Player;P:whyisadityasupercool;;","Wi-Fi QR payload");
    check(wifiQrEscape("a;b,c:d\"e\\")=="a\\;b\\,c\\:d\\\"e\\\\","Wi-Fi QR escaping");
    check(jsonEscape("a\"b\\c\n")=="a\\\"b\\\\c\\u000a","JSON escaping");
    check(!sessionExpired(299999,0,300000) && sessionExpired(300000,0,300000),"5 minute idle timeout");
    check(sessionExpired(1000,UINT32_MAX-298999,300000),"idle timeout handles millis wrap");
    check(sessionSecondsLeft(0,0,300000)==300 && sessionSecondsLeft(299001,0,300000)==1,"countdown rounds up");
}
static void testButtons() {
    using namespace player;
    const ButtonTiming t{25,350,700};
    auto run=[&](ButtonGesture &b,std::vector<std::pair<uint32_t,bool>> edges,uint32_t until){
        std::vector<ButtonEvent> events; bool level=false; size_t next=0;
        for(uint32_t now=0;now<=until;now+=5) {
            while(next<edges.size() && edges[next].first<=now) level=edges[next++].second;
            const ButtonEvent e=b.update(level,now); if(e!=ButtonEvent::None) events.push_back(e);
        }
        return events;
    };
    { ButtonGesture b(t); auto e=run(b,{{100,true},{200,false}},1200);
      check(e.size()==1 && e[0]==ButtonEvent::Single,"single click after double window"); }
    { ButtonGesture b(t); auto e=run(b,{{100,true},{180,false},{300,true},{380,false}},1500);
      check(e.size()==1 && e[0]==ButtonEvent::Double,"double click"); }
    { ButtonGesture b(t); auto e=run(b,{{100,true},{1000,false}},2000);
      check(e.size()==1 && e[0]==ButtonEvent::Long,"long press, release ignored"); }
    { ButtonGesture b(t); auto e=run(b,{{100,true},{105,false},{110,true},{115,false},{120,true},{220,false}},1200);
      check(e.size()==1 && e[0]==ButtonEvent::Single,"contact bounce debounced"); }
    { ButtonGesture b(t,true); auto e=run(b,{{100,true},{200,false},{300,true},{400,false}},1200);
      check(e.size()==2 && e[0]==ButtonEvent::Single && e[1]==ButtonEvent::Single,"immediate-single mode"); }
    { ButtonGesture b(t); auto e=run(b,{{100,true},{180,false},{800,true},{880,false}},2000);
      check(e.size()==2 && e[0]==ButtonEvent::Single && e[1]==ButtonEvent::Single,"slow clicks are two singles"); }
}
int main() {
    testPolicy(); testTags(); testReadAhead(); testBatch(); testPcm(); testVolume(); testActualAudioOutput(); testActualLcd(); testTransfer(); testButtons();
    std::cout<<"PASS: core, PCM fidelity, volume curve/cap, short writes, I2S lifecycle, ID3 bounds, read-ahead, sleep/wake, LCD protocol, upload names, buttons\n";
}
